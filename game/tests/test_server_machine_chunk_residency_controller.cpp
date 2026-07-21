// Server machine ECS residency integration tests.

#include "ecs/entt_config.h"
#include "engine/simulation_runtime.h"
#include "engine/simulation_services.h"
#include "game/client/game_session_config.h"
#include "game/client/machine_tick_system.h"
#include "game/server/server_machine_chunk_residency_controller.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/simulation/science_and_theology_simulation_session.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path make_runtime_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("snt_server_machine_residency_" + std::to_string(nonce));
    std::filesystem::create_directories(root / "engine");
    std::filesystem::create_directories(root / "game");
    std::filesystem::create_directories(root / "user");
    std::filesystem::copy(
        std::filesystem::path(SNT_GAME_TEST_ROOT) / "game/scripts",
        root / "game/scripts", std::filesystem::copy_options::recursive);
    return root;
}

TEST(ServerMachineChunkResidencyControllerTest,
     DematerializesFarMachinesAndRestoresPlayerTicketedMachines) {
    const auto root = make_runtime_root();

    snt::core::RuntimeConfig runtime_config;
    runtime_config.assets.manifest_path = "missing_manifest.json";
    snt::game::GameSessionConfig session_config;
    session_config.demo.bootstrap_chunks = false;

    auto session = std::make_unique<snt::game::ScienceAndTheologySimulationSession>(
        std::move(session_config));
    auto* session_view = session.get();
    snt::engine::SimulationRuntime runtime;
    ASSERT_TRUE(runtime.init(
        runtime_config,
        {
            .engine_root = (root / "engine").string(),
            .game_root = (root / "game").string(),
            .user_root = (root / "user").string(),
        },
        std::move(session)));
    ASSERT_NE(session_view->content().find_machine("furnace"), nullptr);

    constexpr int32_t kMachineChunkX = 10;
    const snt::voxel::ChunkKey machine_chunk_key{"overworld", kMachineChunkX, 0, 0};
    const int32_t machine_block_x = snt::voxel::VoxelChunk::kChunkSize * kMachineChunkX;
    snt::voxel::VoxelChunk machine_chunk;
    machine_chunk.terrain.resize(1, 1, 1);
    runtime.world_session().chunks().set_chunk(
        machine_chunk_key.dimension_id, machine_chunk_key.chunk_x,
        machine_chunk_key.chunk_y, machine_chunk_key.chunk_z, std::move(machine_chunk));
    session_view->world_sidecars().set(machine_chunk_key, {});

    snt::game::MachineRuntimeComponent machine;
    machine.machine_id = "furnace";
    machine.input_slots = {snt::game::MachineItemStack::item("iron_ore", 1)};
    const auto anchored = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        runtime.world_session().world(), session_view->world_sidecars(), machine_chunk_key,
        machine_block_x, 0, 0, std::move(machine));
    ASSERT_TRUE(anchored) << anchored.error().format();

    snt::game::ServerMachineChunkResidencyController controller(
        *session_view,
        {
            .permanent_spawn = {
                .dimension_id = "overworld",
                .position = {.x = 0, .y = 0, .z = 0},
            },
            .horizontal_aoi_radius_blocks = 16,
            .vertical_aoi_radius_blocks = 16,
        });

    ASSERT_TRUE(controller.reconcile(1, std::span<const snt::game::GamePlayerWorldPosition>{}));
    const auto* offline_sidecar = session_view->world_sidecars().get(machine_chunk_key);
    ASSERT_NE(offline_sidecar, nullptr);
    ASSERT_EQ(offline_sidecar->machine_runtime_records.size(), 1u);
    EXPECT_EQ(offline_sidecar->machine_runtime_records.front().residency,
              snt::game::MachineRuntimeResidency::kOfflineStandalone);
    EXPECT_TRUE(runtime.world_session().world().find_entity_by_guid(
        anchored->entity_guid) == entt::null);

    ASSERT_TRUE(runtime.run_fixed_ticks(5));

    snt::game::GamePlayerWorldPosition player_position;
    player_position.dimension_id = "overworld";
    player_position.position = {.x = machine_block_x, .y = 0, .z = 0};
    const std::array<snt::game::GamePlayerWorldPosition, 1> player_positions{
        player_position};
    ASSERT_TRUE(controller.reconcile(6, player_positions));

    const auto* materialized_sidecar = session_view->world_sidecars().get(machine_chunk_key);
    ASSERT_NE(materialized_sidecar, nullptr);
    const auto& record = materialized_sidecar->machine_runtime_records.front();
    EXPECT_EQ(record.residency, snt::game::MachineRuntimeResidency::kLoaded);
    const entt::entity restored_entity = runtime.world_session().world().find_entity_by_guid(
        anchored->entity_guid);
    ASSERT_TRUE(restored_entity != entt::null);
    const auto& restored = runtime.world_session().world().get_component<
        snt::game::MachineRuntimeComponent>(restored_entity);
    ASSERT_TRUE(restored.active_recipe.has_value());
    EXPECT_EQ(restored.progress_ticks, 5);

    runtime.shutdown();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error) << error.message();
}

}  // namespace
