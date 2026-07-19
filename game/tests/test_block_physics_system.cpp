// Game-owned deterministic terrain physics regression coverage.

#include "game/simulation/block_physics_system.h"

#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

constexpr uint32_t kSolid = static_cast<uint32_t>(snt::voxel::TF_SOLID);
constexpr uint32_t kGravityFlags =
    kSolid | static_cast<uint32_t>(snt::voxel::TF_GRAVITY_FALL);
constexpr uint32_t kCollapseFlags =
    kSolid | static_cast<uint32_t>(snt::voxel::TF_COLLAPSE_RISK);
constexpr uint32_t kSupportBeamFlags =
    kSolid | static_cast<uint32_t>(snt::voxel::TF_SUPPORT_BEAM);

void add_empty_chunk(snt::voxel::ChunkRegistry& chunks) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));
}

snt::game::WorldGenConfigSnapshot make_worldgen_config() {
    snt::game::WorldGenConfigSnapshot config;
    const auto add_material = [&config](snt::game::TerrainMaterialId id,
                                        const char* key,
                                        uint32_t flags,
                                        float collapse_chance = 0.3f) {
        snt::game::TerrainMaterialDef material;
        material.id = id;
        material.key = key;
        material.flags = flags;
        material.collapse_chance = collapse_chance;
        config.material_ids_by_key[material.key] = material.id;
        config.material_keys_by_id[material.id] = material.key;
        config.materials.push_back(std::move(material));
    };
    add_material(0, "air", 0, 0.0f);
    add_material(1, "sand", kGravityFlags);
    add_material(2, "unstable_stone", kCollapseFlags, 1.0f);
    add_material(3, "support_beam", kSupportBeamFlags);
    add_material(4, "stone", kSolid);
    config.roles.air = 0;
    return config;
}

struct RecordingMutationSink final : snt::game::IBlockPhysicsMutationSink {
    std::vector<snt::game::BlockPhysicsTerrainChange> changes;

    void on_block_physics_terrain_changed(
        const snt::game::BlockPhysicsTerrainChange& change) override {
        changes.push_back(change);
    }
};

}  // namespace

TEST(GameBlockPhysicsSystemTest, SchedulesTheMutatedGravityCellAndEmitsBothChanges) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameplayConfig gameplay;
    gameplay.enable_collapse = false;
    snt::game::GameBlockPhysicsSystem physics(chunks, worldgen, gameplay);
    RecordingMutationSink changes;
    physics.set_mutation_sink(&changes);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(1, 2, 1, 1, kGravityFlags);

    physics.schedule_after_terrain_mutation("overworld", 1, 2, 1, 0);
    physics.tick(1);

    EXPECT_EQ(chunk->terrain.cell_at(1, 2, 1).material, 0u);
    EXPECT_EQ(chunk->terrain.cell_at(1, 1, 1).material, 1u);
    ASSERT_EQ(changes.changes.size(), 2u);
    EXPECT_EQ(changes.changes[0].previous_material, 1u);
    EXPECT_EQ(changes.changes[0].current_material, 0u);
    EXPECT_EQ(changes.changes[1].previous_material, 0u);
    EXPECT_EQ(changes.changes[1].current_material, 1u);
}

TEST(GameBlockPhysicsSystemTest, CollapsesUsingTheOriginalMaterialAtTheSettledCell) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameplayConfig gameplay;
    gameplay.enable_gravity_fall = false;
    gameplay.collapse_chance_multiplier = 1.0f;
    snt::game::GameBlockPhysicsSystem physics(chunks, worldgen, gameplay);
    RecordingMutationSink changes;
    physics.set_mutation_sink(&changes);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(2, 0, 2, 4, kSolid);
    chunk->terrain.set_cell(2, 4, 2, 2, kCollapseFlags);

    physics.schedule_after_terrain_mutation("overworld", 2, 4, 2, 1);
    physics.tick(1);
    physics.tick(2);
    physics.tick(3);

    EXPECT_EQ(chunk->terrain.cell_at(2, 4, 2).material, 0u);
    EXPECT_EQ(chunk->terrain.cell_at(2, 1, 2).material, 2u);
    ASSERT_EQ(changes.changes.size(), 2u);
    EXPECT_EQ(changes.changes[0].current_material, 0u);
    EXPECT_EQ(changes.changes[1].current_material, 2u);
}

TEST(GameBlockPhysicsSystemTest, SupportBeamPreventsNearbyCollapse) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameplayConfig gameplay;
    gameplay.enable_gravity_fall = false;
    gameplay.collapse_chance_multiplier = 1.0f;
    gameplay.support_beam_radius = 1;
    snt::game::GameBlockPhysicsSystem physics(chunks, worldgen, gameplay);
    RecordingMutationSink changes;
    physics.set_mutation_sink(&changes);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(3, 3, 3, 2, kCollapseFlags);
    chunk->terrain.set_cell(4, 3, 3, 3, kSupportBeamFlags);

    physics.schedule_after_terrain_mutation("overworld", 3, 3, 3, 0);
    physics.tick(2);

    EXPECT_EQ(chunk->terrain.cell_at(3, 3, 3).material, 2u);
    EXPECT_TRUE(changes.changes.empty());
}
