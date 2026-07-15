// Tests for chunk-anchored machine runtime persistence.

#include "ecs/world.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/world/game_chunk.h"
#include "game/world/save/chunk_serializer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

constexpr uint64_t kMachineGuid = 0xAABBCCDD00112233ULL;
constexpr snt::game::EntityId kMachineAnchor{41};

snt::game::BlockEntityPlacement make_machine_anchor() {
    return {
        .id = kMachineAnchor,
        .entity_type = snt::game::BlockEntityType::MACHINE,
        .root_x = -1,
        .root_y = 4,
        .root_z = 5,
        .type_data_json = "furnace|0",
        .owned_cell_count = 0,
    };
}

snt::game::MachineRuntimePersistenceRecord make_machine_record() {
    using namespace snt::game;

    MachineRuntimePersistenceRecord record;
    record.anchor_entity_id = kMachineAnchor;
    record.entity_guid = kMachineGuid;
    record.machine_id = "furnace";
    record.input = {"iron_ore", 3};
    record.output_slots = {{"slag", 2}};
    record.stored_energy = 17;
    record.energy_capacity = 100;
    record.max_output_slots = 4;
    record.max_stack_size = 64;
    record.progress_ticks = 2;
    record.active_recipe = MachineRuntimeRecipeSnapshot{
        .id = "snt.furnace.iron",
        .input_item_id = "iron_ore",
        .outputs = {{"iron_ingot", 1}},
        .duration_ticks = 5,
        .energy_per_tick = 3,
    };
    record.run_state = static_cast<uint8_t>(MachineRunState::Running);
    return record;
}

snt::game::GameChunkSidecar make_machine_sidecar() {
    snt::game::GameChunkSidecar sidecar;
    sidecar.block_entities.push_back(make_machine_anchor());
    sidecar.machine_runtime_records.push_back(make_machine_record());
    return sidecar;
}

}  // namespace

TEST(GameChunkSerializerTest, RoundTripsChunkAnchoredMachineRuntimeRecord) {
    snt::game::GameChunk original;
    original.chunk_x = -1;
    original.chunk_y = 0;
    original.chunk_z = 0;
    original.terrain.resize(1, 1, 1);
    original.block_entities.push_back(make_machine_anchor());
    original.machine_runtime_records.push_back(make_machine_record());

    const snt::game::GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", original);

    snt::game::GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(payload, dimension_id, restored));
    ASSERT_EQ(restored.machine_runtime_records.size(), 1u);
    const auto& record = restored.machine_runtime_records.front();
    EXPECT_EQ(record.anchor_entity_id, kMachineAnchor);
    EXPECT_EQ(record.entity_guid, kMachineGuid);
    EXPECT_EQ(record.machine_id, "furnace");
    EXPECT_EQ(record.input.item_id, "iron_ore");
    EXPECT_EQ(record.input.count, 3);
    ASSERT_TRUE(record.active_recipe.has_value());
    EXPECT_EQ(record.active_recipe->id, "snt.furnace.iron");
    EXPECT_EQ(record.active_recipe->outputs.front().item_id, "iron_ingot");
    EXPECT_EQ(record.run_state,
              static_cast<uint8_t>(snt::game::MachineRunState::Running));
}

TEST(GameMachineRuntimePersistenceTest, RestoresCapturesAndRejectsUnanchoredMachines) {
    using namespace snt::game;

    const ChunkKey chunk_key{"overworld", -1, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, make_machine_sidecar());

    snt::ecs::World world;
    ASSERT_TRUE(GameMachineRuntimePersistence::restore(world, sidecars));

    const snt::ecs::EntityGuid machine_guid{kMachineGuid};
    const entt::entity machine_entity = world.find_entity_by_guid(machine_guid);
    ASSERT_TRUE(machine_entity != entt::null);
    auto& restored = world.get_component<MachineRuntimeComponent>(machine_entity);
    EXPECT_EQ(restored.machine_id, "furnace");
    EXPECT_EQ(restored.stored_energy, 17);
    ASSERT_TRUE(restored.active_recipe.has_value());
    EXPECT_EQ(restored.active_recipe->outputs.front().item_id, "iron_ingot");

    restored.stored_energy = 33;
    const entt::entity unanchored_entity = world.create_entity();
    auto& unanchored = world.add_component<MachineRuntimeComponent>(unanchored_entity);
    unanchored.machine_id = "furnace";

    const auto rejected_capture = GameMachineRuntimePersistence::capture(world, sidecars);
    ASSERT_FALSE(rejected_capture);
    EXPECT_EQ(rejected_capture.error().code(), snt::core::ErrorCode::kInvalidState);
    const auto* unchanged = sidecars.get(chunk_key);
    ASSERT_NE(unchanged, nullptr);
    EXPECT_EQ(unchanged->machine_runtime_records.front().stored_energy, 17);

    world.destroy_entity(unanchored_entity);
    ASSERT_TRUE(GameMachineRuntimePersistence::capture(world, sidecars));
    const auto* captured = sidecars.get(chunk_key);
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->machine_runtime_records.front().stored_energy, 33);
}

TEST(GameMachineRuntimePersistenceTest, CreatesAndRemovesThroughTheAnchorLifecycle) {
    using namespace snt::game;

    const ChunkKey chunk_key{"overworld", -1, 0, 0};
    GameChunkSidecar sidecar;
    sidecar.block_entities.push_back(make_machine_anchor());
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, std::move(sidecar));

    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "furnace";

    const auto created = GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, kMachineAnchor, std::move(runtime));
    ASSERT_TRUE(created) << created.error().format();
    EXPECT_TRUE(world.find_entity_by_guid(*created) != entt::null);
    const auto* populated = sidecars.get(chunk_key);
    ASSERT_NE(populated, nullptr);
    ASSERT_EQ(populated->machine_runtime_records.size(), 1u);
    EXPECT_EQ(populated->machine_runtime_records.front().entity_guid, created->value);

    ASSERT_TRUE(GameMachineRuntimePersistence::remove_anchored_machine(
        world, sidecars, *created));
    EXPECT_TRUE(world.find_entity_by_guid(*created) == entt::null);
    const auto* emptied = sidecars.get(chunk_key);
    ASSERT_NE(emptied, nullptr);
    EXPECT_TRUE(emptied->machine_runtime_records.empty());
}
