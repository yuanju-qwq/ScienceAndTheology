// Game-level integration coverage for the SDL/Vulkan-free session boundary.

#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "game/client/machine_tick_system.h"
#include "game/client/game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"
#include "game/world/save/world_persistence_lifecycle.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {

std::filesystem::path make_runtime_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("snt_game_simulation_session_" + std::to_string(nonce));
    std::filesystem::create_directories(root / "engine");
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    return root;
}

}  // namespace

TEST(GameSimulationSessionTest, InitializesTerrainAndTicksWithoutClientRuntime) {
    const auto root = make_runtime_root();

    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";
    snt::game::GameSessionConfig session_config;
    session_config.scripts.enabled = false;
    session_config.demo.bootstrap_chunks = true;

    snt::engine::SimulationRuntime runtime;
    auto init = runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(
            std::move(session_config)));
    ASSERT_TRUE(init) << init.error().format();
    EXPECT_EQ(runtime.world_session().chunks().chunk_count(), 2u);

    auto ticks = runtime.run_fixed_ticks(3);
    EXPECT_TRUE(ticks) << ticks.error().format();
    runtime.shutdown();

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameSimulationSessionTest, PersistsWorldAcrossSessionRestartWhenEnabled) {
    const auto root = make_runtime_root();
    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";

    snt::game::GameSessionConfig session_config;
    session_config.scripts.enabled = false;
    session_config.demo.bootstrap_chunks = true;
    session_config.demo.seed = 404;
    session_config.persistence.world_save_enabled = true;
    session_config.persistence.universe_save_dir = "saves/session_restart";
    session_config.persistence.world_dimension_id = "overworld";
    session_config.persistence.universe_mode = "test";
    const snt::game::GameSessionConfig restart_config = session_config;

    snt::engine::SimulationRuntime first_runtime;
    auto first_init = first_runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(
            std::move(session_config)));
    ASSERT_TRUE(first_init) << first_init.error().format();
    auto* source_chunk = first_runtime.world_session().chunks().get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(source_chunk, nullptr);
    source_chunk->terrain.cell_at(0, 0, 0).material = 251;
    first_runtime.shutdown();
    EXPECT_TRUE(std::filesystem::exists(
        root / "user" / "saves" / "session_restart" / "universe_header.bin"));

    snt::engine::SimulationRuntime restarted_runtime;
    auto restarted_init = restarted_runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(restart_config));
    ASSERT_TRUE(restarted_init) << restarted_init.error().format();
    const auto* restored_chunk = restarted_runtime.world_session().chunks().get_chunk(
        "overworld", 0, 0, 0);
    ASSERT_NE(restored_chunk, nullptr);
    EXPECT_EQ(restored_chunk->terrain.cell_at(0, 0, 0).material, 251u);
    restarted_runtime.shutdown();

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameSimulationSessionTest, RejectsCorruptWorldWithoutOverwritingItDuringShutdown) {
    const auto root = make_runtime_root();
    const auto universe_root = root / "user" / "saves" / "corrupt_world";
    std::filesystem::create_directories(universe_root);
    const auto header_path = universe_root / "universe_header.bin";
    {
        std::ofstream header(header_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(header.is_open());
        header.write("not-a-current-world", 19);
        ASSERT_TRUE(header.good());
    }

    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";
    snt::game::GameSessionConfig session_config;
    session_config.scripts.enabled = false;
    session_config.persistence.world_save_enabled = true;
    session_config.persistence.universe_save_dir = "saves/corrupt_world";
    session_config.persistence.world_dimension_id = "overworld";
    session_config.persistence.universe_mode = "test";

    snt::engine::SimulationRuntime runtime;
    const auto init = runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(
            std::move(session_config)));
    ASSERT_FALSE(init);
    runtime.shutdown();

    {
        std::ifstream header(header_path, std::ios::binary);
        ASSERT_TRUE(header.is_open());
        const std::string contents((std::istreambuf_iterator<char>(header)),
                                   std::istreambuf_iterator<char>());
        EXPECT_EQ(contents, "not-a-current-world");
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameSimulationSessionTest, RestoresAndSavesChunkAnchoredMachineRuntime) {
    const auto root = make_runtime_root();
    const auto universe_root = root / "user" / "saves" / "machine_restart";
    constexpr uint64_t kMachineGuid = 0xABCDEF0102030405ULL;

    snt::game::GameSessionConfig session_config;
    session_config.scripts.enabled = false;
    session_config.demo.seed = 909;
    session_config.persistence.world_save_enabled = true;
    session_config.persistence.universe_save_dir = "saves/machine_restart";
    session_config.persistence.world_dimension_id = "overworld";
    session_config.persistence.universe_mode = "test";
    const snt::game::GameSessionConfig restart_config = session_config;

    snt::game::GameWorldPersistenceLifecycle persistence({
        .universe_save_dir = universe_root.string(),
        .dimension_id = "overworld",
        .seed = 909,
        .universe_mode = "test",
    });
    snt::voxel::ChunkRegistry source_chunks;
    snt::voxel::VoxelChunk source_chunk;
    source_chunk.terrain.resize(1, 1, 1);
    source_chunks.set_chunk("overworld", 0, 0, 0, std::move(source_chunk));

    snt::game::GameChunkSidecar source_sidecar;
    const snt::game::EntityId anchor_id{73};
    source_sidecar.block_entities.push_back({
        .id = anchor_id,
        .entity_type = snt::game::BlockEntityType::MACHINE,
        .root_x = 0,
        .root_y = 0,
        .root_z = 0,
        .type_data_json = "furnace|0",
        .owned_cell_count = 0,
    });
    snt::game::MachineRuntimePersistenceRecord record;
    record.anchor_entity_id = anchor_id;
    record.entity_guid = kMachineGuid;
    record.machine_id = "furnace";
    record.input = {"iron_ore", 2};
    record.output_slots = {{"slag", 1}};
    record.stored_energy = 12;
    record.energy_capacity = 100;
    record.max_output_slots = 4;
    record.max_stack_size = 64;
    record.progress_ticks = 2;
    record.active_recipe = snt::game::MachineRuntimeRecipeSnapshot{
        .id = "snt.furnace.iron",
        .input_item_id = "iron_ore",
        .outputs = {{"iron_ingot", 1}},
        .duration_ticks = 5,
        .energy_per_tick = 3,
    };
    record.run_state = static_cast<uint8_t>(snt::game::MachineRunState::Running);
    source_sidecar.machine_runtime_records.push_back(std::move(record));
    snt::game::GameChunkSidecarRegistry source_sidecars;
    source_sidecars.set({"overworld", 0, 0, 0}, std::move(source_sidecar));
    ASSERT_TRUE(persistence.save(source_chunks, source_sidecars));

    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";
    snt::engine::SimulationRuntime first_runtime;
    ASSERT_TRUE(first_runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(session_config)));

    const snt::ecs::EntityGuid machine_guid{kMachineGuid};
    const entt::entity machine_entity = first_runtime.world_session().world().find_entity_by_guid(
        machine_guid);
    ASSERT_TRUE(machine_entity != entt::null);
    auto& machine = first_runtime.world_session().world().get_component<
        snt::game::MachineRuntimeComponent>(machine_entity);
    EXPECT_EQ(machine.stored_energy, 12);
    ASSERT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.active_recipe->id, "snt.furnace.iron");
    machine.stored_energy = 47;
    machine.output_slots.push_back({"iron_ingot", 1});
    first_runtime.shutdown();

    snt::engine::SimulationRuntime restarted_runtime;
    ASSERT_TRUE(restarted_runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologySimulationSession>(restart_config)));
    const entt::entity restored_entity = restarted_runtime.world_session().world().find_entity_by_guid(
        machine_guid);
    ASSERT_TRUE(restored_entity != entt::null);
    const auto& restored_machine = restarted_runtime.world_session().world().get_component<
        snt::game::MachineRuntimeComponent>(restored_entity);
    EXPECT_EQ(restored_machine.stored_energy, 47);
    ASSERT_EQ(restored_machine.output_slots.size(), 2u);
    EXPECT_EQ(restored_machine.output_slots.back().item_id, "iron_ingot");
    restarted_runtime.shutdown();

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}
