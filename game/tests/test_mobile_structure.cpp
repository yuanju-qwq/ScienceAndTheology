// Tests for game-owned mobile structure assembly, write-back, and snapshots.

#include "game/world/mobile/dynamic_structure.h"
#include "game/world/mobile/ship_local_grid.h"
#include "game/world/mobile/ship_structure.h"

#include <gtest/gtest.h>

#include <utility>

namespace {

using namespace snt::game;

constexpr const char* kDimension = "overworld";
constexpr int kChunkSize = VoxelChunk::kChunkSize;
constexpr TerrainMaterialId kHull = 9;
constexpr TerrainMaterialId kStone = 1;

VoxelChunk make_empty_chunk() {
    VoxelChunk chunk;
    chunk.terrain.resize(kChunkSize, kChunkSize, kChunkSize);
    return chunk;
}

WorldGenConfigSnapshot make_worldgen_config() {
    WorldGenConfigSnapshot config;
    config.roles.air = 0;
    return config;
}

bool is_hull(const TerrainCell& cell) {
    return static_cast<TerrainMaterialId>(cell.material) == kHull;
}

TEST(GameMobileStructureTest, AssembleClearsWorldAndRegistersEntity) {
    ChunkRegistry chunks;
    VoxelChunk chunk = make_empty_chunk();
    chunk.terrain.set_cell(2, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(2, 3, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(9, 9, 9, kStone, TF_SOLID | TF_MINEABLE);
    chunks.set_chunk(kDimension, 0, 0, 0, std::move(chunk));

    const WorldGenConfigSnapshot config = make_worldgen_config();
    DynamicStructureRegistry registry;
    AssembleOptions options;
    options.max_blocks = 16;
    options.clear_source_cells = true;

    const AssembleResult result = DynamicStructureAssembler::assemble_connected(
        chunks, registry, &config, kDimension, 2, 2, 2, is_hull, options);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.hit_block_limit);
    EXPECT_EQ(result.block_count, 3u);
    EXPECT_EQ(result.terrain_deltas.size(), 3u);
    EXPECT_EQ(registry.count(), 1u);

    const DynamicStructureEntity* entity = registry.get(result.structure_id);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->snapshot.block_count(), 3u);
    EXPECT_DOUBLE_EQ(entity->mass, 3.0);
    EXPECT_TRUE(entity->moving);
    EXPECT_DOUBLE_EQ(entity->transform.position_x, 2.0);
    EXPECT_DOUBLE_EQ(entity->transform.position_y, 2.0);
    EXPECT_DOUBLE_EQ(entity->transform.position_z, 2.0);

    const VoxelChunk* out = chunks.get_chunk(kDimension, 0, 0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->terrain.cell_at(2, 2, 2).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(3, 2, 2).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(2, 3, 2).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(9, 9, 9).material, kStone);
}

TEST(GameMobileStructureTest, TransformSnapshotIsOneEntityPayload) {
    DynamicStructureEntity entity;
    entity.id = 42;
    entity.structure_version = 7;
    entity.transform.dimension_id = kDimension;
    entity.transform.position_x = 100.0;
    entity.transform.position_y = 200.0;
    entity.transform.position_z = 300.0;
    entity.transform.velocity_x = 10.0;

    const DynamicStructureTransformSnapshot snapshot =
        DynamicStructureAssembler::make_transform_snapshot(entity, 1234);

    EXPECT_EQ(snapshot.structure_id, 42u);
    EXPECT_EQ(snapshot.structure_version, 7u);
    EXPECT_EQ(snapshot.tick, 1234);
    EXPECT_DOUBLE_EQ(snapshot.transform.position_x, 100.0);
    EXPECT_DOUBLE_EQ(snapshot.transform.velocity_x, 10.0);
}

TEST(GameMobileStructureTest, DisassembleWritesBackAtNewTransform) {
    ChunkRegistry chunks;
    VoxelChunk chunk = make_empty_chunk();
    chunk.terrain.set_cell(2, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunks.set_chunk(kDimension, 0, 0, 0, std::move(chunk));

    const WorldGenConfigSnapshot config = make_worldgen_config();
    DynamicStructureRegistry registry;
    const AssembleResult assembled = DynamicStructureAssembler::assemble_connected(
        chunks, registry, &config, kDimension, 2, 2, 2, is_hull);
    ASSERT_TRUE(assembled.success) << assembled.error;

    DynamicStructureEntity* entity = registry.get(assembled.structure_id);
    ASSERT_NE(entity, nullptr);
    entity->transform.position_x = 10.0;
    entity->transform.position_y = 2.0;
    entity->transform.position_z = 2.0;

    DisassembleOptions options;
    options.remove_entity = true;
    options.require_air_destination = true;
    const DisassembleResult disassembled = DynamicStructureAssembler::disassemble_to_world(
        chunks, registry, &config, assembled.structure_id, options);

    ASSERT_TRUE(disassembled.success) << disassembled.error;
    EXPECT_EQ(disassembled.block_count, 2u);
    EXPECT_EQ(disassembled.terrain_deltas.size(), 2u);
    EXPECT_EQ(registry.count(), 0u);

    const VoxelChunk* out = chunks.get_chunk(kDimension, 0, 0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->terrain.cell_at(2, 2, 2).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(3, 2, 2).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(10, 2, 2).material, kHull);
    EXPECT_EQ(out->terrain.cell_at(11, 2, 2).material, kHull);
}

TEST(GameMobileStructureTest, BlockLimitFailsWithoutMutatingWorld) {
    ChunkRegistry chunks;
    VoxelChunk chunk = make_empty_chunk();
    chunk.terrain.set_cell(1, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(2, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    chunks.set_chunk(kDimension, 0, 0, 0, std::move(chunk));

    const WorldGenConfigSnapshot config = make_worldgen_config();
    DynamicStructureRegistry registry;
    AssembleOptions options;
    options.max_blocks = 2;
    options.clear_source_cells = true;

    const AssembleResult result = DynamicStructureAssembler::assemble_connected(
        chunks, registry, &config, kDimension, 1, 1, 1, is_hull, options);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.hit_block_limit);
    EXPECT_EQ(registry.count(), 0u);

    const VoxelChunk* out = chunks.get_chunk(kDimension, 0, 0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->terrain.cell_at(1, 1, 1).material, kHull);
    EXPECT_EQ(out->terrain.cell_at(2, 1, 1).material, kHull);
    EXPECT_EQ(out->terrain.cell_at(3, 1, 1).material, kHull);
}

TEST(GameMobileStructureTest, ShipServiceUsesExplicitGameOwnedDependencies) {
    ChunkRegistry chunks;
    VoxelChunk chunk = make_empty_chunk();
    chunk.terrain.set_cell(4, 4, 4, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(5, 4, 4, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(6, 4, 4, kStone, TF_SOLID | TF_MINEABLE);
    chunks.set_chunk(kDimension, 0, 0, 0, std::move(chunk));

    const WorldGenConfigSnapshot config = make_worldgen_config();
    DynamicStructureRegistry registry;
    ShipAssembleOptions options;
    options.max_blocks = 8;
    options.allowed_materials = {kHull};

    const ShipAssembleResult result = ShipStructureService::assemble_ship_from_world(
        chunks, registry, &config, 77, kDimension, 4, 4, 4, options);

    ASSERT_TRUE(result.base.success) << result.base.error;
    EXPECT_EQ(result.base.block_count, 2u);
    EXPECT_EQ(registry.count(), 1u);
    EXPECT_EQ(result.transform_snapshot.tick, 77);

    const VoxelChunk* out = chunks.get_chunk(kDimension, 0, 0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->terrain.cell_at(4, 4, 4).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(5, 4, 4).material, 0u);
    EXPECT_EQ(out->terrain.cell_at(6, 4, 4).material, kStone);
}

TEST(GameMobileStructureTest, ShipLocalGridUpdatesSnapshotVersion) {
    DynamicStructureEntity entity;
    entity.id = 1;
    entity.snapshot.blocks.push_back({0, 0, 0, kHull, TF_SOLID});
    entity.snapshot.blocks.push_back({1, 0, 0, kHull, TF_SOLID});
    entity.mass = 2.0;

    ShipLocalGrid grid(entity);
    EXPECT_EQ(grid.block_count(), 2u);
    const TerrainCell* cell = grid.get_cell(1, 0, 0);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->material, kHull);

    const uint32_t before = grid.structure_version();
    EXPECT_TRUE(grid.set_cell(1, 0, 0, kStone, TF_SOLID | TF_MINEABLE));
    EXPECT_EQ(grid.structure_version(), before + 1);
    cell = grid.get_cell(1, 0, 0);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->material, kStone);
    EXPECT_TRUE(entity.dirty_mesh);
}

}  // namespace
