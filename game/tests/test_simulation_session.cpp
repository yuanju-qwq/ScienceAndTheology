// Game-level integration coverage for the SDL/Vulkan-free session boundary.

#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "game/client/game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
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
