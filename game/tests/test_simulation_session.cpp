// Game-level integration coverage for the SDL/Vulkan-free session boundary.

#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "game/client/game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"
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
