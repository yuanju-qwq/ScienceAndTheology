// Deterministic game-owned crop and farmland simulation implementation.

#include "game/simulation/crop_growth_system.h"

#include "core/error.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

constexpr uint64_t kCropAnchorIdFlag = uint64_t{1} << 60u;
constexpr uint64_t kFarmlandAnchorIdFlag = uint64_t{1} << 59u;
constexpr uint64_t kCropAnchorSerialMask = kCropAnchorIdFlag - 1u;
constexpr uint64_t kFarmlandAnchorSerialMask = kFarmlandAnchorIdFlag - 1u;
constexpr float kEvaporationPerTick = 0.001f;
constexpr float kRainMoisturePerTick = 0.01f;
constexpr float kOffSeasonGrowthFactor = 0.3f;
constexpr float kRotationGrowthFactor = 0.5f;
constexpr float kMinimumGrowthModifier = 0.001f;
constexpr float kHarvestFertilityCost = 0.15f;

struct ResolvedCell {
    snt::voxel::TerrainCell* cell = nullptr;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
};

struct FarmlandReference {
    GameChunkSidecar* sidecar = nullptr;
    BlockEntityPlacement* anchor = nullptr;
    FarmlandPersistenceRecord* record = nullptr;
};

struct CropReference {
    GameChunkSidecar* sidecar = nullptr;
    BlockEntityPlacement* anchor = nullptr;
    CropGrowthPersistenceRecord* record = nullptr;
};

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

int32_t local_coordinate(int32_t value, int32_t chunk_coordinate,
                         int32_t chunk_size) noexcept {
    return static_cast<int32_t>(static_cast<int64_t>(value) -
                                static_cast<int64_t>(chunk_coordinate) * chunk_size);
}

bool offset_coordinate(int32_t value, int64_t offset, int32_t& out) noexcept {
    const int64_t result = static_cast<int64_t>(value) + offset;
    if (result < (std::numeric_limits<int32_t>::min)() ||
        result > (std::numeric_limits<int32_t>::max)()) {
        return false;
    }
    out = static_cast<int32_t>(result);
    return true;
}

ResolvedCell resolve_cell(snt::voxel::ChunkRegistry& chunks,
                          std::string_view dimension_id,
                          int32_t block_x,
                          int32_t block_y,
                          int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    ResolvedCell result;
    result.chunk_x = floor_divide(block_x, kChunkSize);
    result.chunk_y = floor_divide(block_y, kChunkSize);
    result.chunk_z = floor_divide(block_z, kChunkSize);
    const int32_t local_x = local_coordinate(block_x, result.chunk_x, kChunkSize);
    const int32_t local_y = local_coordinate(block_y, result.chunk_y, kChunkSize);
    const int32_t local_z = local_coordinate(block_z, result.chunk_z, kChunkSize);
    snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        std::string(dimension_id), result.chunk_x, result.chunk_y, result.chunk_z);
    if (chunk == nullptr || !chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
        return result;
    }
    result.cell = &chunk->terrain.cell_at(local_x, local_y, local_z);
    return result;
}

bool anchor_belongs_to_key(const BlockEntityPlacement& anchor,
                           const ChunkKey& key) noexcept {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return floor_divide(anchor.root_x, kChunkSize) == key.chunk_x &&
           floor_divide(anchor.root_y, kChunkSize) == key.chunk_y &&
           floor_divide(anchor.root_z, kChunkSize) == key.chunk_z;
}

BlockEntityPlacement* find_unique_anchor(GameChunkSidecar& sidecar,
                                         EntityId anchor_id,
                                         BlockEntityType type) noexcept {
    BlockEntityPlacement* found = nullptr;
    for (BlockEntityPlacement& anchor : sidecar.block_entities) {
        if (anchor.id != anchor_id) continue;
        if (found != nullptr) return nullptr;
        found = &anchor;
    }
    if (found == nullptr || found->entity_type != type) return nullptr;
    return found;
}

BlockEntityPlacement* find_unique_anchor_at(GameChunkSidecar& sidecar,
                                            BlockEntityType type,
                                            int32_t block_x,
                                            int32_t block_y,
                                            int32_t block_z) noexcept {
    BlockEntityPlacement* found = nullptr;
    for (BlockEntityPlacement& anchor : sidecar.block_entities) {
        if (anchor.entity_type != type || anchor.root_x != block_x ||
            anchor.root_y != block_y || anchor.root_z != block_z) {
            continue;
        }
        if (found != nullptr) return nullptr;
        found = &anchor;
    }
    return found;
}

bool root_is_occupied(const GameChunkSidecar& sidecar,
                      int32_t block_x,
                      int32_t block_y,
                      int32_t block_z) noexcept {
    return std::any_of(sidecar.block_entities.begin(), sidecar.block_entities.end(),
                       [block_x, block_y, block_z](const BlockEntityPlacement& anchor) {
                           return anchor.root_x == block_x && anchor.root_y == block_y &&
                                  anchor.root_z == block_z;
                       });
}

FarmlandPersistenceRecord* find_unique_farmland_record(
    GameChunkSidecar& sidecar,
    EntityId anchor_id) noexcept {
    FarmlandPersistenceRecord* found = nullptr;
    for (FarmlandPersistenceRecord& record : sidecar.farmland_records) {
        if (record.anchor_entity_id != anchor_id) continue;
        if (found != nullptr) return nullptr;
        found = &record;
    }
    return found;
}

CropGrowthPersistenceRecord* find_unique_crop_record(
    GameChunkSidecar& sidecar,
    EntityId anchor_id) noexcept {
    CropGrowthPersistenceRecord* found = nullptr;
    for (CropGrowthPersistenceRecord& record : sidecar.crop_growth_records) {
        if (record.anchor_entity_id != anchor_id) continue;
        if (found != nullptr) return nullptr;
        found = &record;
    }
    return found;
}

size_t farmland_record_count(const GameChunkSidecar& sidecar,
                             EntityId anchor_id) noexcept {
    return static_cast<size_t>(std::count_if(
        sidecar.farmland_records.begin(), sidecar.farmland_records.end(),
        [anchor_id](const FarmlandPersistenceRecord& record) {
            return record.anchor_entity_id == anchor_id;
        }));
}

size_t crop_record_count(const GameChunkSidecar& sidecar,
                         EntityId anchor_id) noexcept {
    return static_cast<size_t>(std::count_if(
        sidecar.crop_growth_records.begin(), sidecar.crop_growth_records.end(),
        [anchor_id](const CropGrowthPersistenceRecord& record) {
            return record.anchor_entity_id == anchor_id;
        }));
}

std::optional<FarmlandReference> find_farmland_at(
    GameChunkSidecarRegistry& sidecars,
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const ChunkKey key{
        std::string(dimension_id),
        floor_divide(block_x, kChunkSize),
        floor_divide(block_y, kChunkSize),
        floor_divide(block_z, kChunkSize),
    };
    GameChunkSidecar* sidecar = sidecars.get(key);
    if (sidecar == nullptr) return std::nullopt;
    BlockEntityPlacement* anchor = find_unique_anchor_at(
        *sidecar, BlockEntityType::FARMLAND, block_x, block_y, block_z);
    if (anchor == nullptr || !anchor_belongs_to_key(*anchor, key) ||
        farmland_record_count(*sidecar, anchor->id) != 1) {
        return std::nullopt;
    }
    FarmlandPersistenceRecord* record = find_unique_farmland_record(*sidecar, anchor->id);
    if (record == nullptr) return std::nullopt;
    return FarmlandReference{.sidecar = sidecar, .anchor = anchor, .record = record};
}

std::optional<CropReference> find_crop_at(
    GameChunkSidecarRegistry& sidecars,
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const ChunkKey key{
        std::string(dimension_id),
        floor_divide(block_x, kChunkSize),
        floor_divide(block_y, kChunkSize),
        floor_divide(block_z, kChunkSize),
    };
    GameChunkSidecar* sidecar = sidecars.get(key);
    if (sidecar == nullptr) return std::nullopt;
    BlockEntityPlacement* anchor = find_unique_anchor_at(
        *sidecar, BlockEntityType::CROP, block_x, block_y, block_z);
    if (anchor == nullptr || !anchor_belongs_to_key(*anchor, key) ||
        crop_record_count(*sidecar, anchor->id) != 1) {
        return std::nullopt;
    }
    CropGrowthPersistenceRecord* record = find_unique_crop_record(*sidecar, anchor->id);
    if (record == nullptr) return std::nullopt;
    return CropReference{.sidecar = sidecar, .anchor = anchor, .record = record};
}

bool crop_layout_matches_anchor(const CropGrowthPersistenceRecord& record,
                                const BlockEntityPlacement& anchor) noexcept {
    return record.owned_cells.size() == 1 &&
           record.owned_cells.front().block_x == anchor.root_x &&
           record.owned_cells.front().block_y == anchor.root_y &&
           record.owned_cells.front().block_z == anchor.root_z;
}

TerrainMaterialId material_for_crop_stage(const WorldGenConfigSnapshot& config,
                                          const CropSpeciesDef& species,
                                          CropGrowthStage stage) noexcept {
    const int stage_index = static_cast<int>(stage);
    if (stage_index < 0 || stage_index >= static_cast<int>(CropGrowthStage::COUNT)) {
        return config.roles.air;
    }
    return config.material_id_or(species.stage_material_keys[stage_index], config.roles.air);
}

uint64_t required_ticks(int64_t configured_ticks) noexcept {
    return configured_ticks > 0 ? static_cast<uint64_t>(configured_ticks) : 1u;
}

uint64_t stage_wait_ticks(const CropSpeciesDef& species,
                          const CropGrowthPersistenceRecord& record) noexcept {
    switch (record.growth_stage) {
        case CropGrowthStage::SEED:
            return required_ticks(species.ticks_seed_to_sprout);
        case CropGrowthStage::SPROUT:
            return required_ticks(species.ticks_sprout_to_growing);
        case CropGrowthStage::GROWING:
            return record.is_regrowing
                ? required_ticks(species.regrow_ticks)
                : required_ticks(species.ticks_growing_to_mature);
        case CropGrowthStage::MATURE:
        case CropGrowthStage::COUNT:
            return 0;
    }
    return 0;
}

float clamp_unit(float value, float fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, 0.0f, 1.0f);
}

uint64_t effective_wait_ticks(uint64_t base_ticks, float modifier) noexcept {
    if (!std::isfinite(modifier) || modifier < kMinimumGrowthModifier) {
        modifier = kMinimumGrowthModifier;
    }
    const double requested = std::ceil(
        static_cast<double>(base_ticks) / static_cast<double>(modifier));
    const double max_value = static_cast<double>((std::numeric_limits<uint64_t>::max)());
    if (!std::isfinite(requested) || requested >= max_value) {
        return (std::numeric_limits<uint64_t>::max)();
    }
    return std::max<uint64_t>(1u, static_cast<uint64_t>(requested));
}

bool is_known_season(Season season) noexcept {
    const int value = static_cast<int>(season);
    return value >= 0 && value < static_cast<int>(Season::COUNT);
}

bool is_registered_non_air_material(const WorldGenConfigSnapshot& config,
                                    TerrainMaterialId material) noexcept {
    return material != config.roles.air && config.find_material(material) != nullptr;
}

[[nodiscard]] snt::core::Expected<EntityId> allocate_anchor_id(
    const GameChunkSidecarRegistry& sidecars,
    uint64_t flag,
    uint64_t serial_mask,
    std::string_view kind) {
    std::unordered_set<uint64_t> occupied_ids;
    uint64_t greatest_serial = 0;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            occupied_ids.insert(placement.id.id);
            if ((placement.id.id & flag) == 0 ||
                (placement.id.id & (uint64_t{1} << 63u)) != 0) {
                continue;
            }
            greatest_serial = std::max(greatest_serial, placement.id.id & serial_mask);
        }
    });
    if (greatest_serial >= serial_mask) {
        return invalid_state(std::string(kind) + " exhausted reserved anchor ids");
    }
    for (uint64_t serial = greatest_serial + 1u; serial <= serial_mask; ++serial) {
        const uint64_t candidate = flag | serial;
        if (!occupied_ids.contains(candidate)) return EntityId{candidate};
    }
    return invalid_state(std::string(kind) + " exhausted reserved anchor ids");
}

}  // namespace

GameCropGrowthSystem::GameCropGrowthSystem(
    snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars,
    const WorldGenConfigSnapshot& worldgen_config,
    CropGrowthLimits limits) noexcept
    : chunks_(&chunks),
      sidecars_(&sidecars),
      worldgen_config_(&worldgen_config),
      limits_(limits) {}

snt::core::Expected<EntityId> GameCropGrowthSystem::till_farmland(
    const FarmlandTillingRequest& request,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Crop growth is not initialized");
    }
    if (request.dimension_id.empty()) {
        return invalid_argument("Farmland tilling requires a dimension id");
    }
    if (!std::isfinite(request.initial_moisture) || !std::isfinite(request.initial_fertility) ||
        request.initial_moisture < 0.0f || request.initial_moisture > 1.0f ||
        request.initial_fertility < 0.0f || request.initial_fertility > 1.0f) {
        return invalid_argument("Farmland initial moisture and fertility must be in [0, 1]");
    }

    const TerrainMaterialId farmland_material = worldgen_config_->runtime_ids.farmland;
    if (!is_registered_non_air_material(*worldgen_config_, farmland_material)) {
        return invalid_state("Crop growth has no registered farmland material");
    }
    ResolvedCell target = resolve_cell(*chunks_, request.dimension_id,
                                       request.block_x, request.block_y, request.block_z);
    if (target.cell == nullptr) return invalid_state("Farmland target cell is not loaded");
    if (target.cell->material != worldgen_config_->roles.dirt || target.cell->has_fluid()) {
        return invalid_state("Farmland target cell is not dirt");
    }

    int32_t above_y = 0;
    if (!offset_coordinate(request.block_y, 1, above_y)) {
        return invalid_state("Farmland target has no valid cell above");
    }
    const ResolvedCell above = resolve_cell(*chunks_, request.dimension_id,
                                            request.block_x, above_y, request.block_z);
    if (above.cell == nullptr || above.cell->material != worldgen_config_->roles.air ||
        above.cell->has_fluid()) {
        return invalid_state("Farmland requires a loaded empty cell above");
    }

    const ChunkKey key{request.dimension_id, target.chunk_x, target.chunk_y, target.chunk_z};
    GameChunkSidecar* sidecar = sidecars_->get(key);
    if (sidecar == nullptr) return invalid_state("Farmland target has no loaded game sidecar");
    if (root_is_occupied(*sidecar, request.block_x, request.block_y, request.block_z)) {
        return invalid_state("Farmland target already has a sidecar anchor");
    }

    auto anchor_id = allocate_anchor_id(*sidecars_, kFarmlandAnchorIdFlag,
                                        kFarmlandAnchorSerialMask, "Farmland");
    if (!anchor_id) return anchor_id.error();
    if (!write_cell(request.dimension_id, request.block_x, request.block_y,
                    request.block_z, farmland_material)) {
        return invalid_state("Farmland target changed before commit");
    }

    sidecar->block_entities.push_back({
        .id = *anchor_id,
        .entity_type = BlockEntityType::FARMLAND,
        .root_x = request.block_x,
        .root_y = request.block_y,
        .root_z = request.block_z,
        .owned_cell_count = 1,
    });
    sidecar->farmland_records.push_back({
        .anchor_entity_id = *anchor_id,
        .moisture = request.initial_moisture,
        .fertility = request.initial_fertility,
        .last_moisture_tick = source_tick,
    });
    return *anchor_id;
}

snt::core::Expected<EntityId> GameCropGrowthSystem::plant_crop(
    const CropPlantingRequest& request,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Crop growth is not initialized");
    }
    if (request.dimension_id.empty() || request.species_key.empty()) {
        return invalid_argument("Crop planting requires a dimension and species key");
    }
    const CropSpeciesDef* species = worldgen_config_->find_crop_species(request.species_key);
    if (species == nullptr) return invalid_argument("Crop species is not registered");
    if (!is_registered_non_air_material(*worldgen_config_, worldgen_config_->runtime_ids.farmland)) {
        return invalid_state("Crop growth has no registered farmland material");
    }
    const TerrainMaterialId seed_material = material_for_crop_stage(
        *worldgen_config_, *species, CropGrowthStage::SEED);
    if (!is_registered_non_air_material(*worldgen_config_, seed_material)) {
        return invalid_state("Crop species has no registered seed-stage material");
    }

    ResolvedCell target = resolve_cell(*chunks_, request.dimension_id,
                                       request.block_x, request.block_y, request.block_z);
    if (target.cell == nullptr) return invalid_state("Crop planting target cell is not loaded");
    if (target.cell->material != worldgen_config_->roles.air || target.cell->has_fluid()) {
        return invalid_state("Crop planting target cell is not empty");
    }
    int32_t below_y = 0;
    if (!offset_coordinate(request.block_y, -1, below_y)) {
        return invalid_state("Crop planting target has no valid supporting cell");
    }
    const ResolvedCell below = resolve_cell(*chunks_, request.dimension_id,
                                            request.block_x, below_y, request.block_z);
    if (below.cell == nullptr || below.cell->material != worldgen_config_->runtime_ids.farmland ||
        below.cell->has_fluid()) {
        return invalid_state("Crop planting requires loaded farmland");
    }
    auto farmland = find_farmland_at(*sidecars_, request.dimension_id,
                                     request.block_x, below_y, request.block_z);
    if (!farmland.has_value()) {
        return invalid_state("Crop planting farmland has no typed sidecar record");
    }
    // Appending the crop anchor can reallocate the same sidecar's anchor
    // vector, so retain this stable value before any append.
    const EntityId farmland_anchor_id = farmland->anchor->id;

    const ChunkKey key{request.dimension_id, target.chunk_x, target.chunk_y, target.chunk_z};
    GameChunkSidecar* sidecar = sidecars_->get(key);
    if (sidecar == nullptr) return invalid_state("Crop planting target has no loaded game sidecar");
    if (root_is_occupied(*sidecar, request.block_x, request.block_y, request.block_z)) {
        return invalid_state("Crop planting target already has a sidecar anchor");
    }

    auto anchor_id = allocate_anchor_id(*sidecars_, kCropAnchorIdFlag,
                                        kCropAnchorSerialMask, "Crop");
    if (!anchor_id) return anchor_id.error();
    if (!write_cell(request.dimension_id, request.block_x, request.block_y,
                    request.block_z, seed_material)) {
        return invalid_state("Crop planting target changed before commit");
    }

    sidecar->block_entities.push_back({
        .id = *anchor_id,
        .entity_type = BlockEntityType::CROP,
        .root_x = request.block_x,
        .root_y = request.block_y,
        .root_z = request.block_z,
        .owned_cell_count = 1,
    });
    sidecar->crop_growth_records.push_back({
        .anchor_entity_id = *anchor_id,
        .farmland_anchor_entity_id = farmland_anchor_id,
        .species_key = request.species_key,
        .growth_stage = CropGrowthStage::SEED,
        .planted_tick = source_tick,
        .last_growth_tick = source_tick,
        .owned_cells = {{
            .block_x = request.block_x,
            .block_y = request.block_y,
            .block_z = request.block_z,
            .material = seed_material,
        }},
    });

    FarmlandPersistenceRecord& farmland_record = *farmland->record;
    if (farmland_record.last_crop_key == request.species_key) {
        if (farmland_record.consecutive_same_crop < (std::numeric_limits<uint32_t>::max)()) {
            ++farmland_record.consecutive_same_crop;
        }
    } else {
        farmland_record.last_crop_key = request.species_key;
        farmland_record.consecutive_same_crop = 1;
    }
    return *anchor_id;
}

snt::core::Expected<CropHarvestResult> GameCropGrowthSystem::harvest_crop(
    const CropHarvestRequest& request,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Crop growth is not initialized");
    }
    if (request.dimension_id.empty()) {
        return invalid_argument("Crop harvest requires a dimension id");
    }
    auto crop = find_crop_at(*sidecars_, request.dimension_id,
                             request.block_x, request.block_y, request.block_z);
    if (!crop.has_value() || !crop_layout_matches_anchor(*crop->record, *crop->anchor)) {
        return invalid_state("Crop harvest target has no valid typed crop record");
    }
    CropGrowthPersistenceRecord& record = *crop->record;
    if (record.growth_stage != CropGrowthStage::MATURE) {
        return invalid_state("Crop harvest requires a mature crop");
    }
    if (source_tick < record.last_growth_tick) {
        return invalid_state("Crop harvest tick predates the crop state");
    }
    const CropSpeciesDef* species = worldgen_config_->find_crop_species(record.species_key);
    if (species == nullptr) return invalid_state("Crop harvest species is not registered");

    std::optional<FarmlandReference> farmland;
    if (record.farmland_anchor_entity_id.id != 0) {
        int32_t below_y = 0;
        if (!offset_coordinate(crop->anchor->root_y, -1, below_y)) {
            return invalid_state("Crop harvest has no valid supporting cell");
        }
        farmland = find_farmland_at(*sidecars_, request.dimension_id,
                                    crop->anchor->root_x, below_y, crop->anchor->root_z);
        if (!farmland.has_value() ||
            farmland->anchor->id != record.farmland_anchor_entity_id) {
            return invalid_state("Crop harvest supporting farmland state is invalid");
        }
    }

    CropHarvestResult result{
        .crop_anchor_id = crop->anchor->id,
        .species_key = species->species_key,
        .crop_item_key = species->crop_item_key,
        .crop_count = std::max(0, species->crop_min),
        .byproduct_item_key = species->byproduct_item_key,
        .byproduct_count = std::max(0, species->byproduct_count),
        .regrowing = species->repeat_harvest,
    };

    bool committed = false;
    if (species->repeat_harvest) {
        committed = set_crop_stage(request.dimension_id, *crop->anchor, record, *species,
                                   CropGrowthStage::GROWING, source_tick, true);
        if (committed) {
            record.last_harvest_tick = source_tick;
            record.is_regrowing = true;
        }
    } else {
        const CropGrowthOwnedCell& owned = record.owned_cells.front();
        const ResolvedCell current = resolve_cell(*chunks_, request.dimension_id,
                                                  owned.block_x, owned.block_y, owned.block_z);
        committed = current.cell != nullptr && !current.cell->has_fluid() &&
            current.cell->material == owned.material &&
            write_cell(request.dimension_id, owned.block_x, owned.block_y,
                       owned.block_z, worldgen_config_->roles.air);
        if (committed) {
            const EntityId crop_anchor_id = crop->anchor->id;
            crop->sidecar->crop_growth_records.erase(std::remove_if(
                crop->sidecar->crop_growth_records.begin(),
                crop->sidecar->crop_growth_records.end(),
                [crop_anchor_id](const CropGrowthPersistenceRecord& candidate) {
                    return candidate.anchor_entity_id == crop_anchor_id;
                }), crop->sidecar->crop_growth_records.end());
            crop->sidecar->block_entities.erase(std::remove_if(
                crop->sidecar->block_entities.begin(), crop->sidecar->block_entities.end(),
                [crop_anchor_id](const BlockEntityPlacement& candidate) {
                    return candidate.id == crop_anchor_id;
                }), crop->sidecar->block_entities.end());
        }
    }
    if (!committed) return invalid_state("Crop harvest target changed before commit");
    if (farmland.has_value()) {
        farmland->record->fertility = std::max(
            0.0f, farmland->record->fertility - kHarvestFertilityCost);
    }
    return result;
}

snt::core::Expected<CropHarvestResult> GameCropGrowthSystem::preview_harvest_crop(
    const CropHarvestRequest& request,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Crop growth is not initialized");
    }
    if (request.dimension_id.empty()) {
        return invalid_argument("Crop harvest requires a dimension id");
    }
    auto crop = find_crop_at(*sidecars_, request.dimension_id,
                             request.block_x, request.block_y, request.block_z);
    if (!crop.has_value() || !crop_layout_matches_anchor(*crop->record, *crop->anchor)) {
        return invalid_state("Crop harvest target has no valid typed crop record");
    }
    const CropGrowthPersistenceRecord& record = *crop->record;
    if (record.growth_stage != CropGrowthStage::MATURE) {
        return invalid_state("Crop harvest requires a mature crop");
    }
    if (source_tick < record.last_growth_tick) {
        return invalid_state("Crop harvest tick predates the crop state");
    }
    const CropSpeciesDef* species = worldgen_config_->find_crop_species(record.species_key);
    if (species == nullptr) return invalid_state("Crop harvest species is not registered");

    if (record.farmland_anchor_entity_id.id != 0) {
        int32_t below_y = 0;
        if (!offset_coordinate(crop->anchor->root_y, -1, below_y)) {
            return invalid_state("Crop harvest has no valid supporting cell");
        }
        const auto farmland = find_farmland_at(*sidecars_, request.dimension_id,
                                               crop->anchor->root_x, below_y,
                                               crop->anchor->root_z);
        if (!farmland.has_value() ||
            farmland->anchor->id != record.farmland_anchor_entity_id) {
            return invalid_state("Crop harvest supporting farmland state is invalid");
        }
    }

    return CropHarvestResult{
        .crop_anchor_id = crop->anchor->id,
        .species_key = species->species_key,
        .crop_item_key = species->crop_item_key,
        .crop_count = std::max(0, species->crop_min),
        .byproduct_item_key = species->byproduct_item_key,
        .byproduct_count = std::max(0, species->byproduct_count),
        .regrowing = species->repeat_harvest,
    };
}

snt::core::Expected<CropGrowthStage> GameCropGrowthSystem::fertilize_crop(
    const CropFertilizationRequest& request,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Crop growth is not initialized");
    }
    if (request.dimension_id.empty()) {
        return invalid_argument("Crop fertilization requires a dimension id");
    }
    auto crop = find_crop_at(*sidecars_, request.dimension_id,
                             request.block_x, request.block_y, request.block_z);
    if (!crop.has_value() || !crop_layout_matches_anchor(*crop->record, *crop->anchor)) {
        return invalid_state("Crop fertilization target has no valid typed crop record");
    }
    CropGrowthPersistenceRecord& record = *crop->record;
    if (record.growth_stage == CropGrowthStage::MATURE ||
        record.growth_stage == CropGrowthStage::COUNT) {
        return invalid_state("Crop fertilization requires a non-mature crop");
    }
    if (source_tick < record.last_growth_tick) {
        return invalid_state("Crop fertilization tick predates the crop state");
    }
    const CropSpeciesDef* species = worldgen_config_->find_crop_species(record.species_key);
    if (species == nullptr) return invalid_state("Crop fertilization species is not registered");
    const CropGrowthStage next_stage = static_cast<CropGrowthStage>(
        static_cast<int>(record.growth_stage) + 1);
    if (!set_crop_stage(request.dimension_id, *crop->anchor, record, *species,
                        next_stage, source_tick)) {
        return invalid_state("Crop fertilization target changed before commit");
    }
    return next_stage;
}

void GameCropGrowthSystem::tick(uint64_t current_tick, Season current_season) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) return;

    struct WorkItem {
        ChunkKey key;
        GameChunkSidecar* sidecar = nullptr;
        size_t record_index = 0;
        uint64_t anchor_id = 0;
    };
    std::vector<WorkItem> farmland_work;
    std::vector<WorkItem> crop_work;
    sidecars_->for_each([&](const ChunkKey& key, GameChunkSidecar& sidecar) {
        const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr || chunk->state != snt::voxel::ChunkState::Active) return;
        for (size_t index = 0; index < sidecar.farmland_records.size(); ++index) {
            farmland_work.push_back({
                .key = key,
                .sidecar = &sidecar,
                .record_index = index,
                .anchor_id = sidecar.farmland_records[index].anchor_entity_id.id,
            });
        }
        for (size_t index = 0; index < sidecar.crop_growth_records.size(); ++index) {
            crop_work.push_back({
                .key = key,
                .sidecar = &sidecar,
                .record_index = index,
                .anchor_id = sidecar.crop_growth_records[index].anchor_entity_id.id,
            });
        }
    });
    const auto work_less = [](const WorkItem& left, const WorkItem& right) {
        if (left.key.dimension_id != right.key.dimension_id) {
            return left.key.dimension_id < right.key.dimension_id;
        }
        if (left.key.chunk_x != right.key.chunk_x) return left.key.chunk_x < right.key.chunk_x;
        if (left.key.chunk_y != right.key.chunk_y) return left.key.chunk_y < right.key.chunk_y;
        if (left.key.chunk_z != right.key.chunk_z) return left.key.chunk_z < right.key.chunk_z;
        return left.anchor_id < right.anchor_id;
    };
    std::sort(farmland_work.begin(), farmland_work.end(), work_less);
    std::sort(crop_work.begin(), crop_work.end(), work_less);

    uint32_t moisture_updates = 0;
    for (const WorkItem& item : farmland_work) {
        if (moisture_updates >= limits_.max_moisture_updates_per_tick) break;
        if (item.sidecar == nullptr ||
            item.record_index >= item.sidecar->farmland_records.size()) {
            continue;
        }
        FarmlandPersistenceRecord& record = item.sidecar->farmland_records[item.record_index];
        if (update_farmland_moisture(item.key, *item.sidecar, record, current_tick)) {
            ++moisture_updates;
        }
    }

    uint32_t transitions = 0;
    for (const WorkItem& item : crop_work) {
        if (transitions >= limits_.max_stage_transitions_per_tick) break;
        if (item.sidecar == nullptr ||
            item.record_index >= item.sidecar->crop_growth_records.size()) {
            continue;
        }
        CropGrowthPersistenceRecord& record = item.sidecar->crop_growth_records[item.record_index];
        if (try_grow_crop(item.key, *item.sidecar, record, current_tick, current_season)) {
            ++transitions;
        }
    }
}

bool GameCropGrowthSystem::update_farmland_moisture(
    const ChunkKey& key,
    GameChunkSidecar& sidecar,
    FarmlandPersistenceRecord& record,
    uint64_t current_tick) {
    if (current_tick <= record.last_moisture_tick ||
        farmland_record_count(sidecar, record.anchor_entity_id) != 1) {
        return false;
    }
    BlockEntityPlacement* anchor = find_unique_anchor(
        sidecar, record.anchor_entity_id, BlockEntityType::FARMLAND);
    if (anchor == nullptr || !anchor_belongs_to_key(*anchor, key)) return false;

    bool raining = false;
    if (environment_provider_ != nullptr) {
        CropGrowthEnvironment environment;
        if (environment_provider_->sample_crop_growth_environment(
                key.dimension_id, anchor->root_x, anchor->root_y, anchor->root_z, environment)) {
            raining = environment.is_raining;
        }
    }
    const uint64_t elapsed = current_tick - record.last_moisture_tick;
    const double delta_per_tick = raining
        ? static_cast<double>(kRainMoisturePerTick)
        : -static_cast<double>(kEvaporationPerTick);
    const double moisture = static_cast<double>(record.moisture) +
        delta_per_tick * static_cast<double>(elapsed);
    record.moisture = static_cast<float>(std::clamp(moisture, 0.0, 1.0));
    record.last_moisture_tick = current_tick;
    return true;
}

bool GameCropGrowthSystem::try_grow_crop(
    const ChunkKey& key,
    GameChunkSidecar& sidecar,
    CropGrowthPersistenceRecord& record,
    uint64_t current_tick,
    Season current_season) {
    if (record.growth_stage == CropGrowthStage::MATURE ||
        record.growth_stage == CropGrowthStage::COUNT ||
        crop_record_count(sidecar, record.anchor_entity_id) != 1 ||
        current_tick < record.last_growth_tick) {
        return false;
    }
    BlockEntityPlacement* anchor = find_unique_anchor(
        sidecar, record.anchor_entity_id, BlockEntityType::CROP);
    if (anchor == nullptr || !anchor_belongs_to_key(*anchor, key) ||
        !crop_layout_matches_anchor(record, *anchor)) {
        return false;
    }
    const CropSpeciesDef* species = worldgen_config_->find_crop_species(record.species_key);
    if (species == nullptr) return false;
    const uint64_t base_wait = stage_wait_ticks(*species, record);
    if (base_wait == 0) return false;

    CropGrowthEnvironment environment;
    const bool has_environment = environment_provider_ != nullptr &&
        environment_provider_->sample_crop_growth_environment(
            key.dimension_id, anchor->root_x, anchor->root_y, anchor->root_z, environment);
    if (has_environment) {
        if (!std::isfinite(environment.temperature) || !std::isfinite(environment.humidity) ||
            environment.temperature < species->temperature_min ||
            environment.temperature > species->temperature_max ||
            environment.humidity < species->humidity_min ||
            environment.humidity > species->humidity_max) {
            return false;
        }
    }

    float moisture = 0.5f;
    float fertility = 0.5f;
    std::optional<FarmlandReference> farmland;
    if (record.farmland_anchor_entity_id.id != 0) {
        int32_t below_y = 0;
        if (!offset_coordinate(anchor->root_y, -1, below_y)) return false;
        const ResolvedCell below = resolve_cell(*chunks_, key.dimension_id,
                                                anchor->root_x, below_y, anchor->root_z);
        farmland = find_farmland_at(*sidecars_, key.dimension_id,
                                    anchor->root_x, below_y, anchor->root_z);
        if (below.cell == nullptr ||
            below.cell->material != worldgen_config_->runtime_ids.farmland ||
            below.cell->has_fluid() || !farmland.has_value() ||
            farmland->anchor->id != record.farmland_anchor_entity_id) {
            return false;
        }
        moisture = farmland->record->moisture;
        fertility = farmland->record->fertility;
    } else if (has_environment) {
        moisture = environment.water_availability;
        fertility = environment.soil_fertility;
    }
    moisture = clamp_unit(moisture, 0.5f);
    fertility = clamp_unit(fertility, 0.5f);
    const float fertility_sensitivity = clamp_unit(species->fertility_sensitivity, 0.0f);
    const float water_sensitivity = clamp_unit(species->water_sensitivity, 0.0f);
    const float fertility_factor = (1.0f - fertility_sensitivity) +
        fertility_sensitivity * fertility;
    const float water_factor = (1.0f - water_sensitivity) + water_sensitivity * moisture;
    float season_factor = 1.0f;
    if (is_known_season(current_season) && species->grow_season >= 0 &&
        species->grow_season != static_cast<int>(current_season)) {
        season_factor = kOffSeasonGrowthFactor;
    }
    float rotation_factor = 1.0f;
    if (farmland.has_value() && farmland->record->consecutive_same_crop >= 3 &&
        farmland->record->last_crop_key == record.species_key) {
        rotation_factor = kRotationGrowthFactor;
    }
    const uint64_t wait = effective_wait_ticks(
        base_wait, fertility_factor * water_factor * season_factor * rotation_factor);
    if (current_tick - record.last_growth_tick < wait) return false;

    const TerrainMaterialId expected_material = material_for_crop_stage(
        *worldgen_config_, *species, record.growth_stage);
    const CropGrowthOwnedCell& owned = record.owned_cells.front();
    const ResolvedCell current = resolve_cell(*chunks_, key.dimension_id,
                                              owned.block_x, owned.block_y, owned.block_z);
    if (!is_registered_non_air_material(*worldgen_config_, expected_material) ||
        current.cell == nullptr || current.cell->has_fluid() ||
        owned.material != expected_material || current.cell->material != owned.material) {
        return false;
    }
    const CropGrowthStage next_stage = static_cast<CropGrowthStage>(
        static_cast<int>(record.growth_stage) + 1);
    return set_crop_stage(key.dimension_id, *anchor, record, *species, next_stage, current_tick);
}

bool GameCropGrowthSystem::set_crop_stage(
    std::string_view dimension_id,
    BlockEntityPlacement& anchor,
    CropGrowthPersistenceRecord& record,
    const CropSpeciesDef& species,
    CropGrowthStage new_stage,
    uint64_t current_tick,
    bool allow_stage_regression) {
    if (worldgen_config_ == nullptr || chunks_ == nullptr ||
        !crop_layout_matches_anchor(record, anchor) ||
        new_stage == CropGrowthStage::COUNT ||
        (!allow_stage_regression &&
         static_cast<int>(new_stage) <= static_cast<int>(record.growth_stage)) ||
        current_tick < record.last_growth_tick) {
        return false;
    }
    const TerrainMaterialId new_material = material_for_crop_stage(
        *worldgen_config_, species, new_stage);
    if (!is_registered_non_air_material(*worldgen_config_, new_material)) return false;
    CropGrowthOwnedCell& owned = record.owned_cells.front();
    const ResolvedCell current = resolve_cell(*chunks_, dimension_id,
                                              owned.block_x, owned.block_y, owned.block_z);
    if (current.cell == nullptr || current.cell->has_fluid() ||
        current.cell->material != owned.material) {
        return false;
    }
    if (!write_cell(dimension_id, owned.block_x, owned.block_y, owned.block_z, new_material)) {
        return false;
    }
    owned.material = new_material;
    anchor.owned_cell_count = static_cast<uint32_t>(record.owned_cells.size());
    record.growth_stage = new_stage;
    record.last_growth_tick = current_tick;
    if (new_stage == CropGrowthStage::MATURE) record.is_regrowing = false;
    return true;
}

bool GameCropGrowthSystem::write_cell(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    TerrainMaterialId material) {
    if (chunks_ == nullptr || worldgen_config_ == nullptr ||
        worldgen_config_->find_material(material) == nullptr) {
        return false;
    }
    const ResolvedCell resolved = resolve_cell(*chunks_, dimension_id,
                                               block_x, block_y, block_z);
    if (resolved.cell == nullptr) return false;
    const snt::voxel::TerrainCell previous = *resolved.cell;
    resolved.cell->material = material;
    resolved.cell->flags = worldgen_config_->flags_for_material(material);
    if (previous.material != resolved.cell->material || previous.flags != resolved.cell->flags) {
        emit_terrain_change(dimension_id, block_x, block_y, block_z, previous, *resolved.cell);
    }
    return true;
}

void GameCropGrowthSystem::emit_terrain_change(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    const snt::voxel::TerrainCell& previous,
    const snt::voxel::TerrainCell& current) const {
    if (mutation_sink_ == nullptr) return;
    mutation_sink_->on_crop_growth_terrain_changed({
        .dimension_id = std::string(dimension_id),
        .block_x = block_x,
        .block_y = block_y,
        .block_z = block_z,
        .previous_material = static_cast<uint32_t>(previous.material),
        .previous_flags = previous.flags,
        .current_material = static_cast<uint32_t>(current.material),
        .current_flags = current.flags,
    });
}

}  // namespace snt::game
