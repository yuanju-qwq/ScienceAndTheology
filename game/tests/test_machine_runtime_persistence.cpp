// Tests for chunk-anchored machine runtime persistence.

#include "ecs/world.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/world/game_chunk.h"
#include "game/world/save/chunk_serializer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kMachineGuid = 0xAABBCCDD00112233ULL;
constexpr snt::game::EntityId kMachineAnchor{41};

[[nodiscard]] snt::game::ResourceRuntimeIndex::Snapshot machine_resource_snapshot() {
    static const snt::game::ResourceRuntimeIndex::Snapshot snapshot = [] {
        snt::game::ResourceRuntimeIndex index;
        const std::vector<snt::game::ResourceContentKey> keys{
            snt::game::ResourceContentKey::item("charcoal"),
            snt::game::ResourceContentKey::item("iron_ingot"),
            snt::game::ResourceContentKey::item("iron_ore"),
            snt::game::ResourceContentKey::item("slag"),
        };
        const auto result = index.rebuild(keys);
        if (!result) throw std::logic_error("Machine persistence test snapshot failed");
        return index.snapshot();
    }();
    return snapshot;
}

[[nodiscard]] snt::game::ResourceStack machine_item(std::string id, int64_t amount) {
    const auto stack = snt::game::resolve_resource_stack(
        snt::game::ResourceContentStack::item(std::move(id), amount),
        machine_resource_snapshot());
    if (!stack) throw std::logic_error("Machine persistence test item is missing");
    return *stack;
}

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
    record.input_slots = {
        ResourceContentStack::item("iron_ore", 3),
        ResourceContentStack::item("charcoal", 5),
    };
    record.output_slots = {ResourceContentStack::item("slag", 2)};
    record.stored_energy = 17;
    record.energy_capacity = 100;
    record.max_input_slots = 4;
    record.max_output_slots = 4;
    record.max_stack_size = 64;
    record.progress_ticks = 2;
    record.active_recipe = MachineRuntimeRecipeSnapshot{
        .id = "snt.furnace.iron",
        .inputs = {
            ResourceContentStack::item("iron_ore", 1),
            ResourceContentStack::item("charcoal", 1),
        },
        .outputs = {ResourceContentStack::item("iron_ingot", 1)},
        .duration_ticks = 5,
        .energy_per_tick = 3,
    };
    record.job_owner_account_id = "account:machine-owner";
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
    EXPECT_EQ(record.max_input_slots, 4);
    ASSERT_EQ(record.input_slots.size(), 2u);
    EXPECT_EQ(record.input_slots[0].key.id, "iron_ore");
    EXPECT_EQ(record.input_slots[0].amount, 3);
    EXPECT_EQ(record.input_slots[1].key.id, "charcoal");
    EXPECT_EQ(record.input_slots[1].amount, 5);
    ASSERT_TRUE(record.active_recipe.has_value());
    EXPECT_EQ(record.active_recipe->id, "snt.furnace.iron");
    ASSERT_EQ(record.active_recipe->inputs.size(), 2u);
    EXPECT_EQ(record.active_recipe->inputs[1].key.id, "charcoal");
    EXPECT_EQ(record.active_recipe->inputs[1].amount, 1);
    EXPECT_EQ(record.active_recipe->outputs.front().key.id, "iron_ingot");
    EXPECT_FALSE(record.activation_requested);
    EXPECT_EQ(record.job_owner_account_id, "account:machine-owner");
    EXPECT_EQ(record.run_state,
              static_cast<uint8_t>(snt::game::MachineRunState::Running));
}

TEST(GameChunkSerializerTest, RoundTripsPendingManualMachineActivation) {
    snt::game::GameChunk original;
    original.chunk_x = -1;
    original.chunk_y = 0;
    original.chunk_z = 0;
    original.terrain.resize(1, 1, 1);
    original.block_entities.push_back(make_machine_anchor());

    auto record = make_machine_record();
    record.active_recipe.reset();
    record.progress_ticks = 0;
    record.activation_requested = true;
    record.run_state = static_cast<uint8_t>(snt::game::MachineRunState::WaitingForActivation);
    original.machine_runtime_records.push_back(std::move(record));

    const snt::game::GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", original);

    snt::game::GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(payload, dimension_id, restored));
    ASSERT_EQ(restored.machine_runtime_records.size(), 1u);
    const auto& restored_record = restored.machine_runtime_records.front();
    EXPECT_TRUE(restored_record.activation_requested);
    EXPECT_FALSE(restored_record.active_recipe.has_value());
    EXPECT_EQ(restored_record.job_owner_account_id, "account:machine-owner");
    EXPECT_EQ(restored_record.run_state,
              static_cast<uint8_t>(snt::game::MachineRunState::WaitingForActivation));
    ASSERT_EQ(restored_record.input_slots.size(), 2u);
    EXPECT_EQ(restored_record.input_slots[0].key.id, "iron_ore");
    EXPECT_EQ(restored_record.input_slots[1].key.id, "charcoal");
}

TEST(GameMachineRuntimePersistenceTest, RestoresCapturesAndRejectsUnanchoredMachines) {
    using namespace snt::game;

    const ChunkKey chunk_key{"overworld", -1, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, make_machine_sidecar());

    snt::ecs::World world;
    ASSERT_TRUE(GameMachineRuntimePersistence::restore(
        world, sidecars, machine_resource_snapshot()));

    const snt::ecs::EntityGuid machine_guid{kMachineGuid};
    const entt::entity machine_entity = world.find_entity_by_guid(machine_guid);
    ASSERT_TRUE(machine_entity != entt::null);
    auto& restored = world.get_component<MachineRuntimeComponent>(machine_entity);
    EXPECT_EQ(restored.machine_id, "furnace");
    EXPECT_EQ(restored.stored_energy, 17);
    ASSERT_EQ(restored.input_slots.size(), 2u);
    EXPECT_EQ(restored.input_slots[1].key, machine_item("charcoal", 1).key);
    ASSERT_TRUE(restored.active_recipe.has_value());
    EXPECT_EQ(restored.active_recipe->outputs.front().key, machine_item("iron_ingot", 1).key);
    EXPECT_EQ(restored.job_owner_account_id, "account:machine-owner");

    restored.stored_energy = 33;
    restored.input_slots = {
        machine_item("iron_ore", 2),
        machine_item("charcoal", 4),
    };
    restored.active_recipe.reset();
    restored.progress_ticks = 0;
    restored.activation_requested = true;
    restored.job_owner_account_id = "account:machine-owner-updated";
    restored.state = MachineRunState::WaitingForActivation;
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
    ASSERT_EQ(captured->machine_runtime_records.front().input_slots.size(), 2u);
    EXPECT_EQ(captured->machine_runtime_records.front().input_slots[1].key.id,
              "charcoal");
    EXPECT_TRUE(captured->machine_runtime_records.front().activation_requested);
    EXPECT_EQ(captured->machine_runtime_records.front().job_owner_account_id,
              "account:machine-owner-updated");
    EXPECT_EQ(captured->machine_runtime_records.front().run_state,
              static_cast<uint8_t>(MachineRunState::WaitingForActivation));
}

TEST(GameMachineRuntimePersistenceTest, CreatesAndRemovesThroughTheAnchorLifecycle) {
    using namespace snt::game;

    const ChunkKey chunk_key{"overworld", -1, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, {});

    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "furnace";
    runtime.resource_runtime_index = machine_resource_snapshot();

    const auto created = GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, -1, 4, 5, std::move(runtime));
    ASSERT_TRUE(created) << created.error().format();
    EXPECT_TRUE(world.find_entity_by_guid(created->entity_guid) != entt::null);
    const auto* populated = sidecars.get(chunk_key);
    ASSERT_NE(populated, nullptr);
    ASSERT_EQ(populated->machine_runtime_records.size(), 1u);
    EXPECT_EQ(populated->machine_runtime_records.front().anchor_entity_id,
              created->anchor_entity_id);
    EXPECT_EQ(populated->machine_runtime_records.front().entity_guid,
              created->entity_guid.value);
    ASSERT_EQ(populated->block_entities.size(), 1u);
    EXPECT_EQ(populated->block_entities.front().id, created->anchor_entity_id);
    EXPECT_EQ(populated->block_entities.front().entity_type, BlockEntityType::MACHINE);
    EXPECT_EQ(populated->block_entities.front().root_x, -1);

    ASSERT_TRUE(GameMachineRuntimePersistence::remove_anchored_machine(
        world, sidecars, created->entity_guid));
    EXPECT_TRUE(world.find_entity_by_guid(created->entity_guid) == entt::null);
    const auto* emptied = sidecars.get(chunk_key);
    ASSERT_NE(emptied, nullptr);
    EXPECT_TRUE(emptied->machine_runtime_records.empty());
    EXPECT_TRUE(emptied->block_entities.empty());

    MachineRuntimeComponent invalid_runtime;
    const auto rejected = GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, -2, 4, 5, std::move(invalid_runtime));
    EXPECT_FALSE(rejected);
    EXPECT_TRUE(emptied->machine_runtime_records.empty());
    EXPECT_TRUE(emptied->block_entities.empty());
}
