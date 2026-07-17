// Game-owned machine runtime tests: deterministic worker behavior and
// reload-safe gameplay recipe snapshots.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/job_system.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"
#include "game_content_registry.h"
#include "machine_tick_system.h"
#include "game/simulation/machine_interaction_service.h"

namespace {

using snt::game::GameContentRegistry;
using snt::game::IMachineTickEventSink;
using snt::game::MachineItemStack;
using snt::game::MachineDefinition;
using snt::game::MachineActivationContext;
using snt::game::MachineInteractionService;
using snt::game::MachineRunState;
using snt::game::MachineRuntimeComponent;
using snt::game::MachineTickEvent;
using snt::game::MachineTickEventKind;
using snt::game::MachineTickSystem;
using snt::game::RecipeDefinition;
using snt::game::RecipeInputDefinition;
using snt::game::RecipeOutputDefinition;

RecipeDefinition make_recipe(std::string id,
                             std::string input,
                             std::string output,
                             int duration_ticks,
                             int energy_per_tick = 0) {
    RecipeDefinition recipe;
    recipe.id = std::move(id);
    recipe.machine_id = "furnace";
    recipe.inputs = {RecipeInputDefinition{std::move(input), 1}};
    recipe.outputs = {RecipeOutputDefinition{std::move(output), 1}};
    recipe.duration_ticks = duration_ticks;
    recipe.energy_per_tick = energy_per_tick;
    return recipe;
}

bool register_items(GameContentRegistry& content,
                    std::initializer_list<std::string_view> item_ids) {
    for (const std::string_view item_id : item_ids) {
        if (content.find_item(item_id) != nullptr) continue;
        if (!content.register_builtin_item({
                .id = std::string(item_id),
                .title_key = "item.test",
                .max_stack = 64,
            })) {
            return false;
        }
    }
    return true;
}

class CapturingMachineEvents final : public IMachineTickEventSink {
public:
    void on_machine_tick_event(const MachineTickEvent& event) override {
        events.push_back(event);
    }

    std::vector<MachineTickEvent> events;
};

bool tick_machine(snt::ecs::World& world,
                  const std::shared_ptr<MachineTickSystem>& system) {
    snt::core::JobSystem jobs;
    snt::ecs::SystemScheduler scheduler(jobs);
    if (!scheduler.register_worker(system)) return false;
    return static_cast<bool>(scheduler.fixed_tick(world, 0.05f));
}

}  // namespace

TEST(MachineTickSystemTest, ProcessesFurnaceRecipeAndPublishesCompletion) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"iron_ore", "iron_ingot"}));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.iron", "iron_ore", "iron_ingot", 3)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input_slots = {{"iron_ore", 2}};

    CapturingMachineEvents events;
    auto system = std::make_shared<MachineTickSystem>(content, &events);
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));

    EXPECT_EQ(machine.state, MachineRunState::Idle);
    EXPECT_FALSE(machine.active_recipe.has_value());
    ASSERT_EQ(machine.input_slots.size(), 1u);
    EXPECT_EQ(machine.input_slots.front().item_id, "iron_ore");
    EXPECT_EQ(machine.input_slots.front().count, 1);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "iron_ingot");
    EXPECT_EQ(machine.output_slots[0].count, 1);

    ASSERT_GE(events.events.size(), 2U);
    EXPECT_EQ(events.events[events.events.size() - 2].kind,
              MachineTickEventKind::RecipeCompleted);
    EXPECT_EQ(events.events.back().kind, MachineTickEventKind::StateChanged);
}

TEST(MachineTickSystemTest, WorkerPoolAppliesMachineCommandAtBarrier) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"tin_ore", "tin_ingot"}));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.worker", "tin_ore", "tin_ingot", 2)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input_slots = {{"tin_ore", 1}};

    snt::core::JobSystemP2 jobs;
    jobs.init(2);
    {
        auto system = std::make_shared<MachineTickSystem>(content);
        snt::ecs::SystemScheduler scheduler(jobs);
        ASSERT_TRUE(scheduler.register_worker(system));
        ASSERT_TRUE(scheduler.fixed_tick(world, 0.05f));

        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 1u);
        EXPECT_EQ(scheduler.diagnostics().commands_applied, 1u);
        EXPECT_EQ(machine.state, MachineRunState::Running);
        EXPECT_EQ(machine.progress_ticks, 1);
        EXPECT_TRUE(machine.input_slots.empty());
    }
    jobs.shutdown();
}

TEST(MachineTickSystemTest, WorkerPoolShardsMachinesAndPublishesGuidOrder) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"lead_ore", "lead_ingot"}));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.sharded", "lead_ore", "lead_ingot", 1)));

    constexpr int kMachineCount = 64;
    snt::ecs::World world;
    std::vector<entt::entity> entities;
    std::vector<snt::ecs::EntityGuid> expected_guids;
    entities.reserve(kMachineCount);
    expected_guids.reserve(kMachineCount);
    for (int index = 0; index < kMachineCount; ++index) {
        const entt::entity entity = world.create_entity();
        auto& machine = world.add_component<MachineRuntimeComponent>(entity);
        machine.machine_id = "furnace";
        machine.input_slots = {{"lead_ore", 1}};
        entities.push_back(entity);
        expected_guids.push_back(world.guid_of(entity));
    }
    std::sort(expected_guids.begin(), expected_guids.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.value < rhs.value;
              });

    CapturingMachineEvents events;
    snt::core::JobSystemP2 jobs;
    jobs.init(2);
    {
        auto system = std::make_shared<MachineTickSystem>(content, &events);
        snt::ecs::SystemScheduler scheduler(jobs);
        ASSERT_TRUE(scheduler.register_worker(system));
        ASSERT_TRUE(scheduler.fixed_tick(world, 0.05f));

        EXPECT_EQ(scheduler.diagnostics().worker_tasks_submitted, 1u);
        EXPECT_EQ(scheduler.diagnostics().worker_parallel_for_calls, 1u);
        EXPECT_EQ(scheduler.diagnostics().worker_parallel_for_items,
                  static_cast<uint64_t>(kMachineCount));
        EXPECT_EQ(scheduler.diagnostics().commands_applied,
                  static_cast<uint64_t>(kMachineCount));
    }
    jobs.shutdown();

    ASSERT_EQ(events.events.size(), static_cast<size_t>(kMachineCount * 3));
    for (int index = 0; index < kMachineCount; ++index) {
        const size_t event_offset = static_cast<size_t>(index * 3);
        const MachineTickEvent& started = events.events[event_offset];
        const MachineTickEvent& completed = events.events[event_offset + 1];
        const MachineTickEvent& idled = events.events[event_offset + 2];
        EXPECT_EQ(started.kind, MachineTickEventKind::StateChanged);
        EXPECT_EQ(completed.kind, MachineTickEventKind::RecipeCompleted);
        EXPECT_EQ(idled.kind, MachineTickEventKind::StateChanged);
        EXPECT_EQ(started.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(completed.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(idled.entity_guid, expected_guids[static_cast<size_t>(index)]);
        EXPECT_EQ(started.state, MachineRunState::Running);
        EXPECT_EQ(idled.state, MachineRunState::Idle);

        const auto& machine = world.get_component<MachineRuntimeComponent>(
            entities[static_cast<size_t>(index)]);
        EXPECT_EQ(machine.state, MachineRunState::Idle);
        EXPECT_FALSE(machine.active_recipe.has_value());
        EXPECT_TRUE(machine.input_slots.empty());
        ASSERT_EQ(machine.output_slots.size(), 1u);
        EXPECT_EQ(machine.output_slots[0].item_id, "lead_ingot");
        EXPECT_EQ(machine.output_slots[0].count, 1);
    }
}

TEST(MachineTickSystemTest, ActiveRecipeSnapshotSurvivesScriptReload) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"copper_ore", "copper_ingot", "bronze_ingot"}));
    ASSERT_TRUE(content.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "copper_ingot", 2)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input_slots = {{"copper_ore", 2}};

    auto system = std::make_shared<MachineTickSystem>(content);
    ASSERT_TRUE(tick_machine(world, system));  // Starts the copied copper_ingot snapshot.
    ASSERT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.active_recipe->outputs[0].item_id, "copper_ingot");
    EXPECT_NE(machine.active_recipe->outputs[0].item_runtime_id,
              snt::core::kInvalidRuntimeKeyId);
    EXPECT_EQ(machine.active_recipe->item_runtime_index.find_id("copper_ingot"),
              machine.active_recipe->outputs[0].item_runtime_id);
    const uint64_t active_item_generation = machine.active_recipe->item_runtime_generation;

    ASSERT_TRUE(content.begin_reload(7));
    ASSERT_TRUE(content.register_script_item(
        7, {.id = "aaa_runtime_shift", .title_key = "item.runtime_shift", .max_stack = 1}));
    ASSERT_TRUE(content.register_script_recipe(
        7, make_recipe("snt.furnace.snapshot", "copper_ore", "bronze_ingot", 2)));
    ASSERT_TRUE(content.commit_reload(7));
    EXPECT_GT(content.item_runtime_generation(), active_item_generation);

    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "copper_ingot");

    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_TRUE(tick_machine(world, system));
    ASSERT_EQ(machine.output_slots.size(), 2U);
    EXPECT_EQ(machine.output_slots[1].item_id, "bronze_ingot");
}

TEST(MachineTickSystemTest, ReservesInputAndWaitsForEnergyWithoutProgressing) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"gold_ore", "gold_ingot"}));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.powered", "gold_ore", "gold_ingot", 2, 5)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input_slots = {{"gold_ore", 1}};
    machine.stored_energy = 5;

    auto system = std::make_shared<MachineTickSystem>(content);
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_TRUE(machine.input_slots.empty());
    EXPECT_EQ(machine.stored_energy, 0);

    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::WaitingForEnergy);
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_TRUE(machine.output_slots.empty());

    machine.stored_energy = 5;
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::Idle);
    ASSERT_EQ(machine.output_slots.size(), 1U);
    EXPECT_EQ(machine.output_slots[0].item_id, "gold_ingot");
}

TEST(MachineTickSystemTest, RequiresAllInputsAndManualActivationBeforeStarting) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"iron_crushed", "charcoal", "iron_bloom"}));
    MachineDefinition bloomery;
    bloomery.id = "bloomery";
    bloomery.display_name = "Bloomery";
    bloomery.tier = 1;
    bloomery.requires_manual_activation = true;
    ASSERT_TRUE(content.register_builtin_machine(std::move(bloomery)));

    RecipeDefinition recipe = make_recipe(
        "snt.bloomery.iron", "iron_crushed", "iron_bloom", 2);
    recipe.machine_id = "bloomery";
    recipe.inputs = {
        RecipeInputDefinition{"iron_crushed", 5},
        RecipeInputDefinition{"charcoal", 5},
    };
    ASSERT_TRUE(content.register_builtin_recipe(std::move(recipe)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "bloomery";
    machine.input_slots = {{"iron_crushed", 5}};

    CapturingMachineEvents events;
    auto system = std::make_shared<MachineTickSystem>(content, &events);
    system->set_tick_index(73);
    MachineInteractionService interactions(content);
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::NoMatchingRecipe);
    ASSERT_EQ(machine.input_slots.size(), 1u);
    EXPECT_EQ(machine.input_slots.front().count, 5);
    EXPECT_FALSE(interactions.request_manual_activation(
        world, world.guid_of(entity), {.target_is_reachable = true}));

    machine.input_slots.push_back({"charcoal", 5});
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::WaitingForActivation);
    EXPECT_FALSE(machine.active_recipe.has_value());
    ASSERT_EQ(machine.input_slots.size(), 2u);

    ASSERT_TRUE(interactions.request_manual_activation(
        world, world.guid_of(entity), {.target_is_reachable = true}));
    machine.job_owner_account_id = "account:machine-owner";
    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::Running);
    EXPECT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_TRUE(machine.input_slots.empty());
    EXPECT_FALSE(machine.activation_requested);
    EXPECT_FALSE(interactions.request_manual_activation(
        world, world.guid_of(entity), {.target_is_reachable = true}));

    ASSERT_TRUE(tick_machine(world, system));
    EXPECT_EQ(machine.state, MachineRunState::Idle);
    EXPECT_FALSE(machine.active_recipe.has_value());
    ASSERT_EQ(machine.output_slots.size(), 1u);
    EXPECT_EQ(machine.output_slots.front().item_id, "iron_bloom");
    EXPECT_EQ(machine.output_slots.front().count, 1);
    EXPECT_TRUE(machine.job_owner_account_id.empty());

    const auto completion = std::find_if(
        events.events.begin(), events.events.end(), [](const MachineTickEvent& event) {
            return event.kind == MachineTickEventKind::RecipeCompleted;
        });
    ASSERT_NE(completion, events.events.end());
    EXPECT_EQ(completion->tick_index, 73u);
    EXPECT_EQ(completion->account_id, "account:machine-owner");
    ASSERT_EQ(completion->outputs.size(), 1u);
    EXPECT_EQ(completion->outputs.front().item_id, "iron_bloom");
    EXPECT_EQ(completion->outputs.front().count, 1);
}

TEST(MachineTickSystemTest, ChoosesFirstMatchingRecipeInStableIdOrder) {
    GameContentRegistry content;
    ASSERT_TRUE(register_items(content, {"mixed_ore", "zinc_ingot", "copper_ingot"}));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.zinc", "mixed_ore", "zinc_ingot", 5)));
    ASSERT_TRUE(content.register_builtin_recipe(
        make_recipe("snt.furnace.copper", "mixed_ore", "copper_ingot", 5)));

    snt::ecs::World world;
    const auto entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.input_slots = {{"mixed_ore", 1}};

    auto system = std::make_shared<MachineTickSystem>(content);
    ASSERT_TRUE(tick_machine(world, system));

    ASSERT_TRUE(machine.active_recipe.has_value());
    EXPECT_EQ(machine.active_recipe->id, "snt.furnace.copper");
}
