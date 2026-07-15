// Tests for ScienceAndTheology-owned world rules and persistence sidecars.

#include "game/world/defs/block_entity_registry.h"
#include "game/world/defs/machine_collision_overlay.h"
#include "game/world/game_chunk.h"
#include "game/world/save/chunk_serializer.h"
#include "game/world/save/save_manager.h"
#include "game/world/save/world_persistence_lifecycle.h"
#include "game/worldgen/noise_generator.h"
#include "game/worldgen/terrain_generator.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/storage/region_file.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::shared_ptr<const snt::game::WorldGenConfigSnapshot> make_test_worldgen_config() {
    using namespace snt::game;

    auto config = std::make_shared<WorldGenConfigSnapshot>();
    TerrainMaterialDef air;
    air.id = 0;
    air.key = "air";
    config->materials.push_back(air);
    config->material_ids_by_key[air.key] = air.id;
    config->material_keys_by_id[air.id] = air.key;
    config->roles.air = air.id;

    TerrainMaterialDef stone;
    stone.id = 1;
    stone.key = "stone";
    stone.flags = TF_SOLID;
    config->materials.push_back(stone);
    config->material_ids_by_key[stone.key] = stone.id;
    config->material_keys_by_id[stone.id] = stone.key;
    config->roles.stone = stone.id;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = stone.id;
    config->base_terrain_rules.push_back(rule);
    config->content_hash = hash_world_gen_config(*config);
    return config;
}

}  // namespace

TEST(GameChunkSerializerTest, RoundTripsTerrainAndSidecar) {
    snt::game::GameChunk original;
    original.chunk_x = 5;
    original.chunk_y = -3;
    original.chunk_z = 7;
    original.state = snt::game::ChunkState::Active;
    original.terrain.resize(4, 4, 4);
    original.terrain.cell_at(0, 0, 0).material = 1;
    original.connectors.push_back({.connector_id = 42, .from_dimension = "overworld"});
    original.has_population_cell = true;
    original.population_cell.vegetation_density = 0.75f;

    const snt::game::GameChunkSerializer serializer;
    const std::vector<uint8_t> payload = serializer.serialize("overworld", original);
    ASSERT_FALSE(payload.empty());

    snt::game::GameChunk restored;
    std::string dimension;
    ASSERT_TRUE(serializer.deserialize(payload, dimension, restored));
    EXPECT_EQ(dimension, "overworld");
    EXPECT_EQ(restored.chunk_x, original.chunk_x);
    EXPECT_EQ(restored.connectors.size(), 1u);
    EXPECT_TRUE(restored.has_population_cell);
    EXPECT_FLOAT_EQ(restored.population_cell.vegetation_density, 0.75f);
}

TEST(GameChunkSerializerTest, RejectsNonCurrentPayload) {
    snt::game::GameChunk chunk;
    chunk.terrain.resize(1, 1, 1);
    const snt::game::GameChunkSerializer serializer;
    auto payload = serializer.serialize("overworld", chunk);
    ASSERT_FALSE(payload.empty());
    payload.front() = static_cast<uint8_t>(snt::game::GameChunkSerializer::kCurrentVersion - 1);

    snt::game::GameChunk restored;
    std::string dimension;
    EXPECT_FALSE(serializer.deserialize(payload, dimension, restored));
}

TEST(GameChunkSerializerTest, RejectsTrailingPayloadBytes) {
    snt::game::GameChunk chunk;
    chunk.terrain.resize(1, 1, 1);
    const snt::game::GameChunkSerializer serializer;
    auto payload = serializer.serialize("overworld", chunk);
    payload.push_back(0xFF);

    snt::game::GameChunk restored;
    std::string dimension;
    EXPECT_FALSE(serializer.deserialize(payload, dimension, restored));
}

TEST(GameChunkSidecarRegistryTest, SeparatesGameplayStateFromVoxelRegistry) {
    snt::game::GameChunkSidecarRegistry sidecars;
    const snt::game::ChunkKey key{"overworld", 1, 2, 3};
    snt::game::GameChunkSidecar sidecar;
    sidecar.entities.push_back({42});
    sidecars.set(key, std::move(sidecar));

    const auto* restored = sidecars.get(key);
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->entities.size(), 1u);
    EXPECT_EQ(restored->entities.front().id, 42u);
}

TEST(GameSaveManagerTest, PersistsVoxelChunkAndGameplaySidecarSeparately) {
    const auto save_dir = std::filesystem::temp_directory_path() /
        "snt_game_sidecar_boundary_test";
    std::filesystem::remove_all(save_dir);

    snt::game::ChunkRegistry source_voxels;
    snt::game::GameChunkSidecarRegistry source_sidecars;
    snt::voxel::VoxelChunk voxel;
    voxel.terrain.resize(2, 2, 2);
    voxel.terrain.set_cell(1, 1, 1, 9, snt::voxel::TF_SOLID);
    source_voxels.set_chunk("overworld", 0, 0, 0, std::move(voxel));

    snt::game::GameChunkSidecar sidecar;
    sidecar.has_population_cell = true;
    sidecar.population_cell.vegetation_density = 0.65f;
    sidecar.entities.push_back({123});
    source_sidecars.set({"overworld", 0, 0, 0}, std::move(sidecar));

    ASSERT_EQ(snt::game::GameSaveManager::save_dimension(
                  save_dir.string(), 12345, "overworld", source_voxels, source_sidecars),
              1);

    snt::game::ChunkRegistry loaded_voxels;
    snt::game::GameChunkSidecarRegistry loaded_sidecars;
    ASSERT_EQ(snt::game::GameSaveManager::load_dimension(
                  save_dir.string(), "overworld", loaded_voxels, loaded_sidecars),
              1);

    const auto* restored_voxel = loaded_voxels.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(restored_voxel, nullptr);
    EXPECT_TRUE(restored_voxel->terrain.cell_at(1, 1, 1).is_solid());

    const auto* restored_sidecar = loaded_sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(restored_sidecar, nullptr);
    EXPECT_TRUE(restored_sidecar->has_population_cell);
    EXPECT_FLOAT_EQ(restored_sidecar->population_cell.vegetation_density, 0.65f);
    ASSERT_EQ(restored_sidecar->entities.size(), 1u);
    EXPECT_EQ(restored_sidecar->entities.front().id, 123u);

    std::filesystem::remove_all(save_dir);
}

TEST(GameSaveManagerTest, RejectsDimensionWhenAnyChunkPayloadIsInvalid) {
    const auto save_dir = std::filesystem::temp_directory_path() /
        "snt_game_reject_partial_dimension_test";
    std::filesystem::remove_all(save_dir);
    std::filesystem::create_directories(save_dir / "regions");
    ASSERT_TRUE(snt::game::GameSaveManager::write_planet_data(
        save_dir.string(), 12345, "overworld", nullptr));

    snt::game::GameChunk chunk;
    chunk.terrain.resize(1, 1, 1);
    const snt::game::GameChunkSerializer serializer;
    auto invalid_payload = serializer.serialize("overworld", chunk);
    ASSERT_FALSE(invalid_payload.empty());
    invalid_payload.front() = snt::game::GameChunkSerializer::kCurrentVersion - 1;

    const std::string region_path = (save_dir / "regions" /
        snt::voxel::VoxelRegionFile::region_file_name("overworld", 0, 0, 0)).string();
    ASSERT_TRUE(snt::voxel::VoxelRegionFile::write(
        region_path,
        0,
        0,
        0,
        "overworld",
        {{.local_x = 0, .local_y = 0, .local_z = 0, .payload = std::move(invalid_payload)}}));

    snt::voxel::ChunkRegistry loaded_voxels;
    snt::game::GameChunkSidecarRegistry loaded_sidecars;
    EXPECT_EQ(snt::game::GameSaveManager::load_dimension(
                  save_dir.string(), "overworld", loaded_voxels, loaded_sidecars),
              -1);
    EXPECT_EQ(loaded_voxels.chunk_count(), 0u);
    EXPECT_EQ(loaded_sidecars.size(), 0u);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameWorldPersistenceLifecycleTest, SavesLoadsAndRejectsMismatchedUniverse) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto save_dir = std::filesystem::temp_directory_path() /
        ("snt_game_world_persistence_" + std::to_string(nonce));

    const snt::game::GameWorldPersistenceDescriptor descriptor{
        .universe_save_dir = save_dir.string(),
        .dimension_id = "overworld",
        .seed = 4242,
        .universe_mode = "test",
    };
    snt::game::GameWorldPersistenceLifecycle lifecycle(descriptor);

    snt::voxel::ChunkRegistry source_voxels;
    snt::game::GameChunkSidecarRegistry source_sidecars;
    const auto missing = lifecycle.load_existing(source_voxels, source_sidecars);
    ASSERT_TRUE(missing) << missing.error().format();
    EXPECT_FALSE(*missing);

    snt::voxel::VoxelChunk source_chunk;
    source_chunk.terrain.resize(2, 2, 2);
    source_chunk.terrain.set_cell(0, 0, 0, 17, snt::voxel::TF_SOLID);
    source_voxels.set_chunk("overworld", 0, 0, 0, std::move(source_chunk));
    snt::game::GameChunkSidecar source_sidecar;
    source_sidecar.entities.push_back({987});
    source_sidecars.set({"overworld", 0, 0, 0}, std::move(source_sidecar));
    ASSERT_TRUE(lifecycle.save(source_voxels, source_sidecars));
    EXPECT_TRUE(std::filesystem::exists(save_dir / "universe_header.bin"));

    snt::voxel::ChunkRegistry loaded_voxels;
    snt::game::GameChunkSidecarRegistry loaded_sidecars;
    snt::game::GameWorldPersistenceLifecycle reloaded_lifecycle(descriptor);
    const auto loaded = reloaded_lifecycle.load_existing(loaded_voxels, loaded_sidecars);
    ASSERT_TRUE(loaded) << loaded.error().format();
    EXPECT_TRUE(*loaded);
    const auto* restored_chunk = loaded_voxels.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(restored_chunk, nullptr);
    EXPECT_EQ(restored_chunk->terrain.cell_at(0, 0, 0).material, 17u);
    const auto* restored_sidecar = loaded_sidecars.get({"overworld", 0, 0, 0});
    ASSERT_NE(restored_sidecar, nullptr);
    ASSERT_EQ(restored_sidecar->entities.size(), 1u);
    EXPECT_EQ(restored_sidecar->entities.front().id, 987u);

    auto incompatible_descriptor = descriptor;
    incompatible_descriptor.seed = 4243;
    snt::voxel::ChunkRegistry incompatible_voxels;
    snt::game::GameChunkSidecarRegistry incompatible_sidecars;
    snt::game::GameWorldPersistenceLifecycle incompatible_lifecycle(
        std::move(incompatible_descriptor));
    const auto incompatible = incompatible_lifecycle.load_existing(
        incompatible_voxels, incompatible_sidecars);
    ASSERT_FALSE(incompatible);
    EXPECT_EQ(incompatible.error().code(), snt::core::ErrorCode::kInvalidState);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameBlockEntityRegistryTest, RegistersAndFindsTreeOwnership) {
    snt::game::BlockEntityRegistry registry;
    const std::vector<snt::game::OwnedCell> cells{{5, 10, 15}};
    const auto id = registry.register_tree_entity(
        "overworld", 5, 10, 15, "oak", snt::game::TreeGrowthStage::YOUNG, 0, cells);

    EXPECT_EQ(registry.find_owner_at(5, 10, 15), id);
    EXPECT_NE(registry.get_tree_state(id), nullptr);
}

TEST(GameBlockEntityRegistryTest, RemovesOwnedCellsWithEntity) {
    snt::game::BlockEntityRegistry registry;
    const std::vector<snt::game::OwnedCell> cells{{1, 1, 1}};
    const auto id = registry.register_tree_entity(
        "overworld", 1, 1, 1, "oak", snt::game::TreeGrowthStage::YOUNG, 0, cells);

    registry.remove_entity(id);
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_EQ(registry.find_owner_at(1, 1, 1), snt::game::EntityId{});
}

TEST(GameMachineCollisionOverlayTest, ClearsOneDimensionOnly) {
    snt::game::MachineCollisionOverlay overlay;
    overlay.mark("overworld", 1, 0, 0);
    overlay.mark("nether", 1, 0, 0);

    EXPECT_EQ(overlay.clear_dimension("overworld"), 1u);
    EXPECT_TRUE(overlay.is_occupied("nether", 1, 0, 0));
}

TEST(GameMachineCollisionOverlayTest, MarksAndClearsOneCell) {
    snt::game::MachineCollisionOverlay overlay;
    overlay.mark("overworld", 10, 20, 30);
    EXPECT_TRUE(overlay.is_occupied("overworld", 10, 20, 30));
    overlay.clear("overworld", 10, 20, 30);
    EXPECT_FALSE(overlay.is_occupied("overworld", 10, 20, 30));
}

TEST(GameNoiseGeneratorTest, IsDeterministicForOneSeed) {
    snt::game::NoiseGenerator first(42);
    snt::game::NoiseGenerator second(42);
    EXPECT_FLOAT_EQ(first.noise_3d(1.5f, 2.5f, 3.5f),
                    second.noise_3d(1.5f, 2.5f, 3.5f));
}

TEST(GameNoiseGeneratorTest, StaysInExpectedRange) {
    snt::game::NoiseGenerator noise(7);
    for (int index = 0; index < 100; ++index) {
        const float value = noise.noise_3d(
            static_cast<float>(index) * 0.1f,
            static_cast<float>(index) * 0.2f,
            static_cast<float>(index) * 0.3f);
        EXPECT_GE(value, -1.5f);
        EXPECT_LE(value, 1.5f);
    }
}

TEST(GameTerrainGeneratorTest, ProducesVoxelChunkAndSidecar) {
    const auto config = make_test_worldgen_config();
    snt::game::TerrainGenerator generator(snt::game::WorldSeed(12345), config);
    const snt::game::GameChunk chunk = generator.generate_chunk("overworld", 0, 0, 0);

    EXPECT_EQ(chunk.terrain.size_x, snt::voxel::VoxelChunk::kChunkSize);
    EXPECT_EQ(chunk.state, snt::game::ChunkState::Generated);
}
