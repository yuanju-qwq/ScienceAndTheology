// Game-owned bloomery structure validation tests.

#include <cstdint>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "game/world/defs/machine_structure_validator.h"

namespace {

constexpr snt::game::TerrainMaterialId kBloomeryMaterial = 17;

int32_t floor_div_chunk(int32_t coordinate) {
    constexpr int64_t kChunkSize = snt::game::VoxelChunk::kChunkSize;
    const int64_t value = coordinate;
    return static_cast<int32_t>(value >= 0 ? value / kChunkSize
                                           : -((-value + kChunkSize - 1) / kChunkSize));
}

void ensure_chunk(snt::game::ChunkRegistry& chunks,
                  const std::string& dimension_id,
                  int32_t chunk_x,
                  int32_t chunk_y,
                  int32_t chunk_z) {
    if (chunks.has_chunk(dimension_id, chunk_x, chunk_y, chunk_z)) return;

    snt::game::VoxelChunk chunk;
    chunk.terrain.resize(snt::game::VoxelChunk::kChunkSize,
                         snt::game::VoxelChunk::kChunkSize,
                         snt::game::VoxelChunk::kChunkSize);
    chunks.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z, std::move(chunk));
}

void set_material(snt::game::ChunkRegistry& chunks,
                  const std::string& dimension_id,
                  int32_t block_x,
                  int32_t block_y,
                  int32_t block_z,
                  snt::game::TerrainMaterialId material) {
    const int32_t chunk_x = floor_div_chunk(block_x);
    const int32_t chunk_y = floor_div_chunk(block_y);
    const int32_t chunk_z = floor_div_chunk(block_z);
    ensure_chunk(chunks, dimension_id, chunk_x, chunk_y, chunk_z);
    auto* chunk = chunks.get_chunk(dimension_id, chunk_x, chunk_y, chunk_z);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(
        block_x - chunk_x * snt::game::VoxelChunk::kChunkSize,
        block_y - chunk_y * snt::game::VoxelChunk::kChunkSize,
        block_z - chunk_z * snt::game::VoxelChunk::kChunkSize,
        material);
}

void build_bloomery_shell(snt::game::ChunkRegistry& chunks,
                          const std::string& dimension_id,
                          int32_t controller_x,
                          int32_t controller_y,
                          int32_t controller_z) {
    for (int32_t offset_y = 0; offset_y < 2; ++offset_y) {
        for (int32_t offset_x = -1; offset_x <= 1; ++offset_x) {
            for (int32_t offset_z = -1; offset_z <= 1; ++offset_z) {
                if (offset_x == 0 && offset_y == 0 && offset_z == 0) continue;
                set_material(chunks, dimension_id,
                             controller_x + offset_x,
                             controller_y + offset_y,
                             controller_z + offset_z,
                             kBloomeryMaterial);
            }
        }
    }
}

}  // namespace

TEST(MachineStructureValidatorTest, AcceptsCompleteBloomeryAcrossChunkBoundaries) {
    snt::game::ChunkRegistry chunks;
    build_bloomery_shell(chunks, "overworld", 31, 4, 31);

    const auto result = snt::game::MachineStructureValidator::validate_bloomery(
        chunks, "overworld", 31, 4, 31, kBloomeryMaterial);

    EXPECT_TRUE(result.valid());
    EXPECT_EQ(result.failure, snt::game::MachineStructureFailure::None);
}

TEST(MachineStructureValidatorTest, IdentifiesMissingAndMismatchedBloomeryCells) {
    snt::game::ChunkRegistry chunks;
    build_bloomery_shell(chunks, "overworld", 10, 10, 10);
    set_material(chunks, "overworld", 11, 11, 11, 3);

    const auto mismatched = snt::game::MachineStructureValidator::validate_bloomery(
        chunks, "overworld", 10, 10, 10, kBloomeryMaterial);
    EXPECT_FALSE(mismatched.valid());
    EXPECT_EQ(mismatched.failure, snt::game::MachineStructureFailure::MaterialMismatch);
    EXPECT_EQ(mismatched.block_x, 11);
    EXPECT_EQ(mismatched.block_y, 11);
    EXPECT_EQ(mismatched.block_z, 11);
    EXPECT_EQ(mismatched.actual_material, 3);

    snt::game::ChunkRegistry missing_chunks;
    const auto missing = snt::game::MachineStructureValidator::validate_bloomery(
        missing_chunks, "overworld", 10, 10, 10, kBloomeryMaterial);
    EXPECT_FALSE(missing.valid());
    EXPECT_EQ(missing.failure, snt::game::MachineStructureFailure::MissingChunk);
}

TEST(MachineStructureValidatorTest, RejectsAirAsAStructureMaterial) {
    snt::game::ChunkRegistry chunks;
    const auto result = snt::game::MachineStructureValidator::validate_bloomery(
        chunks, "overworld", 0, 0, 0, 0);

    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.failure, snt::game::MachineStructureFailure::InvalidExpectedMaterial);
}
