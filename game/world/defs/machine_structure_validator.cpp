// Game-owned voxel structure validation implementation.

#include "game/world/defs/machine_structure_validator.h"

#include <cstdint>

namespace snt::game {
namespace {

[[nodiscard]] int32_t floor_div_chunk(int32_t block_coordinate) {
    constexpr int64_t kChunkSize = VoxelChunk::kChunkSize;
    const int64_t value = block_coordinate;
    return static_cast<int32_t>(value >= 0 ? value / kChunkSize
                                           : -((-value + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] MachineStructureCheck failure(
    MachineStructureFailure kind,
    int32_t block_x,
    int32_t block_y,
    int32_t block_z,
    TerrainMaterialId actual_material = 0) {
    return {
        .failure = kind,
        .block_x = block_x,
        .block_y = block_y,
        .block_z = block_z,
        .actual_material = actual_material,
    };
}

}  // namespace

MachineStructureCheck MachineStructureValidator::validate_bloomery(
    const ChunkRegistry& chunks,
    const std::string& dimension_id,
    int32_t controller_x,
    int32_t controller_y,
    int32_t controller_z,
    TerrainMaterialId bloomery_material) {
    if (bloomery_material == 0) {
        return failure(MachineStructureFailure::InvalidExpectedMaterial,
                       controller_x, controller_y, controller_z);
    }

    for (int32_t offset_y = 0; offset_y < 2; ++offset_y) {
        for (int32_t offset_x = -1; offset_x <= 1; ++offset_x) {
            for (int32_t offset_z = -1; offset_z <= 1; ++offset_z) {
                if (offset_x == 0 && offset_y == 0 && offset_z == 0) continue;

                const int32_t block_x = controller_x + offset_x;
                const int32_t block_y = controller_y + offset_y;
                const int32_t block_z = controller_z + offset_z;
                const int32_t chunk_x = floor_div_chunk(block_x);
                const int32_t chunk_y = floor_div_chunk(block_y);
                const int32_t chunk_z = floor_div_chunk(block_z);
                const int32_t local_x = block_x - chunk_x * VoxelChunk::kChunkSize;
                const int32_t local_y = block_y - chunk_y * VoxelChunk::kChunkSize;
                const int32_t local_z = block_z - chunk_z * VoxelChunk::kChunkSize;

                const VoxelChunk* chunk = chunks.get_chunk(
                    dimension_id, chunk_x, chunk_y, chunk_z);
                if (chunk == nullptr) {
                    return failure(MachineStructureFailure::MissingChunk,
                                   block_x, block_y, block_z);
                }
                if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) {
                    return failure(MachineStructureFailure::InvalidTerrainCell,
                                   block_x, block_y, block_z);
                }

                const TerrainMaterialId material =
                    static_cast<TerrainMaterialId>(
                        chunk->terrain.cell_at(local_x, local_y, local_z).material);
                if (material != bloomery_material) {
                    return failure(MachineStructureFailure::MaterialMismatch,
                                   block_x, block_y, block_z, material);
                }
            }
        }
    }
    return {};
}

}  // namespace snt::game
