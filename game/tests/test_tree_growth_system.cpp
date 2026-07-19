// Game-owned tree growth regression coverage.

#include "game/simulation/tree_growth_system.h"

#include "game/world/save/chunk_serializer.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

constexpr uint32_t kSolid = static_cast<uint32_t>(snt::voxel::TF_SOLID);
constexpr uint32_t kMineable = static_cast<uint32_t>(snt::voxel::TF_MINEABLE);

snt::game::WorldGenConfigSnapshot make_worldgen_config() {
    snt::game::WorldGenConfigSnapshot config;
    const auto add_material = [&config](snt::game::TerrainMaterialId id,
                                        const char* key,
                                        uint32_t flags) {
        snt::game::TerrainMaterialDef material;
        material.id = id;
        material.key = key;
        material.flags = flags;
        config.material_ids_by_key[material.key] = material.id;
        config.material_keys_by_id[material.id] = material.key;
        config.materials.push_back(std::move(material));
    };
    add_material(0, "air", 0);
    add_material(1, "dirt", kSolid | kMineable);
    add_material(2, "oak_sapling", kMineable);
    add_material(3, "oak_wood", kSolid | kMineable);
    add_material(4, "oak_leaves", kMineable);
    add_material(5, "stone", kSolid | kMineable);
    config.roles.air = 0;

    snt::game::TreeSpeciesDef oak;
    oak.species_key = "oak";
    oak.wood_material_key = "oak_wood";
    oak.leaves_material_key = "oak_leaves";
    oak.sapling_material_key = "oak_sapling";
    oak.min_trunk_height = 3;
    oak.max_trunk_height = 3;
    oak.canopy_shape = snt::game::CanopyShape::SPHERE;
    oak.canopy_radius = 1;
    oak.ticks_to_young = 1;
    oak.ticks_to_mature = 1;
    config.tree_species.push_back(std::move(oak));
    return config;
}

void add_active_chunk(snt::voxel::ChunkRegistry& chunks) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));
}

struct RecordingMutationSink final : snt::game::ITreeGrowthMutationSink {
    std::vector<snt::game::TreeGrowthTerrainChange> changes;

    void on_tree_growth_terrain_changed(
        const snt::game::TreeGrowthTerrainChange& change) override {
        changes.push_back(change);
    }
};

struct TooColdEnvironment final : snt::game::ITreeGrowthEnvironmentProvider {
    bool sample_tree_growth_environment(std::string_view,
                                        int32_t,
                                        int32_t,
                                        int32_t,
                                        snt::game::TreeGrowthEnvironment& out_environment) const override {
        out_environment.temperature = -2.0f;
        out_environment.humidity = 0.0f;
        return true;
    }
};

}  // namespace

TEST(GameTreeGrowthSystemTest, GrowsSaplingInActiveChunkAndEmitsCommittedTerrainChanges) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    const snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    snt::game::GameTreeGrowthSystem trees(chunks, sidecars, config);
    RecordingMutationSink changes;
    trees.set_mutation_sink(&changes);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(10, 0, 10, 1, kSolid | kMineable);
    const auto anchor = trees.plant_sapling({
        .dimension_id = "overworld",
        .block_x = 10,
        .block_y = 1,
        .block_z = 10,
        .species_key = "oak",
    }, 0);
    ASSERT_TRUE(anchor) << anchor.error().format();
    changes.changes.clear();

    trees.tick(1);

    const auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->tree_growth_records.size(), 1u);
    const auto& record = sidecar->tree_growth_records.front();
    EXPECT_EQ(record.anchor_entity_id, *anchor);
    EXPECT_EQ(record.growth_stage, snt::game::TreeGrowthStage::YOUNG);
    EXPECT_EQ(chunk->terrain.cell_at(10, 1, 10).material, 3u);
    EXPECT_EQ(chunk->terrain.cell_at(10, 2, 10).material, 3u);
    EXPECT_EQ(chunk->terrain.cell_at(10, 3, 10).material, 4u);
    EXPECT_EQ(chunk->terrain.cell_at(9, 3, 10).material, 4u);
    EXPECT_EQ(chunk->terrain.cell_at(11, 3, 10).material, 4u);
    EXPECT_EQ(chunk->terrain.cell_at(10, 3, 9).material, 4u);
    EXPECT_EQ(chunk->terrain.cell_at(10, 3, 11).material, 4u);
    EXPECT_EQ(changes.changes.size(), 7u);
}

TEST(GameTreeGrowthSystemTest, DoesNotGrowThroughForeignTerrain) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    const snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    snt::game::GameTreeGrowthSystem trees(chunks, sidecars, config);
    RecordingMutationSink changes;
    trees.set_mutation_sink(&changes);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(5, 0, 5, 1, kSolid | kMineable);
    ASSERT_TRUE(trees.plant_sapling({
        .dimension_id = "overworld",
        .block_x = 5,
        .block_y = 1,
        .block_z = 5,
        .species_key = "oak",
    }, 0));
    chunk->terrain.set_cell(5, 2, 5, 5, kSolid | kMineable);
    changes.changes.clear();

    trees.tick(1);

    const auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->tree_growth_records.size(), 1u);
    EXPECT_EQ(sidecar->tree_growth_records.front().growth_stage,
              snt::game::TreeGrowthStage::SAPLING);
    EXPECT_EQ(chunk->terrain.cell_at(5, 1, 5).material, 2u);
    EXPECT_EQ(chunk->terrain.cell_at(5, 2, 5).material, 5u);
    EXPECT_TRUE(changes.changes.empty());
}

TEST(GameTreeGrowthSystemTest, DefersClimateChecksToTheDeclaredEnvironmentProvider) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    config.tree_species.front().temperature_min = -1.0f;
    config.tree_species.front().temperature_max = 1.0f;
    snt::game::GameTreeGrowthSystem trees(chunks, sidecars, config);
    TooColdEnvironment environment;
    trees.set_environment_provider(&environment);

    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(8, 0, 8, 1, kSolid | kMineable);
    ASSERT_TRUE(trees.plant_sapling({
        .dimension_id = "overworld",
        .block_x = 8,
        .block_y = 1,
        .block_z = 8,
        .species_key = "oak",
    }, 0));

    trees.tick(1);

    const auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    EXPECT_EQ(sidecar->tree_growth_records.front().growth_stage,
              snt::game::TreeGrowthStage::SAPLING);
}

TEST(GameTreeGrowthSystemTest, PersistsTypedTreeGrowthRecord) {
    snt::game::GameChunk source;
    source.chunk_x = 0;
    source.chunk_y = 0;
    source.chunk_z = 0;
    source.terrain.resize(1, 1, 1);
    source.block_entities.push_back({
        .id = {42},
        .entity_type = snt::game::BlockEntityType::TREE,
        .root_x = 1,
        .root_y = 2,
        .root_z = 3,
        .owned_cell_count = 2,
    });
    source.tree_growth_records.push_back({
        .anchor_entity_id = {42},
        .species_key = "oak",
        .growth_stage = snt::game::TreeGrowthStage::YOUNG,
        .planted_tick = 17,
        .last_growth_tick = 29,
        .owned_cells = {
            {.block_x = 1, .block_y = 2, .block_z = 3, .material = 3},
            {.block_x = 1, .block_y = 3, .block_z = 3, .material = 4},
        },
    });

    const snt::game::GameChunkSerializer serializer;
    const std::vector<uint8_t> bytes = serializer.serialize("overworld", source);
    std::string dimension_id;
    snt::game::GameChunk restored;
    ASSERT_TRUE(serializer.deserialize(bytes, dimension_id, restored));
    ASSERT_EQ(restored.tree_growth_records.size(), 1u);
    const auto& record = restored.tree_growth_records.front();
    EXPECT_EQ(dimension_id, "overworld");
    EXPECT_EQ(record.anchor_entity_id.id, 42u);
    EXPECT_EQ(record.species_key, "oak");
    EXPECT_EQ(record.growth_stage, snt::game::TreeGrowthStage::YOUNG);
    EXPECT_EQ(record.planted_tick, 17u);
    EXPECT_EQ(record.last_growth_tick, 29u);
    ASSERT_EQ(record.owned_cells.size(), 2u);
    EXPECT_EQ(record.owned_cells[1].material, 4u);
}
