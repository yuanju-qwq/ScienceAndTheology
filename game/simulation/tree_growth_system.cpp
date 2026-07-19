// Deterministic game-owned tree growth implementation.

#include "game/simulation/tree_growth_system.h"

#include "core/error.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

constexpr uint64_t kTreeAnchorIdFlag = uint64_t{1} << 61u;
constexpr uint64_t kTreeAnchorSerialMask = kTreeAnchorIdFlag - 1u;
constexpr int32_t kMaxSupportedTrunkHeight = 32;
constexpr int32_t kMaxSupportedCanopyRadius = 8;

struct ResolvedCell {
    snt::voxel::TerrainCell* cell = nullptr;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
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

bool same_position(const TreeGrowthOwnedCell& left,
                   const TreeGrowthOwnedCell& right) noexcept {
    return left.block_x == right.block_x && left.block_y == right.block_y &&
           left.block_z == right.block_z;
}

bool layout_contains(const std::vector<TreeGrowthOwnedCell>& layout,
                     const TreeGrowthOwnedCell& candidate) noexcept {
    return std::any_of(layout.begin(), layout.end(), [&candidate](const auto& cell) {
        return same_position(cell, candidate);
    });
}

bool is_record_owned_cell(const TreeGrowthPersistenceRecord& record,
                          int32_t block_x,
                          int32_t block_y,
                          int32_t block_z,
                          TerrainMaterialId material) noexcept {
    return std::any_of(record.owned_cells.begin(), record.owned_cells.end(),
                       [block_x, block_y, block_z, material](const auto& owned) {
                           return owned.block_x == block_x && owned.block_y == block_y &&
                                  owned.block_z == block_z && owned.material == material;
                       });
}

bool append_layout_cell(std::vector<TreeGrowthOwnedCell>& layout,
                        int64_t block_x,
                        int64_t block_y,
                        int64_t block_z,
                        TerrainMaterialId material) {
    if (block_x < (std::numeric_limits<int32_t>::min)() ||
        block_x > (std::numeric_limits<int32_t>::max)() ||
        block_y < (std::numeric_limits<int32_t>::min)() ||
        block_y > (std::numeric_limits<int32_t>::max)() ||
        block_z < (std::numeric_limits<int32_t>::min)() ||
        block_z > (std::numeric_limits<int32_t>::max)()) {
        return false;
    }
    const TreeGrowthOwnedCell candidate{
        .block_x = static_cast<int32_t>(block_x),
        .block_y = static_cast<int32_t>(block_y),
        .block_z = static_cast<int32_t>(block_z),
        .material = material,
    };
    if (!layout_contains(layout, candidate)) layout.push_back(candidate);
    return true;
}

BlockEntityPlacement* find_tree_anchor(GameChunkSidecar& sidecar,
                                       EntityId anchor_id) noexcept {
    BlockEntityPlacement* found = nullptr;
    for (BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != anchor_id) continue;
        if (found != nullptr) return nullptr;
        found = &placement;
    }
    if (found == nullptr || found->entity_type != BlockEntityType::TREE) return nullptr;
    return found;
}

size_t tree_record_count(const GameChunkSidecar& sidecar, EntityId anchor_id) noexcept {
    return static_cast<size_t>(std::count_if(
        sidecar.tree_growth_records.begin(), sidecar.tree_growth_records.end(),
        [anchor_id](const TreeGrowthPersistenceRecord& record) {
            return record.anchor_entity_id == anchor_id;
        }));
}

uint64_t required_ticks(int64_t configured_ticks) noexcept {
    return configured_ticks > 0 ? static_cast<uint64_t>(configured_ticks) : 1u;
}

[[nodiscard]] snt::core::Expected<EntityId> allocate_tree_anchor_id(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> occupied_ids;
    uint64_t greatest_serial = 0;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            occupied_ids.insert(placement.id.id);
            if ((placement.id.id & kTreeAnchorIdFlag) == 0 ||
                (placement.id.id & (uint64_t{1} << 63u)) != 0) {
                continue;
            }
            greatest_serial = std::max(
                greatest_serial, placement.id.id & kTreeAnchorSerialMask);
        }
    });
    if (greatest_serial >= kTreeAnchorSerialMask) {
        return invalid_state("Tree growth exhausted reserved tree anchor ids");
    }
    for (uint64_t serial = greatest_serial + 1u; serial <= kTreeAnchorSerialMask; ++serial) {
        const uint64_t candidate = kTreeAnchorIdFlag | serial;
        if (!occupied_ids.contains(candidate)) return EntityId{candidate};
    }
    return invalid_state("Tree growth exhausted reserved tree anchor ids");
}

}  // namespace

GameTreeGrowthSystem::GameTreeGrowthSystem(
    snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars,
    const WorldGenConfigSnapshot& worldgen_config,
    TreeGrowthLimits limits) noexcept
    : chunks_(&chunks),
      sidecars_(&sidecars),
      worldgen_config_(&worldgen_config),
      limits_(limits) {}

snt::core::Expected<EntityId> GameTreeGrowthSystem::plant_sapling(
    const TreeSaplingPlacement& placement,
    uint64_t source_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr) {
        return invalid_state("Tree growth is not initialized");
    }
    if (placement.dimension_id.empty() || placement.species_key.empty()) {
        return invalid_argument("Tree sapling placement requires a dimension and species key");
    }
    const TreeSpeciesDef* species = worldgen_config_->find_tree_species(placement.species_key);
    if (species == nullptr) return invalid_argument("Tree sapling species is not registered");
    const TerrainMaterialId sapling_material = worldgen_config_->material_id_or(
        species->sapling_material_key, worldgen_config_->roles.air);
    if (sapling_material == worldgen_config_->roles.air) {
        return invalid_state("Tree sapling species has no non-air sapling material");
    }

    ResolvedCell target = resolve_cell(*chunks_, placement.dimension_id,
                                       placement.block_x, placement.block_y, placement.block_z);
    if (target.cell == nullptr) {
        return invalid_state("Tree sapling target cell is not loaded");
    }
    if (target.cell->material != worldgen_config_->roles.air || target.cell->has_fluid()) {
        return invalid_state("Tree sapling target cell is not empty");
    }
    int32_t ground_y = 0;
    if (!offset_coordinate(placement.block_y, -1, ground_y)) {
        return invalid_state("Tree sapling target has no valid ground coordinate");
    }
    const ResolvedCell ground = resolve_cell(*chunks_, placement.dimension_id,
                                             placement.block_x, ground_y, placement.block_z);
    if (ground.cell == nullptr || !ground.cell->is_solid()) {
        return invalid_state("Tree sapling requires loaded solid ground");
    }

    const ChunkKey key{placement.dimension_id, target.chunk_x, target.chunk_y, target.chunk_z};
    GameChunkSidecar* sidecar = sidecars_->get(key);
    if (sidecar == nullptr) {
        return invalid_state("Tree sapling target has no loaded game sidecar");
    }
    const bool root_occupied = std::any_of(
        sidecar->block_entities.begin(), sidecar->block_entities.end(),
        [&placement](const BlockEntityPlacement& anchor) {
            return anchor.root_x == placement.block_x && anchor.root_y == placement.block_y &&
                   anchor.root_z == placement.block_z;
        });
    if (root_occupied) return invalid_state("Tree sapling target already has a sidecar anchor");

    auto anchor_id = allocate_tree_anchor_id(*sidecars_);
    if (!anchor_id) return anchor_id.error();
    if (!write_cell(placement.dimension_id, placement.block_x, placement.block_y,
                    placement.block_z, sapling_material)) {
        return invalid_state("Tree sapling target changed before commit");
    }

    sidecar->block_entities.push_back({
        .id = *anchor_id,
        .entity_type = BlockEntityType::TREE,
        .root_x = placement.block_x,
        .root_y = placement.block_y,
        .root_z = placement.block_z,
        .owned_cell_count = 1,
    });
    sidecar->tree_growth_records.push_back({
        .anchor_entity_id = *anchor_id,
        .species_key = placement.species_key,
        .growth_stage = TreeGrowthStage::SAPLING,
        .planted_tick = source_tick,
        .last_growth_tick = source_tick,
        .owned_cells = {{
            .block_x = placement.block_x,
            .block_y = placement.block_y,
            .block_z = placement.block_z,
            .material = sapling_material,
        }},
    });
    return *anchor_id;
}

void GameTreeGrowthSystem::tick(uint64_t current_tick) {
    if (chunks_ == nullptr || sidecars_ == nullptr || worldgen_config_ == nullptr ||
        limits_.max_stage_transitions_per_tick == 0) {
        return;
    }

    struct WorkItem {
        ChunkKey key;
        GameChunkSidecar* sidecar = nullptr;
        size_t record_index = 0;
        uint64_t anchor_id = 0;
    };
    std::vector<WorkItem> work;
    sidecars_->for_each([&](const ChunkKey& key, GameChunkSidecar& sidecar) {
        const snt::voxel::VoxelChunk* chunk = chunks_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr || chunk->state != snt::voxel::ChunkState::Active) return;
        for (size_t index = 0; index < sidecar.tree_growth_records.size(); ++index) {
            work.push_back({
                .key = key,
                .sidecar = &sidecar,
                .record_index = index,
                .anchor_id = sidecar.tree_growth_records[index].anchor_entity_id.id,
            });
        }
    });
    std::sort(work.begin(), work.end(), [](const WorkItem& left, const WorkItem& right) {
        if (left.key.dimension_id != right.key.dimension_id) {
            return left.key.dimension_id < right.key.dimension_id;
        }
        if (left.key.chunk_x != right.key.chunk_x) return left.key.chunk_x < right.key.chunk_x;
        if (left.key.chunk_y != right.key.chunk_y) return left.key.chunk_y < right.key.chunk_y;
        if (left.key.chunk_z != right.key.chunk_z) return left.key.chunk_z < right.key.chunk_z;
        return left.anchor_id < right.anchor_id;
    });

    uint32_t transitions = 0;
    for (const WorkItem& item : work) {
        if (transitions >= limits_.max_stage_transitions_per_tick) break;
        if (item.sidecar == nullptr ||
            item.record_index >= item.sidecar->tree_growth_records.size()) {
            continue;
        }
        TreeGrowthPersistenceRecord& record = item.sidecar->tree_growth_records[item.record_index];
        if (try_grow_tree(item.key, *item.sidecar, record, current_tick)) ++transitions;
    }
}

bool GameTreeGrowthSystem::try_grow_tree(
    const ChunkKey& key,
    GameChunkSidecar& sidecar,
    TreeGrowthPersistenceRecord& record,
    uint64_t current_tick) {
    if (record.growth_stage != TreeGrowthStage::SAPLING &&
        record.growth_stage != TreeGrowthStage::YOUNG) {
        return false;
    }
    if (tree_record_count(sidecar, record.anchor_entity_id) != 1) return false;
    BlockEntityPlacement* anchor = find_tree_anchor(sidecar, record.anchor_entity_id);
    if (anchor == nullptr ||
        floor_divide(anchor->root_x, snt::voxel::VoxelChunk::kChunkSize) != key.chunk_x ||
        floor_divide(anchor->root_y, snt::voxel::VoxelChunk::kChunkSize) != key.chunk_y ||
        floor_divide(anchor->root_z, snt::voxel::VoxelChunk::kChunkSize) != key.chunk_z) {
        return false;
    }
    const TreeSpeciesDef* species = worldgen_config_->find_tree_species(record.species_key);
    if (species == nullptr || current_tick < record.last_growth_tick) return false;
    const uint64_t wait_ticks = record.growth_stage == TreeGrowthStage::SAPLING
        ? required_ticks(species->ticks_to_young)
        : required_ticks(species->ticks_to_mature);
    if (current_tick - record.last_growth_tick < wait_ticks) return false;

    if (environment_provider_ != nullptr) {
        TreeGrowthEnvironment environment;
        if (environment_provider_->sample_tree_growth_environment(
                key.dimension_id, anchor->root_x, anchor->root_y, anchor->root_z, environment) &&
            (environment.temperature < species->temperature_min ||
             environment.temperature > species->temperature_max ||
             environment.humidity < species->humidity_min ||
             environment.humidity > species->humidity_max)) {
            return false;
        }
    }

    const TerrainMaterialId wood_material = worldgen_config_->material_id_or(
        species->wood_material_key, worldgen_config_->roles.air);
    const TerrainMaterialId leaves_material = worldgen_config_->material_id_or(
        species->leaves_material_key, worldgen_config_->roles.air);
    if (wood_material == worldgen_config_->roles.air ||
        leaves_material == worldgen_config_->roles.air) {
        return false;
    }

    std::vector<TreeGrowthOwnedCell> layout;
    const TreeGrowthStage next_stage = record.growth_stage == TreeGrowthStage::SAPLING
        ? TreeGrowthStage::YOUNG
        : TreeGrowthStage::MATURE;
    const bool built = next_stage == TreeGrowthStage::YOUNG
        ? build_young_layout(*anchor, wood_material, leaves_material, layout)
        : build_mature_layout(*anchor, *species, wood_material, leaves_material, layout);
    if (!built || layout.empty() || !can_apply_layout(key.dimension_id, record, layout)) {
        return false;
    }
    return apply_layout(key.dimension_id, *anchor, record, std::move(layout), next_stage,
                        current_tick);
}

bool GameTreeGrowthSystem::build_young_layout(
    const BlockEntityPlacement& anchor,
    TerrainMaterialId wood_material,
    TerrainMaterialId leaves_material,
    std::vector<TreeGrowthOwnedCell>& out_layout) const {
    out_layout.clear();
    for (int32_t offset = 0; offset < 2; ++offset) {
        if (!append_layout_cell(out_layout, anchor.root_x,
                                static_cast<int64_t>(anchor.root_y) + offset,
                                anchor.root_z, wood_material)) {
            return false;
        }
    }
    const int64_t canopy_y = static_cast<int64_t>(anchor.root_y) + 2;
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (std::abs(dx) + std::abs(dz) > 1) continue;
            if (!append_layout_cell(out_layout, static_cast<int64_t>(anchor.root_x) + dx,
                                    canopy_y, static_cast<int64_t>(anchor.root_z) + dz,
                                    leaves_material)) {
                return false;
            }
        }
    }
    return true;
}

bool GameTreeGrowthSystem::build_mature_layout(
    const BlockEntityPlacement& anchor,
    const TreeSpeciesDef& species,
    TerrainMaterialId wood_material,
    TerrainMaterialId leaves_material,
    std::vector<TreeGrowthOwnedCell>& out_layout) const {
    if (species.min_trunk_height <= 0 ||
        species.min_trunk_height > kMaxSupportedTrunkHeight ||
        species.canopy_radius < 0 || species.canopy_radius > kMaxSupportedCanopyRadius) {
        return false;
    }
    out_layout.clear();
    for (int32_t offset = 0; offset < species.min_trunk_height; ++offset) {
        if (!append_layout_cell(out_layout, anchor.root_x,
                                static_cast<int64_t>(anchor.root_y) + offset,
                                anchor.root_z, wood_material)) {
            return false;
        }
    }

    const int32_t radius = species.canopy_radius;
    const int64_t base_y = static_cast<int64_t>(anchor.root_y) + species.min_trunk_height;
    const auto add_leaf = [&](int64_t x, int64_t y, int64_t z) {
        return append_layout_cell(out_layout, x, y, z, leaves_material);
    };
    switch (species.canopy_shape) {
        case CanopyShape::SPHERE:
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dz = -radius; dz <= radius; ++dz) {
                    for (int32_t dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dy) + std::abs(dz) > radius + 1 ||
                            !add_leaf(static_cast<int64_t>(anchor.root_x) + dx, base_y + dy,
                                      static_cast<int64_t>(anchor.root_z) + dz)) {
                            return false;
                        }
                    }
                }
            }
            break;
        case CanopyShape::CONE:
            for (int32_t layer = 0; layer <= radius; ++layer) {
                const int32_t layer_radius = radius - layer;
                for (int32_t dz = -layer_radius; dz <= layer_radius; ++dz) {
                    for (int32_t dx = -layer_radius; dx <= layer_radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > layer_radius ||
                            !add_leaf(static_cast<int64_t>(anchor.root_x) + dx, base_y + layer,
                                      static_cast<int64_t>(anchor.root_z) + dz)) {
                            return false;
                        }
                    }
                }
            }
            break;
        case CanopyShape::UMBRELLA:
            for (int32_t dy = 0; dy <= 1; ++dy) {
                const int32_t layer_radius = dy == 0 ? radius : std::max(0, radius - 1);
                for (int32_t dz = -layer_radius; dz <= layer_radius; ++dz) {
                    for (int32_t dx = -layer_radius; dx <= layer_radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > layer_radius + 1 ||
                            !add_leaf(static_cast<int64_t>(anchor.root_x) + dx, base_y + dy,
                                      static_cast<int64_t>(anchor.root_z) + dz)) {
                            return false;
                        }
                    }
                }
            }
            break;
        case CanopyShape::COLUMN:
            for (int32_t dy = 0; dy < 3; ++dy) {
                for (int32_t dz = -radius; dz <= radius; ++dz) {
                    for (int32_t dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > radius ||
                            !add_leaf(static_cast<int64_t>(anchor.root_x) + dx, base_y + dy,
                                      static_cast<int64_t>(anchor.root_z) + dz)) {
                            return false;
                        }
                    }
                }
            }
            break;
        case CanopyShape::COUNT:
            return false;
    }
    return true;
}

bool GameTreeGrowthSystem::can_apply_layout(
    std::string_view dimension_id,
    const TreeGrowthPersistenceRecord& record,
    const std::vector<TreeGrowthOwnedCell>& layout) const {
    if (chunks_ == nullptr || worldgen_config_ == nullptr) return false;
    for (const TreeGrowthOwnedCell& desired : layout) {
        const ResolvedCell resolved = resolve_cell(*chunks_, dimension_id, desired.block_x,
                                                   desired.block_y, desired.block_z);
        if (resolved.cell == nullptr || resolved.cell->has_fluid()) return false;
        if (resolved.cell->material == worldgen_config_->roles.air ||
            is_record_owned_cell(record, desired.block_x, desired.block_y, desired.block_z,
                                 static_cast<TerrainMaterialId>(resolved.cell->material))) {
            continue;
        }
        return false;
    }
    return true;
}

bool GameTreeGrowthSystem::apply_layout(
    std::string_view dimension_id,
    BlockEntityPlacement& anchor,
    TreeGrowthPersistenceRecord& record,
    std::vector<TreeGrowthOwnedCell> layout,
    TreeGrowthStage new_stage,
    uint64_t current_tick) {
    if (worldgen_config_ == nullptr) return false;
    for (const TreeGrowthOwnedCell& old_cell : record.owned_cells) {
        if (layout_contains(layout, old_cell)) continue;
        const ResolvedCell current = resolve_cell(*chunks_, dimension_id, old_cell.block_x,
                                                  old_cell.block_y, old_cell.block_z);
        if (current.cell == nullptr || current.cell->material != old_cell.material) continue;
        if (!write_cell(dimension_id, old_cell.block_x, old_cell.block_y, old_cell.block_z,
                        worldgen_config_->roles.air)) {
            return false;
        }
    }
    for (const TreeGrowthOwnedCell& desired : layout) {
        if (!write_cell(dimension_id, desired.block_x, desired.block_y, desired.block_z,
                        desired.material)) {
            return false;
        }
    }
    anchor.owned_cell_count = static_cast<uint32_t>(layout.size());
    record.owned_cells = std::move(layout);
    record.growth_stage = new_stage;
    record.last_growth_tick = current_tick;
    return true;
}

bool GameTreeGrowthSystem::write_cell(std::string_view dimension_id,
                                      int32_t block_x,
                                      int32_t block_y,
                                      int32_t block_z,
                                      TerrainMaterialId material) {
    if (chunks_ == nullptr || worldgen_config_ == nullptr) return false;
    const ResolvedCell resolved = resolve_cell(*chunks_, dimension_id, block_x, block_y, block_z);
    if (resolved.cell == nullptr) return false;
    const snt::voxel::TerrainCell previous = *resolved.cell;
    resolved.cell->material = material;
    resolved.cell->flags = worldgen_config_->flags_for_material(material);
    if (previous.material != resolved.cell->material || previous.flags != resolved.cell->flags) {
        emit_terrain_change(dimension_id, block_x, block_y, block_z, previous, *resolved.cell);
    }
    return true;
}

void GameTreeGrowthSystem::emit_terrain_change(
    std::string_view dimension_id,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    const snt::voxel::TerrainCell& previous,
    const snt::voxel::TerrainCell& current) const {
    if (mutation_sink_ == nullptr) return;
    mutation_sink_->on_tree_growth_terrain_changed({
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
