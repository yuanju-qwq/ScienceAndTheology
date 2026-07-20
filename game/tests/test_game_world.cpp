// Tests for ScienceAndTheology-owned world rules and persistence sidecars.

#include "game/world/defs/block_entity_registry.h"
#include "game/world/defs/machine_collision_overlay.h"
#include "game/world/game_chunk.h"
#include "game/world/save/chunk_serializer.h"
#include "game/world/save/save_manager.h"
#include "game/world/save/world_persistence_lifecycle.h"
#include "game/worldgen/builtin_terrain_content.h"
#include "game/worldgen/default_worldgen_config.h"
#include "game/worldgen/noise_generator.h"
#include "game/worldgen/terrain_generator.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/storage/region_file.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::shared_ptr<const snt::game::WorldGenConfigSnapshot> make_test_worldgen_config() {
    using namespace snt::game;

    auto config = std::make_shared<WorldGenConfigSnapshot>();
    config->materials.push_back({.key = "snt:air"});
    config->materials.push_back({.key = "snt:stone", .flags = TF_SOLID});
    config->role_keys.air = "snt:air";
    config->role_keys.stone = "snt:stone";
    if (auto result = finalize_world_gen_config(*config); !result) {
        ADD_FAILURE() << result.error().format();
        return {};
    }

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = config->roles.stone;
    config->base_terrain_rules.push_back(rule);
    config->content_hash = hash_world_gen_config(*config);
    return config;
}

std::shared_ptr<const snt::game::WorldGenConfigSnapshot> make_polar_worldgen_config() {
    using namespace snt::game;

    auto config = std::make_shared<WorldGenConfigSnapshot>();
    const auto add_material = [&config](const char* key, uint32_t flags) {
        TerrainMaterialDef material;
        material.key = key;
        material.flags = flags;
        config->materials.push_back(material);
    };

    add_material("snt:air", 0);
    add_material("snt:stone", TF_SOLID | TF_MINEABLE);
    add_material("snt:dirt", TF_WALKABLE | TF_MINEABLE);
    add_material("snt:sand", TF_WALKABLE | TF_MINEABLE);
    add_material("snt:water", TF_LIQUID);
    add_material("snt:lava", TF_LIQUID);
    add_material("snt:deepstone", TF_SOLID | TF_MINEABLE);
    add_material("snt:core_barrier", TF_SOLID | TF_INDESTRUCTIBLE);
    add_material("snt:snow", TF_WALKABLE | TF_MINEABLE);
    add_material("snt:ice", TF_SOLID | TF_WALKABLE | TF_MINEABLE);
    config->role_keys = {
        .air = "snt:air",
        .stone = "snt:stone",
        .dirt = "snt:dirt",
        .sand = "snt:sand",
        .water = "snt:water",
        .lava = "snt:lava",
        .deepstone = "snt:deepstone",
        .core_barrier = "snt:core_barrier",
        .snow = "snt:snow",
        .ice = "snt:ice",
    };
    if (auto result = finalize_world_gen_config(*config); !result) {
        ADD_FAILURE() << result.error().format();
        return {};
    }

    PlanetConfig planet;
    planet.dimension_id = "polar_test";
    planet.planet_radius = 512.0f;
    planet.center_y = -512.0f;
    planet.terrain_height_scale = 16.0f;
    planet.sea_level_fraction = 0.3f;
    planet.atmosphere_type = ATMO_BREATHABLE;
    config->planet_configs.push_back(planet);
    return config;
}

}  // namespace

TEST(WorldGenConfigFinalizationTest, NormalizesSortsAndAssignsRuntimeIdsAfterCollection) {
    snt::game::WorldGenConfigSnapshot config;
    config.materials = {
        {.key = "SNT:Zinc"},
        {.key = "snt:air"},
        {.key = "snt:Alpha"},
    };
    config.role_keys.air = "SNT:Air";

    ASSERT_TRUE(snt::game::finalize_world_gen_config(config));
    EXPECT_EQ(config.roles.air, 0u);
    EXPECT_EQ(config.material_id_or("snt:alpha", 0), 1u);
    EXPECT_EQ(config.material_id_or("snt:zinc", 0), 2u);
    ASSERT_EQ(config.materials.size(), 3u);
    EXPECT_EQ(config.materials[0].key, "snt:air");
    EXPECT_EQ(config.materials[1].key, "snt:alpha");
    EXPECT_EQ(config.materials[2].key, "snt:zinc");
    EXPECT_EQ(config.find_material(static_cast<snt::game::TerrainMaterialId>(0)),
              &config.materials[0]);
    EXPECT_EQ(config.find_material(static_cast<snt::game::TerrainMaterialId>(1)),
              &config.materials[1]);
    EXPECT_EQ(config.find_material(static_cast<snt::game::TerrainMaterialId>(2)),
              &config.materials[2]);
    EXPECT_EQ(config.find_material(static_cast<snt::game::TerrainMaterialId>(3)), nullptr);

    const snt::game::WorldGenConfigSnapshot copied = config;
    ASSERT_NE(copied.find_material(static_cast<snt::game::TerrainMaterialId>(1)), nullptr);
    EXPECT_EQ(copied.find_material(static_cast<snt::game::TerrainMaterialId>(1))->key,
              "snt:alpha");
}

TEST(WorldGenConfigFinalizationTest, RejectsAuthoredRuntimeMaterialIds) {
    snt::game::WorldGenConfigSnapshot config;
    config.materials = {
        {.key = "snt:air"},
        {.id = 9, .key = "snt:stone"},
    };
    config.role_keys.air = "snt:air";

    EXPECT_FALSE(snt::game::finalize_world_gen_config(config));
}

TEST(DefaultGameWorldGenConfigTest, RegistersCompleteBuiltinTerrainContent) {
    const auto config = snt::game::make_default_game_worldgen_config();

    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->materials.size(), 159u);
    EXPECT_EQ(config->tree_species.size(), 8u);
    EXPECT_EQ(config->crop_species.size(), 6u);
    EXPECT_NE(config->find_material("snt:ore_uranium"), nullptr);
    EXPECT_NE(config->find_material("snt:runtime.machine.bloomery"), nullptr);
    EXPECT_NE(config->find_tree_species("sequoia"), nullptr);
    EXPECT_NE(config->find_crop_species("pumpkin"), nullptr);
    EXPECT_EQ(config->roles.air, 0u);
    EXPECT_EQ(config->runtime_ids.bloomery,
              config->material_id_or("snt:runtime.machine.bloomery", 0));
    for (const auto& tree : config->tree_species) {
        EXPECT_TRUE(config->has_material_key(tree.wood_material_key)) << tree.species_key;
        EXPECT_TRUE(config->has_material_key(tree.leaves_material_key)) << tree.species_key;
        EXPECT_TRUE(config->has_material_key(tree.sapling_material_key)) << tree.species_key;
    }
    for (const auto& crop : config->crop_species) {
        for (const auto& material_key : crop.stage_material_keys) {
            EXPECT_TRUE(config->has_material_key(material_key)) << crop.species_key;
        }
    }
}

TEST(DefaultGameWorldGenConfigTest, AppendsPlanetRulesAfterMaterialFinalization) {
    const auto default_config = snt::game::make_default_game_worldgen_config();
    ASSERT_NE(default_config, nullptr);

    snt::game::WorldGenConfigSnapshot config = *default_config;
    snt::game::BuiltinTerrainPlanetInput input;
    input.planet.dimension_id = "migration_test_planet";
    input.planet.planet_radius = 512.0f;
    input.planet.sea_level_fraction = 0.30f;
    input.planet.atmosphere_type = snt::game::ATMO_BREATHABLE;
    input.gravity_multiplier = 1.0f;

    ASSERT_TRUE(snt::game::append_builtin_terrain_planet_content(config, input));
    ASSERT_NE(config.find_base_rule("migration_test_planet"), nullptr);
    ASSERT_NE(config.find_planet_config("migration_test_planet"), nullptr);
    EXPECT_EQ(config.biome_rules.size(), 3u);
    EXPECT_EQ(config.rock_layer_rules.size(), 3u);
    EXPECT_EQ(config.ore_vein_groups.size(), 31u);
    EXPECT_NE(config.content_hash, 0u);
}

TEST(GameChunkSerializerTest, RoundTripsTerrainAndSidecar) {
    snt::game::GameChunk original;
    original.chunk_x = 5;
    original.chunk_y = -3;
    original.chunk_z = 7;
    original.state = snt::game::ChunkState::Active;
    original.terrain.resize(4, 4, 4);
    original.terrain.cell_at(0, 0, 0).material = 1;
    original.terrain.cell_at(1, 1, 1).fluid_type = 7;
    original.terrain.cell_at(1, 1, 1).fluid_mass = 640;
    original.terrain.cell_at(1, 1, 1).fluid_temperature = 341;
    original.terrain.cell_at(1, 1, 1).fluid_is_gas = true;
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
    EXPECT_EQ(restored.terrain.cell_at(1, 1, 1).fluid_type, 7u);
    EXPECT_EQ(restored.terrain.cell_at(1, 1, 1).fluid_mass, 640);
    EXPECT_EQ(restored.terrain.cell_at(1, 1, 1).fluid_temperature, 341);
    EXPECT_TRUE(restored.terrain.cell_at(1, 1, 1).fluid_is_gas);
    EXPECT_TRUE(restored.has_population_cell);
    EXPECT_FLOAT_EQ(restored.population_cell.vegetation_density, 0.75f);
}

TEST(GameChunkSerializerTest, RoundTripsPersistentPlayerBedsAndGraves) {
    snt::game::GameChunk original;
    original.chunk_x = -1;
    original.chunk_y = 0;
    original.chunk_z = 2;
    original.terrain.resize(1, 1, 1);
    original.player_beds.push_back({.root_x = -3, .root_y = 64, .root_z = 7});
    original.player_graves.push_back({
        .grave_id = 0x800000000000002Aull,
        .owner_account_id = "local-name:Sidecar Player",
        .death_tick = 1234,
        .root_x = -4,
        .root_y = 65,
        .root_z = 8,
        .items = {
            {.item_id = "iron_ingot", .count = 12},
            {.item_id = "relic", .count = 1, .instance_data = "unique"},
        },
    });

    const snt::game::GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", original);
    snt::game::GameChunk restored;
    std::string dimension;
    ASSERT_TRUE(serializer.deserialize(payload, dimension, restored));
    ASSERT_EQ(restored.player_beds.size(), 1u);
    EXPECT_EQ(restored.player_beds.front().root_x, -3);
    ASSERT_EQ(restored.player_graves.size(), 1u);
    const auto& grave = restored.player_graves.front();
    EXPECT_EQ(grave.grave_id, 0x800000000000002Aull);
    EXPECT_EQ(grave.owner_account_id, "local-name:Sidecar Player");
    EXPECT_EQ(grave.death_tick, 1234u);
    ASSERT_EQ(grave.items.size(), 2u);
    EXPECT_EQ(grave.items[1].instance_data, "unique");
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
    ASSERT_NE(config, nullptr);
    snt::game::TerrainGenerator generator(snt::game::WorldSeed(12345), config);
    const snt::game::GameChunk chunk = generator.generate_chunk("overworld", 0, 0, 0);

    EXPECT_EQ(chunk.terrain.size_x, snt::voxel::VoxelChunk::kChunkSize);
    EXPECT_EQ(chunk.state, snt::game::ChunkState::Generated);
}

TEST(GameTerrainGeneratorTest, PreservesPolarSurfaceLandformsAndChunkSeams) {
    using namespace snt::game;

    const auto config = make_polar_worldgen_config();
    ASSERT_NE(config, nullptr);
    TerrainGenerator generator(WorldSeed(20260619), config);
    const GameChunk chunk = generator.generate_chunk("polar_test", 0, 0, 0);
    const TerrainMaterialId snow_material = config->roles.snow;
    const TerrainMaterialId water_material = config->roles.water;

    int snow_count = 0;
    for (const TerrainCell& cell : chunk.terrain.cells) {
        if (cell.material == snow_material) {
            ++snow_count;
        }
    }
    EXPECT_GT(snow_count, 0);
    EXPECT_EQ(chunk.terrain.cell_at(0, 6, 0).material, snow_material);
    EXPECT_EQ(chunk.terrain.cell_at(16, 6, 16).material, snow_material);
    EXPECT_EQ(chunk.terrain.cell_at(31, 6, 0).material, snow_material);
    EXPECT_EQ(chunk.terrain.cell_at(16, 7, 16).material, config->roles.air);

    auto dry_config = std::make_shared<WorldGenConfigSnapshot>(*config);
    dry_config->planet_configs[0].dimension_id = "landform_test";
    dry_config->planet_configs[0].sea_level_fraction = 0.0f;
    dry_config->planet_configs[0].atmosphere_type = ATMO_NONE;
    TerrainGenerator landform_generator(WorldSeed(20260619), dry_config);
    std::array<bool, static_cast<size_t>(TerrainGenerator::LandformZone::COUNT)>
        found_zones{};
    constexpr float kPi = 3.14159265358979323846f;
    for (int latitude = -85; latitude <= 85; latitude += 5) {
        const float lat = static_cast<float>(latitude) * kPi / 180.0f;
        for (int longitude = 0; longitude < 360; longitude += 5) {
            const float lon = static_cast<float>(longitude) * kPi / 180.0f;
            const float cos_lat = std::cos(lat);
            const auto zone = landform_generator.landform_zone_at_direction(
                "landform_test",
                cos_lat * std::cos(lon), std::sin(lat), cos_lat * std::sin(lon));
            found_zones[static_cast<size_t>(zone)] = true;
        }
    }
    for (size_t zone = 0; zone < found_zones.size(); ++zone) {
        EXPECT_TRUE(found_zones[zone]) << "missing landform zone " << zone;
    }

    const GameChunk left_low = generator.generate_chunk("polar_test", 2, -1, 0);
    const GameChunk left_high = generator.generate_chunk("polar_test", 2, 0, 0);
    const GameChunk right_low = generator.generate_chunk("polar_test", 3, -1, 0);
    const GameChunk right_high = generator.generate_chunk("polar_test", 3, 0, 0);
    const auto surface_height = [air_material = config->roles.air, water_material](
                                    const GameChunk& low, const GameChunk& high,
                                    int local_x, int local_z) {
        for (int y = VoxelChunk::kChunkSize - 1; y >= 0; --y) {
            const TerrainMaterialId material = high.terrain.cell_at(local_x, y, local_z).material;
            if (material != air_material && material != water_material) {
                return y;
            }
        }
        for (int y = VoxelChunk::kChunkSize - 1; y >= 0; --y) {
            const TerrainMaterialId material = low.terrain.cell_at(local_x, y, local_z).material;
            if (material != air_material && material != water_material) {
                return y - VoxelChunk::kChunkSize;
            }
        }
        return -1000;
    };

    int max_boundary_step = 0;
    for (int z = 0; z < VoxelChunk::kChunkSize; ++z) {
        const int left_y = surface_height(left_low, left_high, VoxelChunk::kChunkSize - 1, z);
        const int right_y = surface_height(right_low, right_high, 0, z);
        if (left_y > -1000 && right_y > -1000) {
            max_boundary_step = std::max(max_boundary_step, std::abs(left_y - right_y));
        }
    }
    EXPECT_LE(max_boundary_step, 6);
}

TEST(GameSaveManagerTest, PreservesNegativeCoordinatesFlagsAndBlockEntities) {
    using namespace snt::game;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto save_dir = std::filesystem::temp_directory_path() /
        ("snt_game_migrated_world_save_" + std::to_string(nonce));
    std::filesystem::remove_all(save_dir);

    constexpr const char* kDimension = "migrated_world_save";
    ChunkRegistry source_voxels;
    GameChunkSidecarRegistry source_sidecars;
    VoxelChunk chunk;
    chunk.state = ChunkState::Active;
    chunk.terrain.resize(VoxelChunk::kChunkSize, VoxelChunk::kChunkSize, VoxelChunk::kChunkSize);
    chunk.terrain.set_cell(1, 2, 3, 17, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(31, 0, 31, 23, TF_SOLID | TF_WALKABLE);
    source_voxels.set_chunk(kDimension, -1, 0, 2, std::move(chunk));

    GameChunkSidecar sidecar;
    BlockEntityPlacement machine;
    machine.id.id = 42;
    machine.entity_type = BlockEntityType::MACHINE;
    machine.root_x = -31;
    machine.root_y = 2;
    machine.root_z = 67;
    machine.type_data_json = R"({"machine_type":"furnace","facing":4})";
    sidecar.block_entities.push_back(std::move(machine));
    source_sidecars.set({kDimension, -1, 0, 2}, std::move(sidecar));

    ASSERT_EQ(GameSaveManager::save_dimension(
                  save_dir.string(), 20260619, kDimension, source_voxels, source_sidecars),
              1);

    ChunkRegistry restored_voxels;
    GameChunkSidecarRegistry restored_sidecars;
    ASSERT_EQ(GameSaveManager::load_dimension(
                  save_dir.string(), kDimension, restored_voxels, restored_sidecars),
              1);

    const VoxelChunk* restored = restored_voxels.get_chunk(kDimension, -1, 0, 2);
    ASSERT_NE(restored, nullptr);
    const TerrainCell& placed = restored->terrain.cell_at(1, 2, 3);
    EXPECT_EQ(placed.material, 17u);
    EXPECT_TRUE(placed.is_solid());
    EXPECT_TRUE(placed.is_mineable());

    const GameChunkSidecar* restored_sidecar =
        restored_sidecars.get({kDimension, -1, 0, 2});
    ASSERT_NE(restored_sidecar, nullptr);
    ASSERT_EQ(restored_sidecar->block_entities.size(), 1u);
    EXPECT_EQ(restored_sidecar->block_entities.front().id.id, 42u);
    EXPECT_EQ(restored_sidecar->block_entities.front().root_x, -31);
    EXPECT_EQ(restored_sidecar->block_entities.front().root_y, 2);
    EXPECT_EQ(restored_sidecar->block_entities.front().root_z, 67);

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}
