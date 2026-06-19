#include "ecosystem_system.hpp"
#include "tick_system.hpp"
#include "day_night_def.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"
#include "creature_species.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void EcosystemSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;

    // Register built-in species definitions.
    if (species_registry_.species_count() == 0) {
        species_registry_.register_builtin_species();
    }

    // Register default biome overrides with species lists.
    if (params_.biome_override_count == 0) {
        register_default_biome_overrides();
    }

    // Restore population cells from any already-loaded ChunkData.
    // This is important after a world load where chunks are already
    // in WorldData but EcosystemSystem's populations_ map is empty.
    restore_populations_from_chunks();
    restore_captive_from_chunks();
}

void EcosystemSystem::tick_active(const ChunkKey& chunk, float delta,
                                  const TickContext* ctx) {
    if (!world_) return;

    // Check if ecosystem is enabled for this dimension.
    const GameplayConfig& gc = world_->gameplay_config();
    if (!gc.is_ecosystem_enabled(chunk.dimension_id)) return;

    // Update cached season from WorldData tick counter.
    const int64_t tick = world_->current_tick();
    constexpr float kTicksPerSecond = 20.0f;
    const int64_t ticks_per_day = static_cast<int64_t>(
        gc.day_length_seconds * kTicksPerSecond);
    current_season_ = season_from_tick(tick, gc.days_per_season, ticks_per_day);

    // Update cached day/night state.
    const float time_of_day = compute_time_of_day(tick, ticks_per_day);
    const DayNightState dn = compute_day_night_state(
        time_of_day, gc.twilight_fraction);
    is_daytime_ = dn.is_daytime;

    // Ensure this chunk has a population cell.
    PopulationCell& cell = ensure_population_cell(chunk);

    // Apply ecosystem rate multiplier from GameplayConfig.
    const float rate_mult = gc.get_ecosystem_rate_multiplier(
        chunk.dimension_id);

    // Advance the population equations by 1 tick (delta at 20 TPS = 0.05s).
    // Convert delta to tick units: dt = delta * kTicksPerSecond.
    const float dt = delta * TickSystem::kTicksPerSecond * rate_mult;
    advance_population(cell, dt);

    // --- Proxy creature management ---

    // If this chunk has no proxy group yet, spawn proxies.
    bool is_new_active = (active_proxies_.find(chunk) == active_proxies_.end());
    if (is_new_active) {
        spawn_proxies_for_chunk(chunk, tick);
        // Spawn render nodes for captive creatures in this chunk.
        spawn_captive_for_chunk(chunk);
    } else if (params_.proxy_rebalance_interval_ticks > 0 &&
               tick % params_.proxy_rebalance_interval_ticks == 0) {
        rebalance_proxies(chunk, tick);
    }

    // Update proxy creature AI (wandering, fleeing).
    tick_proxies(chunk, tick, delta);

    // Tick captive creatures (taming, growth, gestation, wander).
    tick_captive(chunk, tick, delta);

    // Run diffusion at the configured interval.
    if (params_.diffusion_interval_ticks > 0 &&
        tick % params_.diffusion_interval_ticks == 0) {
        diffuse_populations();
        ++diffusion_count_;
    }

    // Sync population cell to ChunkData at diffusion cadence.
    // This ensures persisted data is reasonably up-to-date
    // without writing every tick (would be wasteful).
    if (params_.diffusion_interval_ticks > 0 &&
        tick % params_.diffusion_interval_ticks == 0) {
        sync_population_to_chunk(chunk);
        sync_captive_to_chunk(chunk);
    }
}

void EcosystemSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                    const TickContext* ctx) {
    if (!world_) return;

    // Check if ecosystem is enabled for this dimension.
    const GameplayConfig& gc = world_->gameplay_config();
    if (!gc.is_ecosystem_enabled(chunk.dimension_id)) return;

    // Despawn proxy creatures when chunk transitions to sleeping.
    // This is safe to call every sleeping tick because despawn_proxies
    // is idempotent (no-ops if no proxies exist).
    despawn_proxies_for_chunk(chunk);
    // Despawn captive render nodes too (data persists, rendering stops).
    despawn_captive_for_chunk(chunk);

    // Same-speed low-frequency: same equations, just called less often.
    // Delta is already scaled by the sleep interval, so we convert
    // to accumulated ticks and advance once.
    PopulationCell& cell = ensure_population_cell(chunk);

    const float rate_mult = gc.get_ecosystem_rate_multiplier(
        chunk.dimension_id);
    const float dt = delta * TickSystem::kTicksPerSecond * rate_mult;
    advance_population(cell, dt);
}

void EcosystemSystem::shutdown() {
    populations_.clear();
    captive_.clear();
    active_proxies_.clear();
}

// --- Population cell access ---

PopulationCell* EcosystemSystem::get_population_cell(const ChunkKey& key) {
    auto it = populations_.find(key);
    return (it != populations_.end()) ? &it->second : nullptr;
}

const PopulationCell* EcosystemSystem::get_population_cell(
    const ChunkKey& key) const {
    auto it = populations_.find(key);
    return (it != populations_.end()) ? &it->second : nullptr;
}

PopulationCell& EcosystemSystem::ensure_population_cell(const ChunkKey& key) {
    auto it = populations_.find(key);
    if (it != populations_.end()) {
        return it->second;
    }

    // Check if ChunkData has a persisted population cell.
    if (world_) {
        const ChunkData* chunk = world_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk && chunk->has_population_cell) {
            auto [insert_it, _] = populations_.emplace(
                key, chunk->population_cell);
            return insert_it->second;
        }
    }

    // Create a new population cell with defaults.
    PopulationCell cell;

    // Infer biome type from terrain material composition.
    cell.biome_type = infer_biome_type(key);

    // Apply biome override if available.
    const EcosystemParams::BiomeOverride* bo =
        params_.find_biome_override(cell.biome_type);
    if (bo) {
        cell.water_availability = bo->base_water;
        cell.soil_fertility = bo->base_fertility;
    }

    auto [insert_it, _] = populations_.emplace(key, cell);
    return insert_it->second;
}

void EcosystemSystem::remove_population_cell(const ChunkKey& key) {
    populations_.erase(key);
}

// --- Core equation advancement ---

void EcosystemSystem::advance_population(PopulationCell& cell, float dt) {
    if (dt <= 0.0f) return;

    const ResolvedParams rp = resolve_params(cell);
    const int season_idx = static_cast<int>(current_season_);
    const float season_veg_mod = (season_idx >= 0 && season_idx < 4)
        ? params_.season_veg_growth_mod[season_idx] : 1.0f;
    const float season_herb_mod = (season_idx >= 0 && season_idx < 4)
        ? params_.season_herb_repro_mod[season_idx] : 1.0f;
    const float season_pred_mod = (season_idx >= 0 && season_idx < 4)
        ? params_.season_pred_repro_mod[season_idx] : 1.0f;
    const float season_water_mod = (season_idx >= 0 && season_idx < 4)
        ? params_.season_water_mod[season_idx] : 0.0f;

    // Day/night modifiers: at night, herbivores are less active
    // and predators are more active.
    const float day_veg_mod = is_daytime_ ? 1.0f : params_.night_veg_growth_mod;
    const float day_herb_mod = is_daytime_ ? 1.0f : params_.night_herb_activity_mod;
    const float day_pred_mod = is_daytime_ ? 1.0f : params_.night_pred_activity_mod;

    // Effective water availability: biome base + seasonal modifier.
    const float water = std::clamp(
        rp.base_water + season_water_mod, 0.0f, 1.0f);
    cell.water_availability = water;

    // --- Vegetation dynamics ---
    // dV/dt = growth - decay - grazing
    // growth = veg_growth_rate * fertility * water * season_mod * day_mod
    const float veg_growth = rp.veg_growth_rate
        * cell.soil_fertility * water * season_veg_mod * day_veg_mod;
    const float veg_decay = rp.veg_decay_rate * cell.vegetation_density;
    const float grazing = rp.graze_rate * cell.herbivore_density
        * cell.vegetation_density * day_herb_mod;
    cell.vegetation_density += (veg_growth - veg_decay - grazing) * dt;
    cell.vegetation_density = std::clamp(cell.vegetation_density,
        0.0f, rp.max_vegetation);

    // --- Herbivore dynamics ---
    // dH/dt = reproduction - natural_death - predation - hunting_death
    // reproduction = herb_repro_rate * V * H * season_mod * day_mod
    // hunting_death = hunting_pressure_herb * H
    const float herb_repro = rp.herb_repro_rate
        * cell.vegetation_density * cell.herbivore_density
        * season_herb_mod * day_herb_mod;
    const float herb_death = rp.herb_natural_death * cell.herbivore_density;
    const float predation = rp.predation_rate
        * cell.predator_density * cell.herbivore_density * day_pred_mod;
    const float herb_hunting_death = cell.hunting_pressure_herb
        * cell.herbivore_density;
    cell.herbivore_density += (herb_repro - herb_death - predation
        - herb_hunting_death) * dt;
    cell.herbivore_density = std::clamp(cell.herbivore_density,
        0.0f, rp.max_herbivore);

    // --- Predator dynamics ---
    // dP/dt = reproduction - natural_death - hunting_death
    // reproduction = pred_repro_rate * H * P * season_mod * day_mod
    // hunting_death = hunting_pressure_pred * P
    const float pred_repro = rp.pred_repro_rate
        * cell.herbivore_density * cell.predator_density
        * season_pred_mod * day_pred_mod;
    const float pred_death = rp.pred_natural_death * cell.predator_density;
    const float pred_hunting_death = cell.hunting_pressure_pred
        * cell.predator_density;
    cell.predator_density += (pred_repro - pred_death - pred_hunting_death) * dt;
    cell.predator_density = std::clamp(cell.predator_density,
        0.0f, rp.max_predator);

    // --- Hunting pressure decay ---
    // Pressure decays each tick by a multiplicative factor.
    cell.hunting_pressure_herb *= params_.hunting_pressure_decay;
    cell.hunting_pressure_pred *= params_.hunting_pressure_decay;
    // Clamp to zero when negligible to avoid floating-point drift.
    if (cell.hunting_pressure_herb < 1e-6f) cell.hunting_pressure_herb = 0.0f;
    if (cell.hunting_pressure_pred < 1e-6f) cell.hunting_pressure_pred = 0.0f;

    // --- Nutrient cycling ---
    // Dead biomass from animal deaths (natural + hunting) and vegetation decay.
    const float animal_death_biomass = (herb_death + pred_death
        + herb_hunting_death + pred_hunting_death)
        * rp.death_to_biomass_fraction;
    const float veg_decay_biomass = veg_decay
        * rp.veg_decay_to_biomass_fraction;
    cell.dead_biomass += (animal_death_biomass + veg_decay_biomass) * dt;

    // Decomposition: dead_biomass → soil_fertility.
    const float decomposed = rp.decompose_rate * cell.dead_biomass;
    cell.dead_biomass -= decomposed * dt;
    cell.soil_fertility += decomposed * dt;

    // Final clamp to valid ranges.
    cell.clamp_all();
}

// --- Parameter resolution ---

EcosystemSystem::ResolvedParams EcosystemSystem::resolve_params(
    const PopulationCell& cell) const {
    ResolvedParams rp;
    rp.veg_growth_rate = params_.veg_growth_rate;
    rp.veg_decay_rate = params_.veg_decay_rate;
    rp.graze_rate = params_.graze_rate;
    rp.herb_repro_rate = params_.herb_repro_rate;
    rp.herb_natural_death = params_.herb_natural_death;
    rp.predation_rate = params_.predation_rate;
    rp.pred_repro_rate = params_.pred_repro_rate;
    rp.pred_natural_death = params_.pred_natural_death;
    rp.decompose_rate = params_.decompose_rate;
    rp.death_to_biomass_fraction = params_.death_to_biomass_fraction;
    rp.veg_decay_to_biomass_fraction = params_.veg_decay_to_biomass_fraction;
    rp.max_vegetation = 1.0f;
    rp.max_herbivore = 1.0f;
    rp.max_predator = 1.0f;
    rp.base_water = 0.5f;

    // Apply biome overrides if available.
    const EcosystemParams::BiomeOverride* bo =
        params_.find_biome_override(cell.biome_type);
    if (bo) {
        rp.veg_growth_rate *= bo->veg_growth_multiplier;
        rp.max_vegetation = bo->max_vegetation;
        rp.max_herbivore = bo->max_herbivore;
        rp.max_predator = bo->max_predator;
        rp.base_water = bo->base_water;
    }

    return rp;
}

// --- Diffusion ---

std::vector<ChunkKey> EcosystemSystem::get_neighbor_keys(
    const ChunkKey& key) const {
    std::vector<ChunkKey> neighbors;
    neighbors.reserve(6);

    // 6-axis neighbors (±x, ±y, ±z).
    const int offsets[6][3] = {
        { 1,  0,  0},
        {-1,  0,  0},
        { 0,  1,  0},
        { 0, -1,  0},
        { 0,  0,  1},
        { 0,  0, -1},
    };

    for (int i = 0; i < 6; ++i) {
        neighbors.emplace_back(
            key.dimension_id,
            key.chunk_x + offsets[i][0],
            key.chunk_y + offsets[i][1],
            key.chunk_z + offsets[i][2]);
    }

    return neighbors;
}

void EcosystemSystem::diffuse_populations() {
    const float rate = params_.diffusion_rate;
    if (rate <= 0.0f) return;

    const int budget = params_.max_diffusion_pairs_per_pass;
    diffusion_pairs_processed_ = 0;

    // Accumulate diffusion deltas to avoid order-dependent results.
    // We use a separate map of deltas to apply all at once.
    std::unordered_map<ChunkKey, PopulationCell> deltas;

    for (const auto& [key, cell] : populations_) {
        // Check diffusion budget.
        if (budget > 0 && diffusion_pairs_processed_ >= budget) break;

        const auto neighbors = get_neighbor_keys(key);
        for (const ChunkKey& nk : neighbors) {
            // Check diffusion budget per pair.
            if (budget > 0 && diffusion_pairs_processed_ >= budget) break;

            auto nit = populations_.find(nk);
            if (nit == populations_.end()) continue;

            const PopulationCell& neighbor = nit->second;

            // Diffuse vegetation density (seed spread).
            const float veg_diff = (cell.vegetation_density
                - neighbor.vegetation_density) * rate;
            deltas[key].vegetation_density -= veg_diff;
            deltas[nk].vegetation_density += veg_diff;

            // Diffuse herbivore density (migration).
            const float herb_diff = (cell.herbivore_density
                - neighbor.herbivore_density) * rate;
            deltas[key].herbivore_density -= herb_diff;
            deltas[nk].herbivore_density += herb_diff;

            // Diffuse predator density (territory shift).
            const float pred_diff = (cell.predator_density
                - neighbor.predator_density) * rate;
            deltas[key].predator_density -= pred_diff;
            deltas[nk].predator_density += pred_diff;

            ++diffusion_pairs_processed_;
        }
    }

    // Apply accumulated deltas.
    for (const auto& [key, delta] : deltas) {
        auto it = populations_.find(key);
        if (it == populations_.end()) continue;

        it->second.vegetation_density += delta.vegetation_density;
        it->second.herbivore_density += delta.herbivore_density;
        it->second.predator_density += delta.predator_density;
        it->second.clamp_all();
    }
}

// --- Diagnostics ---

float EcosystemSystem::total_vegetation() const {
    float total = 0.0f;
    for (const auto& [_, cell] : populations_) {
        total += cell.vegetation_density;
    }
    return total;
}

float EcosystemSystem::total_herbivore() const {
    float total = 0.0f;
    for (const auto& [_, cell] : populations_) {
        total += cell.herbivore_density;
    }
    return total;
}

float EcosystemSystem::total_predator() const {
    float total = 0.0f;
    for (const auto& [_, cell] : populations_) {
        total += cell.predator_density;
    }
    return total;
}

// --- Persistence ---

void EcosystemSystem::sync_population_to_chunk(const ChunkKey& key) {
    if (!world_) return;

    auto pop_it = populations_.find(key);
    if (pop_it == populations_.end()) return;

    ChunkData* chunk = world_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (!chunk) return;

    chunk->has_population_cell = true;
    chunk->population_cell = pop_it->second;
}

void EcosystemSystem::sync_all_populations_to_chunks() {
    if (!world_) return;

    for (const auto& [key, cell] : populations_) {
        ChunkData* chunk = world_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk) continue;

        chunk->has_population_cell = true;
        chunk->population_cell = cell;
    }
}

void EcosystemSystem::restore_populations_from_chunks() {
    if (!world_) return;

    for (const auto& key : world_->all_chunk_keys()) {
        const ChunkData* chunk = world_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk || !chunk->has_population_cell) continue;

        // Only restore if we don't already have a population cell
        // for this chunk (avoid overwriting runtime state).
        auto it = populations_.find(key);
        if (it != populations_.end()) continue;

        populations_.emplace(key, chunk->population_cell);
    }
}

// --- Captive persistence ---

void EcosystemSystem::sync_captive_to_chunk(const ChunkKey& key) {
    if (!world_) return;

    auto it = captive_.find(key);
    ChunkData* chunk = world_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (!chunk) return;

    if (it == captive_.end() || it->second.empty()) {
        chunk->has_captive_creatures = false;
        chunk->captive_creatures.clear();
        return;
    }

    chunk->has_captive_creatures = true;
    chunk->captive_creatures = it->second;
}

void EcosystemSystem::sync_all_captive_to_chunks() {
    if (!world_) return;

    for (const auto& [key, creatures] : captive_) {
        ChunkData* chunk = world_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk) continue;
        if (creatures.empty()) {
            chunk->has_captive_creatures = false;
            chunk->captive_creatures.clear();
        } else {
            chunk->has_captive_creatures = true;
            chunk->captive_creatures = creatures;
        }
    }
}

void EcosystemSystem::restore_captive_from_chunks() {
    if (!world_) return;

    for (const auto& key : world_->all_chunk_keys()) {
        const ChunkData* chunk = world_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (!chunk || !chunk->has_captive_creatures) continue;
        if (chunk->captive_creatures.empty()) continue;

        // Only restore if not already present.
        auto it = captive_.find(key);
        if (it != captive_.end()) continue;

        // Assign fresh runtime ids for rendering.
        std::vector<CaptiveCreature> list = chunk->captive_creatures;
        for (auto& cc : list) {
            cc.runtime_id = next_captive_runtime_id();
        }
        captive_.emplace(key, std::move(list));
    }
}

uint64_t EcosystemSystem::next_captive_runtime_id() {
    return captive_runtime_id_counter_++;
}

// --- Captive creature terrain queries ---

bool EcosystemSystem::is_cell_solid(const std::string& dimension,
                                    int32_t bx, int32_t by, int32_t bz) const {
    if (!world_) return false;
    constexpr int kChunkSize = ChunkData::kChunkSize;
    int cx = bx >= 0 ? bx / kChunkSize : (bx - kChunkSize + 1) / kChunkSize;
    int cy = by >= 0 ? by / kChunkSize : (by - kChunkSize + 1) / kChunkSize;
    int cz = bz >= 0 ? bz / kChunkSize : (bz - kChunkSize + 1) / kChunkSize;
    const ChunkData* chunk = world_->get_chunk(dimension, cx, cy, cz);
    if (!chunk) return false;
    int lx = bx - cx * kChunkSize;
    int ly = by - cy * kChunkSize;
    int lz = bz - cz * kChunkSize;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return false;
    return chunk->terrain.cell_at(lx, ly, lz).is_solid();
}

bool EcosystemSystem::detect_enclosure(
    const std::string& dimension,
    int32_t start_x, int32_t start_y, int32_t start_z,
    int32_t& out_min_x, int32_t& out_min_y, int32_t& out_min_z,
    int32_t& out_max_x, int32_t& out_max_y, int32_t& out_max_z) const {
    // Bounded BFS flood-fill. Solid cells are walls; air/non-solid cells
    // are interior. If the flood stays within the max bounding box and
    // visits <= enclosure_max_cells, the region is enclosed.
    const int extent = params_.enclosure_max_extent;
    const size_t max_cells = static_cast<size_t>(params_.enclosure_max_cells);

    int32_t min_x = start_x - extent, max_x = start_x + extent;
    int32_t min_y = start_y - extent, max_y = start_y + extent;
    int32_t min_z = start_z - extent, max_z = start_z + extent;

    // If the start cell itself is solid, it's not a valid interior point.
    if (is_cell_solid(dimension, start_x, start_y, start_z)) {
        return false;
    }

    struct Cell3 { int32_t x, y, z; };
    std::vector<Cell3> frontier;
    frontier.push_back({start_x, start_y, start_z});

    // Visited set keyed by packed int64 (x<<40 | y<<20 | z) — but coords
    // can be negative, so use a hash set of tuples via a string-free key.
    auto pack = [](int32_t x, int32_t y, int32_t z) -> int64_t {
        // Offset to non-negative within ±extent (extent <= 24 → +32 safe).
        return (int64_t(x + 32768) << 40)
             | (int64_t(y + 32768) << 20)
             | (int64_t(z + 32768));
    };
    std::unordered_map<int64_t, bool> visited;
    visited.reserve(1024);
    visited[pack(start_x, start_y, start_z)] = true;

    int32_t bb_min_x = start_x, bb_max_x = start_x;
    int32_t bb_min_y = start_y, bb_max_y = start_y;
    int32_t bb_min_z = start_z, bb_max_z = start_z;

    size_t visited_count = 1;
    static const int kDx[6] = {1, -1, 0, 0, 0, 0};
    static const int kDy[6] = {0, 0, 1, -1, 0, 0};
    static const int kDz[6] = {0, 0, 0, 0, 1, -1};

    while (!frontier.empty()) {
        Cell3 cur = frontier.back();
        frontier.pop_back();

        for (int i = 0; i < 6; ++i) {
            int32_t nx = cur.x + kDx[i];
            int32_t ny = cur.y + kDy[i];
            int32_t nz = cur.z + kDz[i];

            // Exceeded bounding box → not enclosed (open to the outside).
            if (nx < min_x || nx > max_x ||
                ny < min_y || ny > max_y ||
                nz < min_z || nz > max_z) {
                return false;
            }

            // Solid wall — boundary, do not cross.
            if (is_cell_solid(dimension, nx, ny, nz)) continue;

            int64_t key = pack(nx, ny, nz);
            if (visited.count(key)) continue;
            visited[key] = true;
            ++visited_count;
            if (visited_count > max_cells) return false;

            if (nx < bb_min_x) bb_min_x = nx;
            if (nx > bb_max_x) bb_max_x = nx;
            if (ny < bb_min_y) bb_min_y = ny;
            if (ny > bb_max_y) bb_max_y = ny;
            if (nz < bb_min_z) bb_min_z = nz;
            if (nz > bb_max_z) bb_max_z = nz;

            frontier.push_back({nx, ny, nz});
        }
    }

    out_min_x = bb_min_x; out_min_y = bb_min_y; out_min_z = bb_min_z;
    out_max_x = bb_max_x; out_max_y = bb_max_y; out_max_z = bb_max_z;
    return true;
}

// --- Proxy creature management ---

const EcosystemSystem::ProxyGroup* EcosystemSystem::get_proxy_group(
    const ChunkKey& key) const {
    auto it = active_proxies_.find(key);
    return (it != active_proxies_.end()) ? &it->second : nullptr;
}

size_t EcosystemSystem::total_proxy_count() const {
    size_t count = 0;
    for (const auto& [_, group] : active_proxies_) {
        count += group.herbivore_ids.size() + group.predator_ids.size();
    }
    return count;
}

int EcosystemSystem::density_to_proxy_count(
    float density, float min_threshold) const {
    if (density < min_threshold) return 0;
    // Linear mapping: density [min_threshold, 1.0] → count [1, max_proxies].
    const float t = (density - min_threshold) / (1.0f - min_threshold);
    const int count = 1 + static_cast<int>(
        t * (params_.max_proxies_per_chunk - 1));
    return std::clamp(count, 0, params_.max_proxies_per_chunk);
}

uint16_t EcosystemSystem::pick_species_for_biome(
    CreatureRole role, uint8_t biome_type) const {
    // Check biome override for species list.
    const EcosystemParams::BiomeOverride* bo =
        params_.find_biome_override(biome_type);
    if (bo) {
        const auto& species_list = (role == CreatureRole::PREDATOR)
            ? bo->pred_species_ids : bo->herb_species_ids;
        if (!species_list.empty()) {
            // Simple deterministic pick using tick-based hash.
            const int64_t tick = world_ ? world_->current_tick() : 0;
            const size_t idx = static_cast<size_t>(tick) % species_list.size();
            return species_list[idx];
        }
    }

    // Fallback: find first registered species of the requested role.
    for (uint16_t id = 1; id <= creature_species::kMaxBuiltinId; ++id) {
        const CreatureSpeciesDef* def = species_registry_.get_species(id);
        if (def && def->role == role) return id;
    }

    // Ultimate fallback: return 0 (invalid, should not happen).
    return 0;
}

void EcosystemSystem::random_spawn_position_in_chunk(
    const ChunkKey& chunk,
    int32_t& out_x, int32_t& out_y, int32_t& out_z) const {
    // Use a simple hash of chunk coords + tick for deterministic randomness.
    // This avoids pulling in <random> and is sufficient for spawn positions.
    const int64_t tick = world_ ? world_->current_tick() : 0;
    const int64_t seed = static_cast<int64_t>(chunk.chunk_x) * 73856093 +
                         static_cast<int64_t>(chunk.chunk_y) * 19349663 +
                         static_cast<int64_t>(chunk.chunk_z) * 83492791 +
                         tick * 23456789;
    constexpr int kChunkSize = 32;
    out_x = chunk.chunk_x * kChunkSize +
        static_cast<int32_t>((seed & 0xFF) % kChunkSize);
    out_y = chunk.chunk_y * kChunkSize +
        static_cast<int32_t>(((seed >> 8) & 0xFF) % kChunkSize);
    out_z = chunk.chunk_z * kChunkSize +
        static_cast<int32_t>(((seed >> 16) & 0xFF) % kChunkSize);
}

void EcosystemSystem::spawn_proxies_for_chunk(
    const ChunkKey& chunk, int64_t tick) {
    if (!world_) return;

    const PopulationCell* cell = get_population_cell(chunk);
    if (!cell) return;

    auto& registry = world_->block_entity_registry();

    ProxyGroup group;

    // Spawn herbivore proxies.
    const int herb_count = density_to_proxy_count(
        cell->herbivore_density, params_.min_herb_density_for_proxy);
    for (int i = 0; i < herb_count; ++i) {
        int32_t sx, sy, sz;
        random_spawn_position_in_chunk(chunk, sx, sy, sz);
        sx += i * 3;

        uint16_t species = pick_species_for_biome(
            CreatureRole::HERBIVORE, cell->biome_type);
        EntityId id = registry.register_creature_entity(
            chunk.dimension_id, sx, sy, sz,
            species, CreatureRole::HERBIVORE, tick);
        group.herbivore_ids.push_back(id);

        CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
        if (cs) {
            cs->pos_x = static_cast<float>(sx);
            cs->pos_y = static_cast<float>(sy);
            cs->pos_z = static_cast<float>(sz);
            cs->next_wander_tick = tick + params_.wander_interval_ticks;
        }

        if (event_bus_) {
            const CreatureSpeciesDef* def =
                species_registry_.get_species(species);
            const char* name = def ? def->species_key.c_str() : "herbivore";
            event_bus_->emit(GameEvent::creature_spawned(
                id.id, name,
                chunk.dimension_id,
                chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
        }
    }

    // Spawn predator proxies.
    const int pred_count = density_to_proxy_count(
        cell->predator_density, params_.min_pred_density_for_proxy);
    for (int i = 0; i < pred_count; ++i) {
        int32_t sx, sy, sz;
        random_spawn_position_in_chunk(chunk, sx, sy, sz);
        sx += i * 3 + 1;
        sz += i * 2;

        uint16_t species = pick_species_for_biome(
            CreatureRole::PREDATOR, cell->biome_type);
        EntityId id = registry.register_creature_entity(
            chunk.dimension_id, sx, sy, sz,
            species, CreatureRole::PREDATOR, tick);
        group.predator_ids.push_back(id);

        CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
        if (cs) {
            cs->pos_x = static_cast<float>(sx);
            cs->pos_y = static_cast<float>(sy);
            cs->pos_z = static_cast<float>(sz);
            cs->next_wander_tick = tick + params_.wander_interval_ticks;
        }

        if (event_bus_) {
            const CreatureSpeciesDef* def =
                species_registry_.get_species(species);
            const char* name = def ? def->species_key.c_str() : "predator";
            event_bus_->emit(GameEvent::creature_spawned(
                id.id, name,
                chunk.dimension_id,
                chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
        }
    }

    active_proxies_[chunk] = std::move(group);
}

void EcosystemSystem::despawn_proxies_for_chunk(const ChunkKey& chunk) {
    auto it = active_proxies_.find(chunk);
    if (it == active_proxies_.end()) return;

    if (world_) {
        auto& registry = world_->block_entity_registry();

        for (EntityId id : it->second.herbivore_ids) {
            if (event_bus_) {
                const CreatureBlockEntityState* cs =
                    registry.get_creature_state(id);
                const char* name = "herbivore";
                if (cs) {
                    const CreatureSpeciesDef* def =
                        species_registry_.get_species(cs->species_id);
                    if (def) name = def->species_key.c_str();
                }
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }

        for (EntityId id : it->second.predator_ids) {
            if (event_bus_) {
                const CreatureBlockEntityState* cs =
                    registry.get_creature_state(id);
                const char* name = "predator";
                if (cs) {
                    const CreatureSpeciesDef* def =
                        species_registry_.get_species(cs->species_id);
                    if (def) name = def->species_key.c_str();
                }
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }
    }

    active_proxies_.erase(it);
}

void EcosystemSystem::rebalance_proxies(
    const ChunkKey& chunk, int64_t tick) {
    if (!world_) return;

    const PopulationCell* cell = get_population_cell(chunk);
    if (!cell) return;

    auto it = active_proxies_.find(chunk);
    if (it == active_proxies_.end()) {
        spawn_proxies_for_chunk(chunk, tick);
        return;
    }

    ProxyGroup& group = it->second;
    auto& registry = world_->block_entity_registry();

    // --- Rebalance herbivores ---
    const int desired_herb = density_to_proxy_count(
        cell->herbivore_density, params_.min_herb_density_for_proxy);
    const int current_herb = static_cast<int>(group.herbivore_ids.size());

    if (desired_herb > current_herb) {
        for (int i = 0; i < desired_herb - current_herb; ++i) {
            int32_t sx, sy, sz;
            random_spawn_position_in_chunk(chunk, sx, sy, sz);
            sx += (current_herb + i) * 3;

            uint16_t species = pick_species_for_biome(
                CreatureRole::HERBIVORE, cell->biome_type);
            EntityId id = registry.register_creature_entity(
                chunk.dimension_id, sx, sy, sz,
                species, CreatureRole::HERBIVORE, tick);
            group.herbivore_ids.push_back(id);

            CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
            if (cs) {
                cs->pos_x = static_cast<float>(sx);
                cs->pos_y = static_cast<float>(sy);
                cs->pos_z = static_cast<float>(sz);
                cs->next_wander_tick = tick + params_.wander_interval_ticks;
            }

            if (event_bus_) {
                const CreatureSpeciesDef* def =
                    species_registry_.get_species(species);
                const char* name = def ? def->species_key.c_str() : "herbivore";
                event_bus_->emit(GameEvent::creature_spawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
        }
    } else if (desired_herb < current_herb) {
        for (int i = desired_herb; i < current_herb; ++i) {
            EntityId id = group.herbivore_ids[i];
            if (event_bus_) {
                const CreatureBlockEntityState* cs =
                    registry.get_creature_state(id);
                const char* name = "herbivore";
                if (cs) {
                    const CreatureSpeciesDef* def =
                        species_registry_.get_species(cs->species_id);
                    if (def) name = def->species_key.c_str();
                }
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }
        group.herbivore_ids.resize(desired_herb);
    }

    // --- Rebalance predators ---
    const int desired_pred = density_to_proxy_count(
        cell->predator_density, params_.min_pred_density_for_proxy);
    const int current_pred = static_cast<int>(group.predator_ids.size());

    if (desired_pred > current_pred) {
        for (int i = 0; i < desired_pred - current_pred; ++i) {
            int32_t sx, sy, sz;
            random_spawn_position_in_chunk(chunk, sx, sy, sz);
            sx += (current_pred + i) * 3 + 1;
            sz += (current_pred + i) * 2;

            uint16_t species = pick_species_for_biome(
                CreatureRole::PREDATOR, cell->biome_type);
            EntityId id = registry.register_creature_entity(
                chunk.dimension_id, sx, sy, sz,
                species, CreatureRole::PREDATOR, tick);
            group.predator_ids.push_back(id);

            CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
            if (cs) {
                cs->pos_x = static_cast<float>(sx);
                cs->pos_y = static_cast<float>(sy);
                cs->pos_z = static_cast<float>(sz);
                cs->next_wander_tick = tick + params_.wander_interval_ticks;
            }

            if (event_bus_) {
                const CreatureSpeciesDef* def =
                    species_registry_.get_species(species);
                const char* name = def ? def->species_key.c_str() : "predator";
                event_bus_->emit(GameEvent::creature_spawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
        }
    } else if (desired_pred < current_pred) {
        for (int i = desired_pred; i < current_pred; ++i) {
            EntityId id = group.predator_ids[i];
            if (event_bus_) {
                const CreatureBlockEntityState* cs =
                    registry.get_creature_state(id);
                const char* name = "predator";
                if (cs) {
                    const CreatureSpeciesDef* def =
                        species_registry_.get_species(cs->species_id);
                    if (def) name = def->species_key.c_str();
                }
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, name,
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }
        group.predator_ids.resize(desired_pred);
    }
}

// --- Proxy creature AI ---

void EcosystemSystem::tick_proxies(
    const ChunkKey& chunk, int64_t tick, float delta) {
    if (!world_) return;

    auto pit = active_proxies_.find(chunk);
    if (pit == active_proxies_.end()) return;

    ProxyGroup& group = pit->second;
    auto& registry = world_->block_entity_registry();

    // Convert delta to tick-scaled movement time.
    const float move_dt = delta * TickSystem::kTicksPerSecond;
    const float global_speed = params_.creature_move_speed;

    // Helper to emit creature_moved event after a successful move.
    auto emit_moved = [&](EntityId id, const CreatureBlockEntityState& cs) {
        if (!event_bus_) return;
        const CreatureSpeciesDef* def =
            species_registry_.get_species(cs.species_id);
        const char* key = def ? def->species_key.c_str() : "unknown";
        event_bus_->emit(GameEvent::creature_moved(
            id.id, key, cs.pos_x, cs.pos_y, cs.pos_z));
    };

    // Helper to resolve effective speed for a creature.
    // Uses species-specific speed if defined, otherwise global default.
    auto resolve_speed = [&](const CreatureBlockEntityState& cs) -> float {
        const CreatureSpeciesDef* def =
            species_registry_.get_species(cs.species_id);
        if (def && def->move_speed > 0.0f) return def->move_speed;
        return global_speed;
    };

    // Helper to resolve effective flee detection radius.
    auto resolve_flee_radius = [&](const CreatureBlockEntityState& cs) -> float {
        const CreatureSpeciesDef* def =
            species_registry_.get_species(cs.species_id);
        if (def && def->flee_detection_radius > 0.0f)
            return def->flee_detection_radius;
        return params_.flee_detection_radius;
    };

    // Helper to resolve effective wander radius.
    auto resolve_wander_radius = [&](const CreatureBlockEntityState& cs) -> float {
        const CreatureSpeciesDef* def =
            species_registry_.get_species(cs.species_id);
        if (def && def->wander_radius > 0.0f) return def->wander_radius;
        return params_.wander_radius;
    };

    // --- Update herbivores ---
    for (EntityId id : group.herbivore_ids) {
        CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
        if (!cs) continue;

        const float speed = resolve_speed(*cs);

        // Check for nearby predators (flee behavior).
        if (check_flee_for_herbivore(*cs, group)) {
            cs->ai_state = CreatureState::FLEEING;
            if (cs->flee_end_tick <= tick) {
                cs->flee_end_tick = tick + params_.flee_duration_ticks;
            }
        }

        // State machine.
        switch (cs->ai_state) {
            case CreatureState::IDLE: {
                if (tick >= cs->next_wander_tick) {
                    pick_wander_target(*cs);
                    cs->ai_state = CreatureState::WANDERING;
                }
                break;
            }
            case CreatureState::WANDERING: {
                if (move_creature_toward_target(
                        *cs, speed, move_dt)) {
                    emit_moved(id, *cs);
                }

                // Check if reached target.
                float dx = cs->wander_target_x - cs->pos_x;
                float dy = cs->wander_target_y - cs->pos_y;
                float dz = cs->wander_target_z - cs->pos_z;
                float dist_sq = dx * dx + dy * dy + dz * dz;
                if (dist_sq < 1.0f) {
                    cs->ai_state = CreatureState::IDLE;
                    cs->next_wander_tick = tick + params_.wander_interval_ticks;
                }
                break;
            }
            case CreatureState::FLEEING: {
                if (tick >= cs->flee_end_tick) {
                    cs->ai_state = CreatureState::IDLE;
                    cs->next_wander_tick = tick + params_.wander_interval_ticks / 2;
                    break;
                }

                // Flee: move away from flee_from position.
                float fx = cs->pos_x - cs->flee_from_x;
                float fy = cs->pos_y - cs->flee_from_y;
                float fz = cs->pos_z - cs->flee_from_z;
                float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
                if (flen > 0.001f) {
                    fx /= flen;
                    fy /= flen;
                    fz /= flen;
                } else {
                    fx = 1.0f;
                    fy = 0.0f;
                    fz = 0.0f;
                }

                const float wander_r = resolve_wander_radius(*cs);
                cs->wander_target_x = cs->pos_x + fx * wander_r;
                cs->wander_target_y = cs->pos_y + fy * wander_r;
                cs->wander_target_z = cs->pos_z + fz * wander_r;

                float flee_speed = speed * params_.flee_speed_multiplier;
                if (move_creature_toward_target(*cs, flee_speed, move_dt)) {
                    emit_moved(id, *cs);
                }
                break;
            }
            default:
                break;
        }
    }

    // --- Update predators ---
    for (EntityId id : group.predator_ids) {
        CreatureBlockEntityState* cs = registry.get_creature_state_mut(id);
        if (!cs) continue;

        const float speed = resolve_speed(*cs);

        // Predators don't flee; they just wander.
        switch (cs->ai_state) {
            case CreatureState::IDLE: {
                if (tick >= cs->next_wander_tick) {
                    pick_wander_target(*cs);
                    cs->ai_state = CreatureState::WANDERING;
                }
                break;
            }
            case CreatureState::WANDERING: {
                if (move_creature_toward_target(
                        *cs, speed, move_dt)) {
                    emit_moved(id, *cs);
                }

                float dx = cs->wander_target_x - cs->pos_x;
                float dy = cs->wander_target_y - cs->pos_y;
                float dz = cs->wander_target_z - cs->pos_z;
                float dist_sq = dx * dx + dy * dy + dz * dz;
                if (dist_sq < 1.0f) {
                    cs->ai_state = CreatureState::IDLE;
                    cs->next_wander_tick = tick + params_.wander_interval_ticks;
                }
                break;
            }
            case CreatureState::FLEEING: {
                // Predators don't flee; reset to idle.
                cs->ai_state = CreatureState::IDLE;
                cs->next_wander_tick = tick + params_.wander_interval_ticks / 2;
                break;
            }
            default:
                break;
        }
    }
}

void EcosystemSystem::pick_wander_target(
    CreatureBlockEntityState& creature) const {
    // Use a simple hash for pseudo-random direction.
    const int64_t tick = world_ ? world_->current_tick() : 0;
    const int64_t seed = static_cast<int64_t>(creature.pos_x * 100.0f) * 73856093 +
                         static_cast<int64_t>(creature.pos_z * 100.0f) * 83492791 +
                         tick * 19349663;

    // Resolve species-specific wander radius.
    float wander_r = params_.wander_radius;
    const CreatureSpeciesDef* def =
        species_registry_.get_species(creature.species_id);
    if (def && def->wander_radius > 0.0f) wander_r = def->wander_radius;

    // Map hash to [-1, 1] range for each axis.
    float rx = static_cast<float>((seed & 0xFFFF) % 2001 - 1000) / 1000.0f;
    float ry = static_cast<float>(((seed >> 16) & 0xFF) % 2001 - 1000) / 1000.0f;
    float rz = static_cast<float>(((seed >> 24) & 0xFF) % 2001 - 1000) / 1000.0f;

    creature.wander_target_x = creature.pos_x + rx * wander_r;
    creature.wander_target_y = creature.pos_y + ry * 2.0f;
    creature.wander_target_z = creature.pos_z + rz * wander_r;
}

bool EcosystemSystem::check_flee_for_herbivore(
    CreatureBlockEntityState& herbivore,
    const ProxyGroup& group) const {
    if (!world_) return false;
    auto& registry = world_->block_entity_registry();

    // Resolve species-specific flee detection radius.
    float radius = params_.flee_detection_radius;
    const CreatureSpeciesDef* def =
        species_registry_.get_species(herbivore.species_id);
    if (def && def->flee_detection_radius > 0.0f)
        radius = def->flee_detection_radius;

    const float radius_sq = radius * radius;

    for (EntityId pred_id : group.predator_ids) {
        const CreatureBlockEntityState* pred =
            registry.get_creature_state(pred_id);
        if (!pred) continue;

        float dx = herbivore.pos_x - pred->pos_x;
        float dy = herbivore.pos_y - pred->pos_y;
        float dz = herbivore.pos_z - pred->pos_z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        if (dist_sq < radius_sq) {
            herbivore.flee_from_x = pred->pos_x;
            herbivore.flee_from_y = pred->pos_y;
            herbivore.flee_from_z = pred->pos_z;
            return true;
        }
    }

    return false;
}

bool EcosystemSystem::move_creature_toward_target(
    CreatureBlockEntityState& creature,
    float speed, float dt) const {
    float dx = creature.wander_target_x - creature.pos_x;
    float dy = creature.wander_target_y - creature.pos_y;
    float dz = creature.wander_target_z - creature.pos_z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (dist < 0.01f) return false;

    // Normalize direction.
    float nx = dx / dist;
    float ny = dy / dist;
    float nz = dz / dist;

    // Move by speed * dt, but don't overshoot.
    float step = speed * dt;
    if (step > dist) step = dist;

    creature.pos_x += nx * step;
    creature.pos_y += ny * step;
    creature.pos_z += nz * step;
    return true;
}

// --- Player stewardship (feeding) ---

bool EcosystemSystem::feed_creatures(
    const ChunkKey& key, CreatureRole role, float amount) {
    if (amount <= 0.0f) return false;

    PopulationCell* cell = get_population_cell(key);
    if (!cell) return false;

    if (role == CreatureRole::HERBIVORE) {
        cell->herbivore_density = std::clamp(
            cell->herbivore_density + amount, 0.0f, 1.0f);
    } else {
        cell->predator_density = std::clamp(
            cell->predator_density + amount, 0.0f, 1.0f);
    }

    // Trigger proxy rebalance so new creatures appear immediately.
    if (world_) {
        const int64_t tick = world_->current_tick();
        rebalance_proxies(key, tick);
    }

    return true;
}

// ============================================================
// Player combat (hunting) — attack and kill proxy creatures
// ============================================================

EcosystemSystem::AttackResult EcosystemSystem::attack_creature(
    const std::string& dimension,
    float player_x, float player_y, float player_z,
    float look_dir_x, float look_dir_y, float look_dir_z,
    float reach, float damage, int64_t tick) {
    AttackResult result;

    if (!world_) return result;

    // Normalize look direction.
    float look_len = std::sqrt(look_dir_x * look_dir_x
        + look_dir_y * look_dir_y + look_dir_z * look_dir_z);
    if (look_len < 0.001f) return result;
    look_dir_x /= look_len;
    look_dir_y /= look_len;
    look_dir_z /= look_len;

    const float reach_sq = reach * reach;

    // Compute the player's chunk key.
    ChunkKey player_chunk = chunk_key_for_position(
        dimension, player_x, player_y, player_z);

    // Search all active proxy groups in the player's dimension.
    // We check the player's chunk and immediate neighbors to handle
    // creatures near chunk boundaries.
    EntityId best_id;
    float best_dot = -2.0f;
    float best_dist_sq = reach_sq + 1.0f;
    ChunkKey best_chunk;

    for (const auto& [chunk_key, group] : active_proxies_) {
        if (chunk_key.dimension_id != dimension) continue;

        // Quick reject: only check chunks within reach distance.
        // A chunk is at most kChunkSize blocks across, so we check
        // if the chunk center is within reach + kChunkSize*sqrt(3).
        const float cx_center = (chunk_key.chunk_x + 0.5f)
            * ChunkData::kChunkSize;
        const float cy_center = (chunk_key.chunk_y + 0.5f)
            * ChunkData::kChunkSize;
        const float cz_center = (chunk_key.chunk_z + 0.5f)
            * ChunkData::kChunkSize;
        float cdx = cx_center - player_x;
        float cdy = cy_center - player_y;
        float cdz = cz_center - player_z;
        float cdist_sq = cdx * cdx + cdy * cdy + cdz * cdz;
        float max_chunk_dist = reach + ChunkData::kChunkSize * 1.74f;
        if (cdist_sq > max_chunk_dist * max_chunk_dist) continue;

        auto& registry = world_->block_entity_registry();

        // Check all herbivore proxies in this chunk.
        for (EntityId id : group.herbivore_ids) {
            const CreatureBlockEntityState* cs =
                registry.get_creature_state(id);
            if (!cs) continue;

            float dx = cs->pos_x - player_x;
            float dy = cs->pos_y - player_y;
            float dz = cs->pos_z - player_z;
            float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq > reach_sq) continue;

            // View cone check: dot product of look direction and
            // direction to creature. Allow a wide cone (~60 degrees).
            float inv_dist = 1.0f / std::sqrt(dist_sq);
            float dot = look_dir_x * dx * inv_dist
                      + look_dir_y * dy * inv_dist
                      + look_dir_z * dz * inv_dist;
            if (dot < 0.5f) continue;

            // Pick the closest creature in the center of view.
            if (dot > best_dot || (dot == best_dot && dist_sq < best_dist_sq)) {
                best_id = id;
                best_dot = dot;
                best_dist_sq = dist_sq;
                best_chunk = chunk_key;
            }
        }

        // Check all predator proxies in this chunk.
        for (EntityId id : group.predator_ids) {
            const CreatureBlockEntityState* cs =
                registry.get_creature_state(id);
            if (!cs) continue;

            float dx = cs->pos_x - player_x;
            float dy = cs->pos_y - player_y;
            float dz = cs->pos_z - player_z;
            float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq > reach_sq) continue;

            float inv_dist = 1.0f / std::sqrt(dist_sq);
            float dot = look_dir_x * dx * inv_dist
                      + look_dir_y * dy * inv_dist
                      + look_dir_z * dz * inv_dist;
            if (dot < 0.5f) continue;

            if (dot > best_dot || (dot == best_dot && dist_sq < best_dist_sq)) {
                best_id = id;
                best_dot = dot;
                best_dist_sq = dist_sq;
                best_chunk = chunk_key;
            }
        }
    }

    if (best_dot < -1.0f) return result;

    // Found a target — apply damage.
    result = apply_damage_to_creature(best_id, damage, tick);
    result.chunk_key = best_chunk;
    return result;
}

EcosystemSystem::AttackResult EcosystemSystem::apply_damage_to_creature(
    EntityId creature_id, float damage, int64_t tick) {
    AttackResult result;

    if (!world_) return result;

    auto& registry = world_->block_entity_registry();
    CreatureBlockEntityState* cs = registry.get_creature_state_mut(creature_id);
    if (!cs) return result;

    result.hit = true;
    result.creature_id = creature_id.id;
    result.species_id = cs->species_id;

    // Clamp damage to remaining health.
    float actual_damage = std::min(damage, cs->health);
    cs->health -= actual_damage;
    result.damage_dealt = actual_damage;
    result.remaining_health = cs->health;

    // Find the chunk key for this creature by searching active_proxies_.
    ChunkKey chunk;
    for (const auto& [ck, group] : active_proxies_) {
        bool found = false;
        for (EntityId id : group.herbivore_ids) {
            if (id.id == creature_id.id) { chunk = ck; found = true; break; }
        }
        if (found) break;
        for (EntityId id : group.predator_ids) {
            if (id.id == creature_id.id) { chunk = ck; found = true; break; }
        }
        if (found) break;
    }

    result.chunk_key = chunk;

    // Emit creature_damaged event.
    if (event_bus_) {
        event_bus_->emit(GameEvent::creature_damaged(
            creature_id.id, cs->species_id,
            actual_damage, cs->health,
            chunk.dimension_id,
            chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
    }

    // Check if creature is killed.
    if (cs->health <= 0.0f) {
        result.killed = true;
        kill_proxy_creature(creature_id, chunk,
            cs->creature_role, cs->species_id, tick);
    }

    return result;
}

void EcosystemSystem::kill_proxy_creature(
    EntityId creature_id, const ChunkKey& chunk,
    CreatureRole role, uint16_t species_id, int64_t tick) {
    if (!world_) return;

    auto& registry = world_->block_entity_registry();

    // Add hunting pressure to the population cell.
    PopulationCell* cell = get_population_cell(chunk);
    if (cell) {
        if (role == CreatureRole::HERBIVORE) {
            cell->hunting_pressure_herb += params_.hunting_kill_contribution;
        } else {
            cell->hunting_pressure_pred += params_.hunting_kill_contribution;
        }
    }

    // Remove from proxy group.
    auto it = active_proxies_.find(chunk);
    if (it != active_proxies_.end()) {
        ProxyGroup& group = it->second;
        if (role == CreatureRole::HERBIVORE) {
            group.herbivore_ids.erase(
                std::remove(group.herbivore_ids.begin(),
                            group.herbivore_ids.end(),
                            creature_id),
                group.herbivore_ids.end());
        } else {
            group.predator_ids.erase(
                std::remove(group.predator_ids.begin(),
                            group.predator_ids.end(),
                            creature_id),
                group.predator_ids.end());
        }
    }

    // Emit creature_killed event (before removing entity so
    // listeners can still query the entity).
    if (event_bus_) {
        event_bus_->emit(GameEvent::creature_killed(
            creature_id.id, species_id,
            chunk.dimension_id,
            chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
    }

    // Remove the entity from the registry.
    registry.remove_entity(creature_id);
}

ChunkKey EcosystemSystem::chunk_key_for_position(
    const std::string& dimension,
    float world_x, float world_y, float world_z) const {
    // Floor to get block coordinates, then integer-divide by chunk size.
    int bx = static_cast<int>(std::floor(world_x));
    int by = static_cast<int>(std::floor(world_y));
    int bz = static_cast<int>(std::floor(world_z));
    int cx = (bx >= 0 ? bx : bx - ChunkData::kChunkSize + 1)
           / ChunkData::kChunkSize;
    int cy = (by >= 0 ? by : by - ChunkData::kChunkSize + 1)
           / ChunkData::kChunkSize;
    int cz = (bz >= 0 ? bz : bz - ChunkData::kChunkSize + 1)
           / ChunkData::kChunkSize;
    return ChunkKey(dimension, cx, cy, cz);
}

// --- Biome inference and default overrides ---

uint8_t EcosystemSystem::infer_biome_type(const ChunkKey& key) const {
    if (!world_) return ecosystem_biome::kPlains;

    const ChunkData* chunk = world_->get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    if (!chunk) return ecosystem_biome::kPlains;

    // Resolve material role IDs from worldgen config.
    auto config = world_->worldgen_config();
    TerrainMaterialId sand_id = 0;
    TerrainMaterialId stone_id = 0;
    TerrainMaterialId water_id = 0;
    TerrainMaterialId dirt_id = 0;
    if (config) {
        sand_id = config->roles.sand;
        stone_id = config->roles.stone;
        water_id = config->roles.water;
        dirt_id = config->roles.dirt;
    }

    // Count surface-level exposed cells by material type.
    // Only scan the top quarter of the chunk (y >= 3/4 of size_y)
    // to capture surface biome rather than underground.
    const int size_x = chunk->terrain.size_x;
    const int size_y = chunk->terrain.size_y;
    const int size_z = chunk->terrain.size_z;
    const int surface_y_start = size_y * 3 / 4;

    int sand_count = 0;
    int stone_count = 0;
    int water_count = 0;
    int dirt_count = 0;
    int total_counted = 0;

    for (int y = surface_y_start; y < size_y; ++y) {
        for (int z = 0; z < size_z; ++z) {
            for (int x = 0; x < size_x; ++x) {
                TerrainMaterialId mat = chunk->terrain.cell_at(x, y, z).material;
                if (mat == 0) continue;  // Skip air.
                ++total_counted;
                if (mat == sand_id) ++sand_count;
                else if (mat == stone_id) ++stone_count;
                else if (mat == water_id) ++water_count;
                else if (mat == dirt_id) ++dirt_count;
            }
        }
    }

    if (total_counted == 0) return ecosystem_biome::kPlains;

    // Classify by dominant surface material.
    const float threshold = 0.4f;
    if (static_cast<float>(water_count) / total_counted >= threshold) {
        return ecosystem_biome::kOcean;
    }
    if (static_cast<float>(sand_count) / total_counted >= threshold) {
        return ecosystem_biome::kDesert;
    }
    if (static_cast<float>(stone_count) / total_counted >= threshold) {
        return ecosystem_biome::kRocky;
    }

    return ecosystem_biome::kPlains;
}

void EcosystemSystem::register_default_biome_overrides() {
    using namespace ecosystem_biome;
    using namespace creature_species;

    // Plains (0): temperate, balanced ecosystem.
    {
        EcosystemParams::BiomeOverride& bo =
            params_.biome_overrides[params_.biome_override_count++];
        bo.biome_type = kPlains;
        bo.base_water = 0.5f;
        bo.base_fertility = 0.5f;
        bo.veg_growth_multiplier = 1.0f;
        bo.max_vegetation = 1.0f;
        bo.max_herbivore = 1.0f;
        bo.max_predator = 1.0f;
        bo.herb_species_ids = {kGlowDeer, kRockLizard};
        bo.pred_species_ids = {kThunderbird, kBlazeBeast};
    }

    // Desert (1): low water, sparse vegetation, heat-adapted species.
    {
        EcosystemParams::BiomeOverride& bo =
            params_.biome_overrides[params_.biome_override_count++];
        bo.biome_type = kDesert;
        bo.base_water = 0.1f;
        bo.base_fertility = 0.2f;
        bo.veg_growth_multiplier = 0.4f;
        bo.max_vegetation = 0.4f;
        bo.max_herbivore = 0.5f;
        bo.max_predator = 0.4f;
        bo.herb_species_ids = {kRockLizard};
        bo.pred_species_ids = {kThunderbird};
    }

    // Rocky (2): low fertility, hardy species only.
    {
        EcosystemParams::BiomeOverride& bo =
            params_.biome_overrides[params_.biome_override_count++];
        bo.biome_type = kRocky;
        bo.base_water = 0.3f;
        bo.base_fertility = 0.3f;
        bo.veg_growth_multiplier = 0.5f;
        bo.max_vegetation = 0.5f;
        bo.max_herbivore = 0.4f;
        bo.max_predator = 0.5f;
        bo.herb_species_ids = {kRockLizard};
        bo.pred_species_ids = {kThunderbird, kBlazeBeast};
    }

    // Ocean (3): water-dominated, aquatic species only.
    {
        EcosystemParams::BiomeOverride& bo =
            params_.biome_overrides[params_.biome_override_count++];
        bo.biome_type = kOcean;
        bo.base_water = 1.0f;
        bo.base_fertility = 0.1f;
        bo.veg_growth_multiplier = 0.2f;
        bo.max_vegetation = 0.2f;
        bo.max_herbivore = 0.1f;
        bo.max_predator = 0.6f;
        bo.herb_species_ids = {};
        bo.pred_species_ids = {kSeaSerpent};
    }

    // Barren (4): toxic/corrosive, no natural fauna.
    {
        EcosystemParams::BiomeOverride& bo =
            params_.biome_overrides[params_.biome_override_count++];
        bo.biome_type = kBarren;
        bo.base_water = 0.05f;
        bo.base_fertility = 0.05f;
        bo.veg_growth_multiplier = 0.1f;
        bo.max_vegetation = 0.1f;
        bo.max_herbivore = 0.0f;
        bo.max_predator = 0.0f;
        bo.herb_species_ids = {};
        bo.pred_species_ids = {};
    }
}

} // namespace science_and_theology
