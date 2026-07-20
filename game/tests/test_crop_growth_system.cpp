// Game-owned crop and farmland regression coverage.

#include "game/simulation/crop_growth_system.h"

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
    const auto add_material = [&config](const char* key,
                                        uint32_t flags) {
        snt::game::TerrainMaterialDef material;
        material.key = key;
        material.flags = flags;
        config.materials.push_back(std::move(material));
    };
    add_material("snt:air", 0);
    add_material("snt:dirt", kSolid | kMineable);
    add_material("snt:farmland", kSolid | kMineable);
    add_material("snt:wheat_seed", kMineable);
    add_material("snt:wheat_sprout", kMineable);
    add_material("snt:wheat_growing", kMineable);
    add_material("snt:wheat_mature", kMineable);
    config.role_keys.air = "snt:air";
    config.role_keys.dirt = "snt:dirt";
    config.runtime_material_keys.farmland = "snt:farmland";
    EXPECT_TRUE(snt::game::finalize_world_gen_config(config));

    snt::game::CropSpeciesDef wheat;
    wheat.species_key = "wheat";
    wheat.stage_material_keys[0] = "snt:wheat_seed";
    wheat.stage_material_keys[1] = "snt:wheat_sprout";
    wheat.stage_material_keys[2] = "snt:wheat_growing";
    wheat.stage_material_keys[3] = "snt:wheat_mature";
    wheat.ticks_seed_to_sprout = 1;
    wheat.ticks_sprout_to_growing = 1;
    wheat.ticks_growing_to_mature = 1;
    wheat.regrow_ticks = 2;
    wheat.fertility_sensitivity = 0.0f;
    wheat.water_sensitivity = 0.0f;
    wheat.crop_item_key = "crop.wheat";
    wheat.crop_min = 2;
    wheat.byproduct_item_key = "seed.wheat";
    wheat.byproduct_count = 1;
    config.crop_species.push_back(std::move(wheat));
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

struct RecordingMutationSink final : snt::game::ICropGrowthMutationSink {
    std::vector<snt::game::CropGrowthTerrainChange> changes;

    void on_crop_growth_terrain_changed(
        const snt::game::CropGrowthTerrainChange& change) override {
        changes.push_back(change);
    }
};

struct TooColdEnvironment final : snt::game::ICropGrowthEnvironmentProvider {
    bool sample_crop_growth_environment(std::string_view,
                                        int32_t,
                                        int32_t,
                                        int32_t,
                                        snt::game::CropGrowthEnvironment& out_environment) const override {
        out_environment.temperature = -2.0f;
        out_environment.humidity = 0.0f;
        return true;
    }
};

void prepare_farmland_and_crop(snt::game::GameCropGrowthSystem& crops,
                               snt::voxel::ChunkRegistry& chunks,
                               const snt::game::WorldGenConfigSnapshot& config,
                               int32_t block_x,
                               int32_t block_z) {
    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    chunk->terrain.set_cell(
        block_x, 0, block_z, config.roles.dirt, kSolid | kMineable);
    ASSERT_TRUE(crops.till_farmland({
        .dimension_id = "overworld",
        .block_x = block_x,
        .block_y = 0,
        .block_z = block_z,
    }, 0));
    ASSERT_TRUE(crops.plant_crop({
        .dimension_id = "overworld",
        .block_x = block_x,
        .block_y = 1,
        .block_z = block_z,
        .species_key = "wheat",
    }, 0));
}

}  // namespace

TEST(GameCropGrowthSystemTest, TillsPlantsAndGrowsActiveCropsWithTerrainDeltas) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    const snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    snt::game::GameCropGrowthSystem crops(chunks, sidecars, config);
    RecordingMutationSink changes;
    crops.set_mutation_sink(&changes);
    prepare_farmland_and_crop(crops, chunks, config, 10, 10);
    changes.changes.clear();

    crops.tick(1, snt::game::Season::SPRING);
    crops.tick(2, snt::game::Season::SPRING);
    crops.tick(3, snt::game::Season::SPRING);

    const auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->farmland_records.size(), 1u);
    ASSERT_EQ(sidecar->crop_growth_records.size(), 1u);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::MATURE);
    EXPECT_FLOAT_EQ(sidecar->farmland_records.front().moisture, 0.497f);
    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->terrain.cell_at(10, 1, 10).material,
              config.material_id_or("snt:wheat_mature", 0));
    ASSERT_EQ(changes.changes.size(), 3u);
    EXPECT_EQ(changes.changes.back().current_material,
              config.material_id_or("snt:wheat_mature", 0));
}

TEST(GameCropGrowthSystemTest, UsesAuthoritativeSeasonAndOptionalEnvironmentProvider) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    config.crop_species.front().grow_season = static_cast<int>(snt::game::Season::SUMMER);
    snt::game::GameCropGrowthSystem crops(chunks, sidecars, config);
    prepare_farmland_and_crop(crops, chunks, config, 8, 8);

    crops.tick(1, snt::game::Season::WINTER);
    const auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::SEED);

    TooColdEnvironment environment;
    crops.set_environment_provider(&environment);
    crops.tick(4, snt::game::Season::SUMMER);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::SEED);

    crops.set_environment_provider(nullptr);
    crops.tick(5, snt::game::Season::SUMMER);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::SPROUT);
}

TEST(GameCropGrowthSystemTest, HarvestsAndRegrowsRepeatHarvestCrops) {
    snt::voxel::ChunkRegistry chunks;
    add_active_chunk(chunks);
    snt::game::GameChunkSidecarRegistry sidecars;
    sidecars.set({"overworld", 0, 0, 0}, {});
    snt::game::WorldGenConfigSnapshot config = make_worldgen_config();
    config.crop_species.front().repeat_harvest = true;
    snt::game::GameCropGrowthSystem crops(chunks, sidecars, config);
    prepare_farmland_and_crop(crops, chunks, config, 6, 6);
    crops.tick(1, snt::game::Season::SPRING);
    crops.tick(2, snt::game::Season::SPRING);
    crops.tick(3, snt::game::Season::SPRING);

    const auto harvest = crops.harvest_crop({
        .dimension_id = "overworld",
        .block_x = 6,
        .block_y = 1,
        .block_z = 6,
    }, 4);
    ASSERT_TRUE(harvest) << harvest.error().format();
    EXPECT_EQ(harvest->crop_item_key, "crop.wheat");
    EXPECT_EQ(harvest->crop_count, 2);
    EXPECT_EQ(harvest->byproduct_item_key, "seed.wheat");
    EXPECT_EQ(harvest->byproduct_count, 1);
    EXPECT_TRUE(harvest->regrowing);

    auto* sidecar = sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->crop_growth_records.size(), 1u);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::GROWING);
    EXPECT_TRUE(sidecar->crop_growth_records.front().is_regrowing);
    EXPECT_FLOAT_EQ(sidecar->farmland_records.front().fertility, 0.55f);

    crops.tick(5, snt::game::Season::SPRING);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::GROWING);
    crops.tick(6, snt::game::Season::SPRING);
    EXPECT_EQ(sidecar->crop_growth_records.front().growth_stage,
              snt::game::CropGrowthStage::MATURE);
}

TEST(GameCropGrowthSystemTest, PersistsTypedFarmlandAndCropRecords) {
    snt::game::GameChunk source;
    source.chunk_x = 0;
    source.chunk_y = 0;
    source.chunk_z = 0;
    source.terrain.resize(1, 1, 1);
    source.block_entities.push_back({
        .id = {41},
        .entity_type = snt::game::BlockEntityType::FARMLAND,
        .root_x = 1,
        .root_y = 2,
        .root_z = 3,
        .owned_cell_count = 1,
    });
    source.block_entities.push_back({
        .id = {42},
        .entity_type = snt::game::BlockEntityType::CROP,
        .root_x = 1,
        .root_y = 3,
        .root_z = 3,
        .owned_cell_count = 1,
    });
    source.farmland_records.push_back({
        .anchor_entity_id = {41},
        .moisture = 0.25f,
        .fertility = 0.75f,
        .last_crop_key = "wheat",
        .consecutive_same_crop = 3,
        .last_moisture_tick = 17,
    });
    source.crop_growth_records.push_back({
        .anchor_entity_id = {42},
        .farmland_anchor_entity_id = {41},
        .species_key = "wheat",
        .growth_stage = snt::game::CropGrowthStage::GROWING,
        .planted_tick = 11,
        .last_growth_tick = 29,
        .last_harvest_tick = 23,
        .is_regrowing = true,
        .owned_cells = {{.block_x = 1, .block_y = 3, .block_z = 3, .material = 5}},
    });

    const snt::game::GameChunkSerializer serializer;
    const std::vector<uint8_t> bytes = serializer.serialize("overworld", source);
    std::string dimension_id;
    snt::game::GameChunk restored;
    ASSERT_TRUE(serializer.deserialize(bytes, dimension_id, restored));
    ASSERT_EQ(restored.farmland_records.size(), 1u);
    ASSERT_EQ(restored.crop_growth_records.size(), 1u);
    EXPECT_EQ(dimension_id, "overworld");
    EXPECT_FLOAT_EQ(restored.farmland_records.front().moisture, 0.25f);
    EXPECT_EQ(restored.farmland_records.front().last_crop_key, "wheat");
    const auto& crop = restored.crop_growth_records.front();
    EXPECT_EQ(crop.anchor_entity_id.id, 42u);
    EXPECT_EQ(crop.farmland_anchor_entity_id.id, 41u);
    EXPECT_EQ(crop.growth_stage, snt::game::CropGrowthStage::GROWING);
    EXPECT_TRUE(crop.is_regrowing);
    ASSERT_EQ(crop.owned_cells.size(), 1u);
    EXPECT_EQ(crop.owned_cells.front().material, 5u);
}
