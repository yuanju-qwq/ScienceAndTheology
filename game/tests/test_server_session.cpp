// Dedicated-server session integration coverage without opening a socket.

#include "engine/simulation_runtime.h"
#include "game/client/game_session_config.h"
#include "game/server/science_and_theology_server_session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace {

std::filesystem::path make_runtime_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("snt_game_server_session_" + std::to_string(nonce));
    std::filesystem::create_directories(root / "engine");
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    return root;
}

}  // namespace

TEST(GameServerSessionTest, TicksWithoutOpeningNetworkPortsWhenDisabled) {
    const auto root = make_runtime_root();
    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";
    snt::game::GameSessionConfig session_config;
    session_config.scripts.enabled = false;
    session_config.server_network.enabled = false;

    snt::engine::SimulationRuntime runtime;
    auto init = runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::make_unique<snt::game::ScienceAndTheologyServerSession>(std::move(session_config)));
    ASSERT_TRUE(init) << init.error().format();
    auto ticks = runtime.run_fixed_ticks(2);
    EXPECT_TRUE(ticks) << ticks.error().format();
    runtime.shutdown();

    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}
