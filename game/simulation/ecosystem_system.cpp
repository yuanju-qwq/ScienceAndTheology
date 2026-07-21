// Deterministic game-owned ecosystem simulation implementation.

#include "game/simulation/ecosystem_system.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool finite_unit(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

[[nodiscard]] float nonnegative_rate_multiplier(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f ? value : 1.0f;
}

[[nodiscard]] bool chunk_key_less(const ChunkKey& left, const ChunkKey& right) noexcept {
    if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
    if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
    if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
    return left.chunk_z < right.chunk_z;
}

struct PopulationDelta {
    float vegetation = 0.0f;
    float herbivore = 0.0f;
    float predator = 0.0f;
};

[[nodiscard]] int season_index(Season season) noexcept {
    const int value = static_cast<int>(season);
    return value >= 0 && value < 4 ? value : 0;
}

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_uint32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t byte_index = 0; byte_index < 4; ++byte_index) {
        hash_byte(hash, static_cast<uint8_t>(value >> (byte_index * 8)));
    }
}

void hash_string(uint64_t& hash, const std::string& value) noexcept {
    for (const unsigned char character : value) hash_byte(hash, character);
    hash_byte(hash, 0);
}

[[nodiscard]] uint64_t stable_wild_proxy_id(
    const ChunkKey& chunk, uint8_t biome_type, CreatureRole role, uint32_t slot) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash_string(hash, chunk.dimension_id);
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_x));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_y));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_z));
    hash_byte(hash, biome_type);
    hash_byte(hash, static_cast<uint8_t>(role));
    hash_uint32(hash, slot);
    return hash == 0 ? 1 : hash;
}

}  // namespace

const GameEcosystemBiomeRules& GameEcosystemConfig::rules_for(uint8_t biome_type) const noexcept {
    const size_t index = biome_type < biome_rules.size()
        ? static_cast<size_t>(biome_type)
        : static_cast<size_t>(ecosystem_biome::kPlains);
    return biome_rules[index];
}

GameEcosystemSystem::GameEcosystemSystem(
    ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
    const WorldGenConfigSnapshot& worldgen_config, GameEcosystemConfig config) noexcept
    : chunks_(&chunks), sidecars_(&sidecars), worldgen_config_(&worldgen_config),
      config_(std::move(config)), species_catalog_(&builtin_creature_species()) {}

snt::core::Expected<PopulationCell*> GameEcosystemSystem::ensure_population_cell(
    const ChunkKey& chunk, uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Ecosystem system is not initialized");
    }
    if (chunks_->get_chunk(chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z) == nullptr) {
        return invalid_state("Ecosystem population chunk is not loaded");
    }
    GameChunkSidecar* sidecar = sidecars_->get(chunk);
    if (sidecar == nullptr) {
        sidecars_->set(chunk, {});
        sidecar = sidecars_->get(chunk);
    }
    if (sidecar == nullptr) return invalid_state("Ecosystem population sidecar is unavailable");
    if (sidecar->has_population_cell) return &sidecar->population_cell;

    const PopulationCell previous = sidecar->population_cell;
    initialize_population(sidecar->population_cell, infer_biome(chunk), source_tick);
    sidecar->has_population_cell = true;
    emit_population_mutation(GameEcosystemPopulationMutationKind::kInitialized, chunk,
                             source_tick, previous, sidecar->population_cell);
    return &sidecar->population_cell;
}

PopulationCell* GameEcosystemSystem::find_population_cell(const ChunkKey& chunk) noexcept {
    GameChunkSidecar* const sidecar = sidecars_ == nullptr ? nullptr : sidecars_->get(chunk);
    return sidecar != nullptr && sidecar->has_population_cell ? &sidecar->population_cell : nullptr;
}

const PopulationCell* GameEcosystemSystem::find_population_cell(const ChunkKey& chunk) const noexcept {
    const GameChunkSidecar* const sidecar = sidecars_ == nullptr ? nullptr : sidecars_->get(chunk);
    return sidecar != nullptr && sidecar->has_population_cell ? &sidecar->population_cell : nullptr;
}

size_t GameEcosystemSystem::population_cell_count() const noexcept {
    if (sidecars_ == nullptr) return 0;
    size_t count = 0;
    sidecars_->for_each([&count](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (sidecar.has_population_cell) ++count;
    });
    return count;
}

snt::core::Expected<void> GameEcosystemSystem::record_hunt(
    const ChunkKey& chunk, CreatureRole role, float contribution, uint64_t source_tick) {
    if (role != CreatureRole::HERBIVORE && role != CreatureRole::PREDATOR) {
        return invalid_state("Ecosystem hunt role is invalid");
    }
    PopulationCell* cell = nullptr;
    if (auto ensured = ensure_population_cell(chunk, source_tick); !ensured) return ensured.error();
    else cell = *ensured;
    const float resolved_contribution = contribution > 0.0f
        ? contribution
        : config_.hunting_kill_contribution;
    if (!std::isfinite(resolved_contribution) || resolved_contribution <= 0.0f) {
        return invalid_state("Ecosystem hunt contribution is invalid");
    }
    const PopulationCell previous = *cell;
    if (role == CreatureRole::HERBIVORE) {
        cell->hunting_pressure_herb += resolved_contribution;
    } else {
        cell->hunting_pressure_pred += resolved_contribution;
    }
    emit_population_mutation(GameEcosystemPopulationMutationKind::kHuntRecorded, chunk,
                             source_tick, previous, *cell);
    return {};
}

snt::core::Expected<void> GameEcosystemSystem::record_wild_capture(
    const ChunkKey& chunk, CreatureRole role, float contribution, uint64_t source_tick) {
    auto ensured = ensure_population_cell(chunk, source_tick);
    if (!ensured) return ensured.error();
    PopulationCell& cell = **ensured;
    const PopulationCell previous = cell;
    const float resolved_contribution = contribution > 0.0f && std::isfinite(contribution)
        ? contribution
        : config_.wild_capture_density_contribution;
    if (!std::isfinite(resolved_contribution) || resolved_contribution <= 0.0f) {
        return invalid_state("Wild capture contribution must be positive and finite");
    }
    if (role == CreatureRole::HERBIVORE) {
        cell.herbivore_density = std::max(0.0f, cell.herbivore_density - resolved_contribution);
    } else if (role == CreatureRole::PREDATOR) {
        cell.predator_density = std::max(0.0f, cell.predator_density - resolved_contribution);
    } else {
        return invalid_state("Wild capture creature role is invalid");
    }
    emit_population_mutation(GameEcosystemPopulationMutationKind::kCaptureRecorded, chunk,
                             source_tick, previous, cell);
    return {};
}

void GameEcosystemSystem::tick(uint64_t current_tick, Season current_season) {
    if (!config_.enabled || chunks_ == nullptr || sidecars_ == nullptr ||
        worldgen_config_ == nullptr) {
        return;
    }

    std::vector<GameEcosystemInterestCenter> interest_centers;
    if (interest_provider_ != nullptr) {
        interest_provider_->collect_ecosystem_interest_centers(
            current_tick, interest_centers);
    }
    std::sort(interest_centers.begin(), interest_centers.end(),
              [](const GameEcosystemInterestCenter& left,
                 const GameEcosystemInterestCenter& right) {
                  return chunk_key_less(left.chunk, right.chunk);
              });
    interest_centers.erase(
        std::unique(interest_centers.begin(), interest_centers.end(),
                    [](const GameEcosystemInterestCenter& left,
                       const GameEcosystemInterestCenter& right) {
                        return left.chunk == right.chunk;
                    }),
        interest_centers.end());

    std::vector<ChunkKey> loaded_active_chunks;
    for (const ChunkKey& chunk : chunks_->all_chunk_keys()) {
        if (is_active_loaded_chunk(chunk)) loaded_active_chunks.push_back(chunk);
    }
    std::sort(loaded_active_chunks.begin(), loaded_active_chunks.end(), chunk_key_less);

    std::vector<ChunkKey> active_chunks;
    active_chunks.reserve(loaded_active_chunks.size());
    std::unordered_set<ChunkKey> current_interactive_chunks;
    std::unordered_set<ChunkKey> current_far_visual_chunks;

    for (const ChunkKey& chunk : loaded_active_chunks) {
        const ChunkActivity activity = activity_for_chunk(chunk, interest_centers);
        if (activity == ChunkActivity::kInactive) continue;

        GameEcosystemEnvironmentSample environment;
        if (environment_provider_ != nullptr) {
            static_cast<void>(environment_provider_->sample_ecosystem_environment(chunk, environment));
        }
        if (!environment.enabled) continue;
        active_chunks.push_back(chunk);
        const bool was_initialized = find_population_cell(chunk) != nullptr;
        auto ensured = ensure_population_cell(chunk, current_tick);
        if (!ensured) continue;
        PopulationCell& cell = **ensured;
        const PopulationCell previous = cell;
        if (environment.biome_type.has_value()) {
            cell.biome_type = *environment.biome_type < ecosystem_biome::kCount
                ? *environment.biome_type
                : ecosystem_biome::kBarren;
        }
        if (environment.soil_fertility.has_value() && finite_unit(*environment.soil_fertility)) {
            cell.soil_fertility = *environment.soil_fertility;
        }

        uint64_t elapsed_ticks = 1;
        if (was_initialized && cell.last_macro_simulation_tick != 0) {
            elapsed_ticks = current_tick > cell.last_macro_simulation_tick
                ? current_tick - cell.last_macro_simulation_tick
                : 0;
        }
        if (elapsed_ticks != 0) {
            advance_population(cell, current_season, environment, elapsed_ticks);
        }
        cell.last_macro_simulation_tick = current_tick;
        if (cell.vegetation_density != previous.vegetation_density ||
            cell.herbivore_density != previous.herbivore_density ||
            cell.predator_density != previous.predator_density ||
            cell.soil_fertility != previous.soil_fertility ||
            cell.water_availability != previous.water_availability ||
            cell.dead_biomass != previous.dead_biomass ||
            cell.hunting_pressure_herb != previous.hunting_pressure_herb ||
            cell.hunting_pressure_pred != previous.hunting_pressure_pred ||
            cell.biome_type != previous.biome_type) {
            const GameEcosystemPopulationMutationKind kind =
                was_initialized && elapsed_ticks > 1
                    ? GameEcosystemPopulationMutationKind::kMacroCatchUp
                    : GameEcosystemPopulationMutationKind::kAdvanced;
            emit_population_mutation(kind, chunk, current_tick, previous, cell);
        }
        if (activity == ChunkActivity::kInteractive) {
            const bool newly_interactive = !interactive_proxy_chunks_.contains(chunk);
            current_interactive_chunks.insert(chunk);
            if (wild_proxy_sink_ != nullptr &&
                (newly_interactive || force_wild_proxy_rebalance_ ||
                 config_.wild_proxy_rebalance_interval_ticks == 0 ||
                 current_tick % config_.wild_proxy_rebalance_interval_ticks == 0)) {
                wild_proxy_sink_->request_wild_proxy_rebalance(
                    make_wild_proxy_rebalance_request(chunk, current_tick, cell));
            }
        }
        if (activity == ChunkActivity::kVisual || activity == ChunkActivity::kInteractive) {
            current_far_visual_chunks.insert(chunk);
        }
        GameChunkSidecar* const sidecar = sidecars_->get(chunk);
        if (captive_lifecycle_sink_ != nullptr && sidecar != nullptr &&
            sidecar->has_captive_creatures) {
            captive_lifecycle_sink_->tick_captive_creatures({
                .chunk = chunk,
                .source_tick = current_tick,
                .creatures = sidecar->captive_creatures,
            });
        }
    }
    if (config_.diffusion_interval_ticks != 0 &&
        current_tick % config_.diffusion_interval_ticks == 0) {
        diffuse_populations(current_tick, active_chunks);
    }

    if (wild_proxy_sink_ != nullptr) {
        std::vector<ChunkKey> stopped_interactive;
        stopped_interactive.reserve(interactive_proxy_chunks_.size());
        for (const ChunkKey& chunk : interactive_proxy_chunks_) {
            if (!current_interactive_chunks.contains(chunk)) {
                stopped_interactive.push_back(chunk);
            }
        }
        std::sort(stopped_interactive.begin(), stopped_interactive.end(), chunk_key_less);
        for (const ChunkKey& chunk : stopped_interactive) {
            GameEcosystemWildProxyRebalanceRequest request;
            request.chunk = chunk;
            request.source_tick = current_tick;
            if (const PopulationCell* cell = find_population_cell(chunk); cell != nullptr) {
                request.population = *cell;
            }
            wild_proxy_sink_->request_wild_proxy_rebalance(request);
        }
        force_wild_proxy_rebalance_ = false;
    }
    if (far_visual_sink_ != nullptr) {
        std::vector<ChunkKey> stopped_far_visuals;
        stopped_far_visuals.reserve(far_visual_proxy_chunks_.size());
        for (const ChunkKey& chunk : far_visual_proxy_chunks_) {
            if (!current_far_visual_chunks.contains(chunk)) {
                stopped_far_visuals.push_back(chunk);
            }
        }
        std::sort(stopped_far_visuals.begin(), stopped_far_visuals.end(), chunk_key_less);
        for (const ChunkKey& chunk : stopped_far_visuals) {
            GameEcosystemFarVisualRebalanceRequest request;
            request.chunk = chunk;
            request.source_tick = current_tick;
            if (const PopulationCell* cell = find_population_cell(chunk); cell != nullptr) {
                request.population = *cell;
            }
            far_visual_sink_->request_far_visual_rebalance(request);
        }

        std::vector<ChunkKey> current_far_visuals;
        current_far_visuals.reserve(current_far_visual_chunks.size());
        for (const ChunkKey& chunk : current_far_visual_chunks) {
            current_far_visuals.push_back(chunk);
        }
        std::sort(current_far_visuals.begin(), current_far_visuals.end(), chunk_key_less);
        for (const ChunkKey& chunk : current_far_visuals) {
            const bool newly_visual = !far_visual_proxy_chunks_.contains(chunk);
            if (!newly_visual && !force_far_visual_rebalance_ &&
                config_.far_visual_rebalance_interval_ticks != 0 &&
                current_tick % config_.far_visual_rebalance_interval_ticks != 0) {
                continue;
            }
            const PopulationCell* const cell = find_population_cell(chunk);
            if (cell == nullptr) continue;
            far_visual_sink_->request_far_visual_rebalance(
                make_far_visual_rebalance_request(chunk, current_tick, *cell));
        }
        force_far_visual_rebalance_ = false;
    }
    interactive_proxy_chunks_ = std::move(current_interactive_chunks);
    far_visual_proxy_chunks_ = std::move(current_far_visual_chunks);
}

uint8_t GameEcosystemSystem::infer_biome(const ChunkKey& chunk) const {
    if (chunks_ == nullptr || worldgen_config_ == nullptr) return ecosystem_biome::kBarren;
    const VoxelChunk* const voxel_chunk = chunks_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (voxel_chunk == nullptr || voxel_chunk->terrain.size_x <= 0 ||
        voxel_chunk->terrain.size_y <= 0 || voxel_chunk->terrain.size_z <= 0) {
        return ecosystem_biome::kBarren;
    }

    uint32_t land = 0;
    uint32_t water = 0;
    uint32_t sand = 0;
    uint32_t rock = 0;
    for (int local_z = 0; local_z < voxel_chunk->terrain.size_z; ++local_z) {
        for (int local_x = 0; local_x < voxel_chunk->terrain.size_x; ++local_x) {
            for (int local_y = voxel_chunk->terrain.size_y - 1; local_y >= 0; --local_y) {
                const TerrainCell& cell = voxel_chunk->terrain.cell_at(local_x, local_y, local_z);
                if (cell.material == worldgen_config_->roles.air && !cell.has_fluid()) continue;
                if (cell.has_fluid() || cell.material == worldgen_config_->roles.water) {
                    ++water;
                    break;
                }
                ++land;
                const TerrainMaterialDef* material = worldgen_config_->find_material(
                    static_cast<TerrainMaterialId>(cell.material));
                if (material != nullptr && material->key.find("sand") != std::string::npos) {
                    ++sand;
                } else if (cell.material == worldgen_config_->roles.stone) {
                    ++rock;
                }
                break;
            }
        }
    }
    const uint32_t observed = land + water;
    if (observed == 0) return ecosystem_biome::kBarren;
    if (water * 2 > observed) return ecosystem_biome::kOcean;
    if (sand * 2 > land) return ecosystem_biome::kDesert;
    if (rock * 2 > land) return ecosystem_biome::kRocky;
    return ecosystem_biome::kPlains;
}

bool GameEcosystemSystem::is_active_loaded_chunk(const ChunkKey& chunk) const {
    if (chunks_ == nullptr) return false;
    const VoxelChunk* const voxel_chunk = chunks_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    return voxel_chunk != nullptr && voxel_chunk->state == ChunkState::Active;
}

GameEcosystemSystem::ChunkActivity GameEcosystemSystem::activity_for_chunk(
    const ChunkKey& chunk,
    const std::vector<GameEcosystemInterestCenter>& centers) const noexcept {
    bool macro_active = false;
    bool visual_active = false;
    for (const GameEcosystemInterestCenter& center : centers) {
        if (center.chunk.dimension_id != chunk.dimension_id) continue;
        const int64_t dx = static_cast<int64_t>(chunk.chunk_x) - center.chunk.chunk_x;
        const int64_t dy = static_cast<int64_t>(chunk.chunk_y) - center.chunk.chunk_y;
        const int64_t dz = static_cast<int64_t>(chunk.chunk_z) - center.chunk.chunk_z;
        const uint64_t absolute_dx = static_cast<uint64_t>(dx < 0 ? -dx : dx);
        const uint64_t absolute_dz = static_cast<uint64_t>(dz < 0 ? -dz : dz);
        const uint64_t horizontal_distance_squared =
            absolute_dx * absolute_dx + absolute_dz * absolute_dz;
        const uint64_t vertical_distance = static_cast<uint64_t>(dy < 0 ? -dy : dy);

        const uint64_t macro_radius = config_.macro_horizontal_radius_chunks;
        const uint64_t macro_vertical_radius = config_.macro_vertical_radius_chunks;
        if (vertical_distance > macro_vertical_radius ||
            horizontal_distance_squared > macro_radius * macro_radius) {
            continue;
        }

        const uint64_t interactive_radius = config_.interactive_horizontal_radius_chunks;
        const uint64_t interactive_vertical_radius =
            config_.interactive_vertical_radius_chunks;
        if (vertical_distance <= interactive_vertical_radius &&
            horizontal_distance_squared <= interactive_radius * interactive_radius) {
            return ChunkActivity::kInteractive;
        }
        const uint64_t visual_radius = std::min<uint64_t>(
            config_.visual_horizontal_radius_chunks, macro_radius);
        const uint64_t visual_vertical_radius = std::min<uint64_t>(
            config_.visual_vertical_radius_chunks, macro_vertical_radius);
        if (vertical_distance <= visual_vertical_radius &&
            horizontal_distance_squared <= visual_radius * visual_radius) {
            visual_active = true;
        }
        macro_active = true;
    }
    if (visual_active) return ChunkActivity::kVisual;
    return macro_active ? ChunkActivity::kMacro : ChunkActivity::kInactive;
}

void GameEcosystemSystem::initialize_population(
    PopulationCell& cell, uint8_t biome_type, uint64_t current_tick) const {
    const GameEcosystemBiomeRules& rules = config_.rules_for(biome_type);
    cell = {};
    cell.biome_type = rules.biome_type;
    cell.vegetation_density = std::min(0.5f, rules.max_vegetation);
    cell.herbivore_density = std::min(0.3f, rules.max_herbivore);
    cell.predator_density = std::min(0.1f, rules.max_predator);
    cell.soil_fertility = std::clamp(rules.base_fertility, 0.0f, 1.0f);
    cell.water_availability = std::clamp(rules.base_water, 0.0f, 1.0f);
    cell.dead_biomass = 0.0f;
    cell.hunting_pressure_herb = 0.0f;
    cell.hunting_pressure_pred = 0.0f;
    cell.last_macro_simulation_tick = current_tick;
}

void GameEcosystemSystem::advance_population(
    PopulationCell& cell, Season season,
    const GameEcosystemEnvironmentSample& environment,
    uint64_t elapsed_ticks) const {
    if (elapsed_ticks == 0) return;
    const uint64_t bounded_ticks = config_.max_macro_catchup_ticks == 0
        ? elapsed_ticks
        : std::min(elapsed_ticks, config_.max_macro_catchup_ticks);
    const uint64_t maximum_substeps = std::max<uint32_t>(
        1, config_.max_macro_catchup_substeps);
    const uint64_t step_ticks = std::max<uint64_t>(
        1, (bounded_ticks + maximum_substeps - 1) / maximum_substeps);
    uint64_t remaining_ticks = bounded_ticks;
    while (remaining_ticks != 0) {
        const uint64_t step = std::min(remaining_ticks, step_ticks);
        const float step_multiplier = static_cast<float>(step);
    const GameEcosystemBiomeRules& rules = config_.rules_for(cell.biome_type);
    const int index = season_index(season);
    const float rate_multiplier = nonnegative_rate_multiplier(environment.rate_multiplier) *
        step_multiplier;
    const float water = environment.water_availability.has_value() &&
            finite_unit(*environment.water_availability)
        ? *environment.water_availability
        : std::clamp(rules.base_water + config_.season_water_adjustment[index], 0.0f, 1.0f);
    cell.water_availability = water;
    const float vegetation_day = environment.is_daytime ? 1.0f : config_.night_vegetation_activity;
    const float herbivore_day = environment.is_daytime ? 1.0f : config_.night_herbivore_activity;
    const float predator_day = environment.is_daytime ? 1.0f : config_.night_predator_activity;

    const float vegetation_growth = rate_multiplier * config_.vegetation_growth_rate *
        rules.vegetation_growth_multiplier * cell.soil_fertility * water *
        config_.season_vegetation_growth[index] * vegetation_day;
    const float vegetation_decay = rate_multiplier * config_.vegetation_decay_rate *
        cell.vegetation_density;
    const float grazing = rate_multiplier * config_.grazing_rate * cell.herbivore_density *
        cell.vegetation_density * herbivore_day;
    cell.vegetation_density = std::clamp(
        cell.vegetation_density + vegetation_growth - vegetation_decay - grazing,
        0.0f, std::clamp(rules.max_vegetation, 0.0f, 1.0f));

    const float herbivore_reproduction = rate_multiplier * config_.herbivore_reproduction_rate *
        cell.vegetation_density * cell.herbivore_density *
        config_.season_herbivore_reproduction[index] * herbivore_day;
    const float herbivore_death = rate_multiplier * config_.herbivore_natural_death_rate *
        cell.herbivore_density;
    const float predation = rate_multiplier * config_.predation_rate * cell.predator_density *
        cell.herbivore_density * predator_day;
    const float herbivore_hunting = rate_multiplier * cell.hunting_pressure_herb *
        cell.herbivore_density;
    cell.herbivore_density = std::clamp(
        cell.herbivore_density + herbivore_reproduction - herbivore_death - predation -
            herbivore_hunting,
        0.0f, std::clamp(rules.max_herbivore, 0.0f, 1.0f));

    const float predator_reproduction = rate_multiplier * config_.predator_reproduction_rate *
        cell.herbivore_density * cell.predator_density *
        config_.season_predator_reproduction[index] * predator_day;
    const float predator_death = rate_multiplier * config_.predator_natural_death_rate *
        cell.predator_density;
    const float predator_hunting = rate_multiplier * cell.hunting_pressure_pred *
        cell.predator_density;
    cell.predator_density = std::clamp(
        cell.predator_density + predator_reproduction - predator_death - predator_hunting,
        0.0f, std::clamp(rules.max_predator, 0.0f, 1.0f));

    const float pressure_decay = std::clamp(config_.hunting_pressure_decay, 0.0f, 1.0f);
    cell.hunting_pressure_herb *= std::pow(pressure_decay, rate_multiplier);
    cell.hunting_pressure_pred *= std::pow(pressure_decay, rate_multiplier);
    if (cell.hunting_pressure_herb < 1e-6f) cell.hunting_pressure_herb = 0.0f;
    if (cell.hunting_pressure_pred < 1e-6f) cell.hunting_pressure_pred = 0.0f;

    const float biomass = (herbivore_death + predator_death + herbivore_hunting +
                           predator_hunting) * config_.death_to_biomass_fraction +
        vegetation_decay * config_.vegetation_decay_to_biomass_fraction;
    cell.dead_biomass = std::clamp(cell.dead_biomass + biomass, 0.0f, 1.0f);
    const float decomposed = rate_multiplier * config_.decomposition_rate * cell.dead_biomass;
    cell.dead_biomass = std::max(0.0f, cell.dead_biomass - decomposed);
    cell.soil_fertility = std::clamp(cell.soil_fertility + decomposed, 0.0f, 1.0f);
    cell.clamp_all();
        remaining_ticks -= step;
    }
}

void GameEcosystemSystem::diffuse_populations(
    uint64_t current_tick, const std::vector<ChunkKey>& active_chunks) {
    if (config_.diffusion_rate <= 0.0f || active_chunks.empty() || sidecars_ == nullptr) return;
    std::unordered_map<ChunkKey, PopulationDelta> deltas;
    uint32_t processed = 0;
    for (const ChunkKey& chunk : active_chunks) {
        if (config_.max_diffusion_pairs_per_pass != 0 &&
            processed >= config_.max_diffusion_pairs_per_pass) {
            break;
        }
        const PopulationCell* const cell = find_population_cell(chunk);
        if (cell == nullptr) continue;
        const std::array<ChunkKey, 3> neighbors = {{
            {chunk.dimension_id, chunk.chunk_x + 1, chunk.chunk_y, chunk.chunk_z},
            {chunk.dimension_id, chunk.chunk_x, chunk.chunk_y + 1, chunk.chunk_z},
            {chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z + 1},
        }};
        for (const ChunkKey& neighbor_key : neighbors) {
            if (config_.max_diffusion_pairs_per_pass != 0 &&
                processed >= config_.max_diffusion_pairs_per_pass) {
                break;
            }
            if (!std::binary_search(active_chunks.begin(), active_chunks.end(), neighbor_key,
                                    chunk_key_less)) {
                continue;
            }
            const PopulationCell* const neighbor = find_population_cell(neighbor_key);
            if (neighbor == nullptr) continue;
            const float vegetation = (cell->vegetation_density - neighbor->vegetation_density) *
                config_.diffusion_rate;
            const float herbivore = (cell->herbivore_density - neighbor->herbivore_density) *
                config_.diffusion_rate;
            const float predator = (cell->predator_density - neighbor->predator_density) *
                config_.diffusion_rate;
            deltas[chunk].vegetation -= vegetation;
            deltas[chunk].herbivore -= herbivore;
            deltas[chunk].predator -= predator;
            deltas[neighbor_key].vegetation += vegetation;
            deltas[neighbor_key].herbivore += herbivore;
            deltas[neighbor_key].predator += predator;
            ++processed;
        }
    }
    std::vector<ChunkKey> changed;
    changed.reserve(deltas.size());
    for (const auto& [chunk, _] : deltas) changed.push_back(chunk);
    std::sort(changed.begin(), changed.end(), chunk_key_less);
    for (const ChunkKey& chunk : changed) {
        PopulationCell* const cell = find_population_cell(chunk);
        if (cell == nullptr) continue;
        const PopulationCell previous = *cell;
        const PopulationDelta& delta = deltas.at(chunk);
        cell->vegetation_density += delta.vegetation;
        cell->herbivore_density += delta.herbivore;
        cell->predator_density += delta.predator;
        cell->clamp_all();
        emit_population_mutation(GameEcosystemPopulationMutationKind::kDiffused, chunk,
                                 current_tick, previous, *cell);
    }
}

GameEcosystemWildProxyRebalanceRequest
GameEcosystemSystem::make_wild_proxy_rebalance_request(
    const ChunkKey& chunk, uint64_t current_tick, const PopulationCell& population) const {
    GameEcosystemWildProxyRebalanceRequest request{
        .chunk = chunk,
        .source_tick = current_tick,
        .population = population,
    };
    append_wild_proxy_plans(request, CreatureRole::HERBIVORE,
                            population.herbivore_density,
                            config_.wild_proxy_herbivore_min_density,
                            config_.max_wild_herbivore_proxies_per_chunk);
    append_wild_proxy_plans(request, CreatureRole::PREDATOR,
                            population.predator_density,
                            config_.wild_proxy_predator_min_density,
                            config_.max_wild_predator_proxies_per_chunk);
    return request;
}

GameEcosystemFarVisualRebalanceRequest
GameEcosystemSystem::make_far_visual_rebalance_request(
    const ChunkKey& chunk, uint64_t current_tick,
    const PopulationCell& population) const {
    GameEcosystemFarVisualRebalanceRequest request{
        .chunk = chunk,
        .source_tick = current_tick,
        .population = population,
    };
    append_wild_proxy_plans(request, CreatureRole::HERBIVORE,
                            population.herbivore_density,
                            config_.far_visual_herbivore_min_density,
                            config_.max_far_visual_herbivore_proxies_per_chunk);
    append_wild_proxy_plans(request, CreatureRole::PREDATOR,
                            population.predator_density,
                            config_.far_visual_predator_min_density,
                            config_.max_far_visual_predator_proxies_per_chunk);
    return request;
}

void GameEcosystemSystem::append_wild_proxy_plans(
    GameEcosystemWildProxyRebalanceRequest& request, CreatureRole role,
    float density, float minimum_density, uint32_t maximum) const {
    const uint32_t count = density_to_wild_proxy_count(density, minimum_density, maximum);
    if (count == 0 || species_catalog_ == nullptr) return;

    std::vector<uint16_t> candidates;
    for (const uint16_t species_id : species_catalog_->all_species_ids()) {
        const CreatureSpeciesDef* const definition = species_catalog_->get_species(species_id);
        if (definition == nullptr || definition->role != role ||
            std::find(definition->biomes.begin(), definition->biomes.end(),
                      request.population.biome_type) == definition->biomes.end()) {
            continue;
        }
        candidates.push_back(species_id);
    }
    // Species without an explicit biome are event-only. Do not fall back to
    // another biome: a projected wild creature must respect content limits.
    if (candidates.empty()) return;

    request.proxies.reserve(request.proxies.size() + count);
    for (uint32_t slot = 0; slot < count; ++slot) {
        const uint64_t stable_id = stable_wild_proxy_id(
            request.chunk, request.population.biome_type, role, slot);
        request.proxies.push_back({
            .stable_id = stable_id,
            .species_id = candidates[static_cast<size_t>(stable_id % candidates.size())],
            .role = role,
            .slot = slot,
        });
    }
}

uint32_t GameEcosystemSystem::density_to_wild_proxy_count(
    float density, float minimum_density, uint32_t maximum) const noexcept {
    if (maximum == 0 || !std::isfinite(density) || !std::isfinite(minimum_density)) {
        return 0;
    }
    const float clamped_density = std::clamp(density, 0.0f, 1.0f);
    const float clamped_minimum = std::clamp(minimum_density, 0.0f, 1.0f);
    if (clamped_density < clamped_minimum) return 0;
    if (clamped_minimum >= 1.0f) return 1;

    const float normalized = (clamped_density - clamped_minimum) /
        (1.0f - clamped_minimum);
    const uint32_t count = 1 + static_cast<uint32_t>(
        normalized * static_cast<float>(maximum - 1));
    return std::min(count, maximum);
}

void GameEcosystemSystem::emit_population_mutation(
    GameEcosystemPopulationMutationKind kind, const ChunkKey& chunk, uint64_t current_tick,
    const PopulationCell& previous, const PopulationCell& current) const {
    if (mutation_sink_ == nullptr) return;
    mutation_sink_->on_ecosystem_population_mutated({
        .kind = kind,
        .chunk = chunk,
        .source_tick = current_tick,
        .previous = previous,
        .current = current,
    });
}

}  // namespace snt::game
