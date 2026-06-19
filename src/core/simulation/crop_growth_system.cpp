#include "crop_growth_system.hpp"
#include "ecosystem_system.hpp"
#include "season_system.hpp"
#include "population_cell.hpp"
#include "season_def.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"
#include "../world_gen/world_gen_config.hpp"
#include "../world_gen/noise_generator.hpp"
#include "../world_gen/world_seed.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void CropGrowthSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void CropGrowthSystem::tick_active(const ChunkKey& chunk, float delta,
                                   const TickContext* ctx) {
    (void)delta;
    if (!world_) return;

    const int64_t tick = world_->current_tick();
    growth_count_ = 0;
    moisture_update_count_ = 0;

    // Phase 1: Update farmland moisture (evaporation) for this chunk.
    update_farmland_moisture(chunk, tick, ctx);

    // Phase 2: Grow crops in this chunk.
    auto& registry = world_->block_entity_registry();
    auto entities = registry.entities_in_chunk(
        chunk.dimension_id,
        chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);

    for (const auto& entity_id : entities) {
        if (growth_count_ >= kMaxGrowthPerTick) break;

        if (registry.get_entity_type(entity_id) == BlockEntityType::CROP) {
            if (try_grow_crop(entity_id, chunk.dimension_id, tick, ctx)) {
                ++growth_count_;
            }
        }
    }
}

void CropGrowthSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                    const TickContext* ctx) {
    (void)chunk;
    (void)delta;
    (void)ctx;
    // Crops do not grow in sleeping chunks.
}

void CropGrowthSystem::shutdown() {
    // No persistent state to drain.
}

// --- Farmland moisture update ---

void CropGrowthSystem::update_farmland_moisture(
    const ChunkKey& chunk, int64_t current_tick,
    const TickContext* ctx) {
    if (!world_) return;

    auto& registry = world_->block_entity_registry();
    auto entities = registry.entities_in_chunk(
        chunk.dimension_id,
        chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);

    // Determine if it is currently raining (ecosystem water availability
    // above a threshold counts as rain). This is a simplification; a real
    // weather system would feed an explicit flag.
    bool raining = false;
    if (ctx && ctx->ecosystem) {
        const PopulationCell* cell =
            ctx->ecosystem->get_population_cell(chunk);
        if (cell && cell->water_availability > 0.75f) {
            raining = true;
        }
    }

    for (const auto& entity_id : entities) {
        if (moisture_update_count_ >= kMaxMoistureUpdatesPerTick) break;
        if (registry.get_entity_type(entity_id) != BlockEntityType::FARMLAND) {
            continue;
        }

        FarmlandBlockEntityState* state =
            registry.get_farmland_state_mut(entity_id);
        if (!state) continue;

        const int64_t elapsed = current_tick - state->last_moisture_tick;
        if (elapsed <= 0) continue;

        // Apply evaporation / rain accumulation per elapsed tick.
        // Evaporation: -0.001/tick (slow dry-out).
        // Rain: +0.01/tick (fast hydration).
        const float delta_per_tick = raining ? 0.01f : -0.001f;
        state->moisture += delta_per_tick * static_cast<float>(elapsed);
        state->moisture = std::clamp(state->moisture, 0.0f, 1.0f);
        state->last_moisture_tick = current_tick;
        ++moisture_update_count_;
    }
}

// --- Crop growth logic ---

bool CropGrowthSystem::try_grow_crop(
    EntityId entity_id,
    const std::string& dimension_id,
    int64_t current_tick,
    const TickContext* ctx) {
    auto& registry = world_->block_entity_registry();
    CropBlockEntityState* state = registry.get_crop_state_mut(entity_id);
    if (!state) return false;

    // Already mature: no further growth (until harvested, if repeat_harvest).
    if (state->growth_stage == CropGrowthStage::MATURE) {
        return false;
    }

    // Look up the species definition.
    auto config = world_->worldgen_config();
    if (!config) return false;
    const CropSpeciesDef* species = config->find_crop_species(state->species_key);
    if (!species) return false;

    // Determine base ticks required for the next transition.
    int64_t base_ticks_required = 0;
    switch (state->growth_stage) {
        case CropGrowthStage::SEED:
            base_ticks_required = species->ticks_seed_to_sprout;
            break;
        case CropGrowthStage::SPROUT:
            base_ticks_required = species->ticks_sprout_to_growing;
            break;
        case CropGrowthStage::GROWING:
            base_ticks_required = species->ticks_growing_to_mature;
            break;
        default:
            return false;
    }

    // --- Growth rate modifiers ---

    // 1. Farmland moisture/fertility (per-cell, primary source).
    //    The crop sits on top of a farmland block; query the FARMLAND entity
    //    at (root_x, root_y - 1, root_z).
    const BlockEntityPlacement* placement = registry.get_placement(entity_id);
    if (!placement) return false;

    float moisture = 0.5f;
    float fertility = 0.5f;
    bool has_farmland = false;

    const EntityId farmland_owner = registry.find_owner_at(
        placement->root_x, placement->root_y - 1, placement->root_z);
    if (farmland_owner.id != 0) {
        const FarmlandBlockEntityState* fl =
            registry.get_farmland_state(farmland_owner);
        if (fl) {
            moisture = fl->moisture;
            fertility = fl->fertility;
            has_farmland = true;
        }
    }

    // 2. Ecosystem fallback (per-chunk) if no farmland entity (wild crop).
    if (!has_farmland && ctx && ctx->ecosystem) {
        constexpr int kChunkSize = 32;
        const int cx = static_cast<int>(
            std::floor(static_cast<float>(placement->root_x) / kChunkSize));
        const int cy = static_cast<int>(
            std::floor(static_cast<float>(placement->root_y) / kChunkSize));
        const int cz = static_cast<int>(
            std::floor(static_cast<float>(placement->root_z) / kChunkSize));
        ChunkKey key(dimension_id, cx, cy, cz);
        const PopulationCell* cell = ctx->ecosystem->get_population_cell(key);
        if (cell) {
            moisture = cell->water_availability;
            fertility = cell->soil_fertility;
        }
    }

    // fertility_factor = (1 - sensitivity) + sensitivity * fertility
    // water_factor     = (1 - sensitivity) + sensitivity * moisture
    const float fertility_factor =
        (1.0f - species->fertility_sensitivity) +
        species->fertility_sensitivity * fertility;
    const float water_factor =
        (1.0f - species->water_sensitivity) +
        species->water_sensitivity * moisture;

    // 3. Season modifier: off-season growth rate x0.3.
    float season_factor = 1.0f;
    const int season = current_season(ctx);
    if (season >= 0 && species->grow_season >= 0 &&
        season != species->grow_season) {
        season_factor = 0.3f;
    }

    // 4. Continuous-cropping penalty: same crop 3+ times → x0.5.
    float rotation_factor = 1.0f;
    if (has_farmland) {
        const FarmlandBlockEntityState* fl =
            registry.get_farmland_state(farmland_owner);
        if (fl && fl->consecutive_same_crop >= 3 &&
            fl->last_crop_key == state->species_key) {
            rotation_factor = 0.5f;
        }
    }

    const float growth_modifier =
        fertility_factor * water_factor * season_factor * rotation_factor;
    const int64_t effective_ticks_required = (growth_modifier > 0.001f)
        ? static_cast<int64_t>(
              std::ceil(static_cast<float>(base_ticks_required) / growth_modifier))
        : base_ticks_required * 100;

    const int64_t elapsed = current_tick - state->last_growth_tick;
    if (elapsed < effective_ticks_required) {
        return false;
    }

    // Check growth conditions (biome, etc.).
    if (!check_growth_conditions(*species, dimension_id,
            placement->root_x, placement->root_y, placement->root_z,
            state->growth_stage, ctx)) {
        return false;
    }

    // Apply the growth transition.
    CropGrowthStage new_stage = static_cast<CropGrowthStage>(
        static_cast<int>(state->growth_stage) + 1);
    advance_crop_stage(entity_id, *species, dimension_id,
        placement->root_x, placement->root_y, placement->root_z,
        new_stage, current_tick);

    return true;
}

bool CropGrowthSystem::check_growth_conditions(
    const CropSpeciesDef& species,
    const std::string& dimension_id,
    int root_x, int root_y, int root_z,
    CropGrowthStage current_stage,
    const TickContext* ctx) const {
    (void)dimension_id;
    (void)current_stage;
    (void)ctx;

    // Check temperature/humidity if biome data is available.
    float temperature = 0.0f;
    float humidity = 0.0f;
    if (get_biome_at(root_x, root_y, root_z, temperature, humidity)) {
        if (temperature < species.temperature_min ||
            temperature > species.temperature_max ||
            humidity < species.humidity_min ||
            humidity > species.humidity_max) {
            return false;
        }
    }

    return true;
}

void CropGrowthSystem::advance_crop_stage(
    EntityId entity_id,
    const CropSpeciesDef& species,
    const std::string& dimension_id,
    int root_x, int root_y, int root_z,
    CropGrowthStage new_stage,
    int64_t current_tick) {
    auto& registry = world_->block_entity_registry();
    auto config = world_->worldgen_config();
    if (!config) return;

    const int stage_index = static_cast<int>(new_stage);
    if (stage_index < 0 || stage_index >= 4) return;

    const std::string& material_key = species.stage_material_keys[stage_index];
    const TerrainMaterialId material = config->material_id_or(material_key, 0);
    if (material == 0) return;

    // Update the terrain cell to the new stage's material.
    set_world_cell(dimension_id, root_x, root_y, root_z, material,
        TF_WALKABLE | TF_MINEABLE);

    // Update the block entity state.
    CropBlockEntityState* state = registry.get_crop_state_mut(entity_id);
    if (state) {
        state->growth_stage = new_stage;
        state->last_growth_tick = current_tick;
    }
}

// --- Helpers ---

bool CropGrowthSystem::set_world_cell(
    const std::string& dimension_id,
    int world_x, int world_y, int world_z,
    TerrainMaterialId material, uint32_t flags) {
    if (!world_) return false;

    constexpr int kChunkSize = 32;
    const int cx = static_cast<int>(
        std::floor(static_cast<float>(world_x) / kChunkSize));
    const int cy = static_cast<int>(
        std::floor(static_cast<float>(world_y) / kChunkSize));
    const int cz = static_cast<int>(
        std::floor(static_cast<float>(world_z) / kChunkSize));
    const int lx = world_x - cx * kChunkSize;
    const int ly = world_y - cy * kChunkSize;
    const int lz = world_z - cz * kChunkSize;

    ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return false;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return false;

    chunk->terrain.cell_at(lx, ly, lz).material =
        static_cast<TerrainMaterial>(material);
    chunk->terrain.cell_at(lx, ly, lz).flags = flags;
    return true;
}

bool CropGrowthSystem::get_biome_at(
    int global_x, int global_y, int global_z,
    float& out_temperature, float& out_humidity) const {
    if (!world_) return false;

    auto config = world_->worldgen_config();
    if (!config) return false;

    // Simple temperature model: decreases with altitude, varies with x/z.
    // Matches the approximation used by TreeGrowthSystem.
    const float lat_factor = static_cast<float>(global_z) * 0.001f;
    const float alt_factor = static_cast<float>(global_y) * 0.01f;
    out_temperature = std::clamp(0.0f - lat_factor - alt_factor, -1.0f, 1.0f);

    const float humid_factor = static_cast<float>(global_x) * 0.001f;
    out_humidity = std::clamp(humid_factor, -1.0f, 1.0f);

    return true;
}

int CropGrowthSystem::current_season(const TickContext* ctx) const {
    if (!ctx || !ctx->season) return -1;
    return static_cast<int>(ctx->season->current_season());
}

} // namespace science_and_theology
