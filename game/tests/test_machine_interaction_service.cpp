// Game-owned manual-machine interaction tests.

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "ecs/world.h"
#include "game_content_registry.h"
#include "machine_tick_system.h"
#include "game/simulation/machine_interaction_service.h"

namespace {

using snt::game::GameContentRegistry;
using snt::game::MachineActivationContext;
using snt::game::MachineActivationRequirements;
using snt::game::MachineDefinition;
using snt::game::MachineInteractionService;
using snt::game::MachineRunState;
using snt::game::MachineRuntimeComponent;

MachineDefinition make_manual_machine() {
    MachineDefinition machine;
    machine.id = "bloomery";
    machine.display_name = "Bloomery";
    machine.tier = 1;
    machine.requires_manual_activation = true;
    machine.activation_requirements = {
        .requires_cover = true,
        .requires_ignition = true,
        .requires_valid_structure = true,
        .required_tool_tag = "hammer",
    };
    return machine;
}

}  // namespace

TEST(MachineInteractionServiceTest, ValidatesPrerequisitesBeforeQueuingActivation) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_machine(make_manual_machine()));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "bloomery";
    machine.state = MachineRunState::WaitingForActivation;

    MachineInteractionService interactions(content);
    const snt::ecs::EntityGuid guid = world.guid_of(entity);

    MachineActivationContext context;
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));

    context.target_is_reachable = true;
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));
    context.cover_is_present = true;
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));
    context.ignition_is_present = true;
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));
    context.structure_is_valid = true;
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));
    context.held_tool_tags = {"chisel", "hammer"};

    ASSERT_TRUE(interactions.request_manual_activation(world, guid, context));
    EXPECT_TRUE(machine.activation_requested);
    EXPECT_FALSE(interactions.request_manual_activation(world, guid, context));
}

TEST(MachineInteractionServiceTest, RejectsTargetsThatDoNotNeedOrCannotStartManually) {
    GameContentRegistry content;
    MachineDefinition furnace;
    furnace.id = "furnace";
    furnace.display_name = "Furnace";
    furnace.tier = 1;
    ASSERT_TRUE(content.register_builtin_machine(std::move(furnace)));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.state = MachineRunState::WaitingForActivation;

    MachineInteractionService interactions(content);
    const MachineActivationContext context{.target_is_reachable = true};
    EXPECT_FALSE(interactions.request_manual_activation(world, world.guid_of(entity), context));

    MachineDefinition invalid;
    invalid.id = "invalid";
    invalid.display_name = "Invalid";
    invalid.tier = 1;
    invalid.activation_requirements.requires_cover = true;
    EXPECT_FALSE(content.register_builtin_machine(std::move(invalid)));
}
