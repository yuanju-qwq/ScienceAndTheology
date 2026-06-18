#include "ecosystem_system.hpp"
#include "tick_system.hpp"
#include "day_night_def.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void EcosystemSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void EcosystemSystem::tick_active(const ChunkKey& chunk, float delta) {
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
    } else if (params_.proxy_rebalance_interval_ticks > 0 &&
               tick % params_.proxy_rebalance_interval_ticks == 0) {
        rebalance_proxies(chunk, tick);
    }

    // Update proxy creature AI (wandering, fleeing).
    tick_proxies(chunk, tick, delta);

    // Run diffusion at the configured interval.
    if (params_.diffusion_interval_ticks > 0 &&
        tick % params_.diffusion_interval_ticks == 0) {
        diffuse_populations();
        ++diffusion_count_;
    }
}

void EcosystemSystem::tick_sleeping(const ChunkKey& chunk, float delta) {
    if (!world_) return;

    // Check if ecosystem is enabled for this dimension.
    const GameplayConfig& gc = world_->gameplay_config();
    if (!gc.is_ecosystem_enabled(chunk.dimension_id)) return;

    // Despawn proxy creatures when chunk transitions to sleeping.
    // This is safe to call every sleeping tick because despawn_proxies
    // is idempotent (no-ops if no proxies exist).
    despawn_proxies_for_chunk(chunk);

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

    // Create a new population cell with defaults.
    PopulationCell cell;

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
    // dH/dt = reproduction - natural_death - predation
    // reproduction = herb_repro_rate * V * H * season_mod * day_mod
    const float herb_repro = rp.herb_repro_rate
        * cell.vegetation_density * cell.herbivore_density
        * season_herb_mod * day_herb_mod;
    const float herb_death = rp.herb_natural_death * cell.herbivore_density;
    const float predation = rp.predation_rate
        * cell.predator_density * cell.herbivore_density * day_pred_mod;
    cell.herbivore_density += (herb_repro - herb_death - predation) * dt;
    cell.herbivore_density = std::clamp(cell.herbivore_density,
        0.0f, rp.max_herbivore);

    // --- Predator dynamics ---
    // dP/dt = reproduction - natural_death
    // reproduction = pred_repro_rate * H * P * season_mod * day_mod
    const float pred_repro = rp.pred_repro_rate
        * cell.herbivore_density * cell.predator_density
        * season_pred_mod * day_pred_mod;
    const float pred_death = rp.pred_natural_death * cell.predator_density;
    cell.predator_density += (pred_repro - pred_death) * dt;
    cell.predator_density = std::clamp(cell.predator_density,
        0.0f, rp.max_predator);

    // --- Nutrient cycling ---
    // Dead biomass from animal deaths and vegetation decay.
    const float animal_death_biomass = (herb_death + pred_death)
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
        // Offset each creature by a small amount to avoid stacking.
        sx += i * 3;
        EntityId id = registry.register_creature_entity(
            chunk.dimension_id, sx, sy, sz,
            CreatureType::HERBIVORE, tick);
        group.herbivore_ids.push_back(id);

        if (event_bus_) {
            event_bus_->emit(GameEvent::creature_spawned(
                id.id, "herbivore",
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
        EntityId id = registry.register_creature_entity(
            chunk.dimension_id, sx, sy, sz,
            CreatureType::PREDATOR, tick);
        group.predator_ids.push_back(id);

        if (event_bus_) {
            event_bus_->emit(GameEvent::creature_spawned(
                id.id, "predator",
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
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, "herbivore",
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }

        for (EntityId id : it->second.predator_ids) {
            if (event_bus_) {
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, "predator",
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
        // No proxies yet — spawn fresh.
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
        // Spawn additional herbivores.
        for (int i = 0; i < desired_herb - current_herb; ++i) {
            int32_t sx, sy, sz;
            random_spawn_position_in_chunk(chunk, sx, sy, sz);
            sx += (current_herb + i) * 3;
            EntityId id = registry.register_creature_entity(
                chunk.dimension_id, sx, sy, sz,
                CreatureType::HERBIVORE, tick);
            group.herbivore_ids.push_back(id);

            if (event_bus_) {
                event_bus_->emit(GameEvent::creature_spawned(
                    id.id, "herbivore",
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
        }
    } else if (desired_herb < current_herb) {
        // Remove excess herbivores (remove from the end).
        for (int i = desired_herb; i < current_herb; ++i) {
            EntityId id = group.herbivore_ids[i];
            if (event_bus_) {
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, "herbivore",
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
            EntityId id = registry.register_creature_entity(
                chunk.dimension_id, sx, sy, sz,
                CreatureType::PREDATOR, tick);
            group.predator_ids.push_back(id);

            if (event_bus_) {
                event_bus_->emit(GameEvent::creature_spawned(
                    id.id, "predator",
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
        }
    } else if (desired_pred < current_pred) {
        for (int i = desired_pred; i < current_pred; ++i) {
            EntityId id = group.predator_ids[i];
            if (event_bus_) {
                event_bus_->emit(GameEvent::creature_despawned(
                    id.id, "predator",
                    chunk.dimension_id,
                    chunk.chunk_x, chunk.chunk_y, chunk.chunk_z));
            }
            registry.remove_entity(id);
        }
        group.predator_ids.resize(desired_pred);
    }
}

} // namespace science_and_theology
