// Client block interaction mapping tests.

#include "client_block_interaction.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameClientBlockInteractionController;
using snt::game::GameClientBlockInteractionInput;
using snt::game::GameClientBlockInteractionTarget;
using snt::game::GameClientInteractionConfig;
using snt::game::GameClientMachineInteractionTarget;
using snt::game::IGameClientBlockInteractionCommandSink;
using snt::game::replication::GameBlockInteractionAction;
using snt::game::replication::GameBlockInteractionCommand;

class RecordingBlockInteractionSink final : public IGameClientBlockInteractionCommandSink {
public:
    [[nodiscard]] snt::core::Expected<void> submit_block_interaction(
        GameBlockInteractionCommand command) override {
        commands.push_back(std::move(command));
        return {};
    }

    std::vector<GameBlockInteractionCommand> commands;
};

GameClientBlockInteractionTarget make_target(uint16_t material = 1) {
    return {
        .dimension_id = "overworld",
        .hit_x = 12,
        .hit_y = 64,
        .hit_z = -3,
        .hit_material = material,
        .placement = GameClientBlockInteractionTarget::PlacementCell{
            .x = 12,
            .y = 64,
            .z = -2,
            .expected_material = 0,
        },
    };
}

TEST(GameClientBlockInteractionControllerTest, MiningTakesPrecedenceOverContextInput) {
    GameClientBlockInteractionController controller(GameClientInteractionConfig{});
    RecordingBlockInteractionSink sink;
    const GameClientBlockInteractionInput input{
        .mine_pressed = true,
        .context_pressed = true,
    };
    const GameClientMachineInteractionTarget machine{
        .machine_id = "pit_kiln",
        .has_collectible_output = true,
        .requires_manual_activation = true,
        .activation_hints = 7,
    };

    ASSERT_TRUE(controller.handle_input(input, make_target(), "flint_and_steel", machine, sink));
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands.front().action, GameBlockInteractionAction::kMine);
    EXPECT_EQ(sink.commands.front().block_x, 12);
    EXPECT_EQ(sink.commands.front().block_y, 64);
    EXPECT_EQ(sink.commands.front().block_z, -3);
    EXPECT_EQ(sink.commands.front().expected_material, 1);
    EXPECT_TRUE(sink.commands.front().selected_item_id.empty());
}

TEST(GameClientBlockInteractionControllerTest, ContextUsesBedBeforeSelectedPlacementItem) {
    GameClientInteractionConfig config;
    config.bed_material_id = 5;
    GameClientBlockInteractionController controller(std::move(config));
    RecordingBlockInteractionSink sink;

    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, make_target(5), "dirt", std::nullopt, sink));
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands.front().action, GameBlockInteractionAction::kUse);
    EXPECT_EQ(sink.commands.front().block_x, 12);
    EXPECT_EQ(sink.commands.front().block_z, -3);
    EXPECT_TRUE(sink.commands.front().selected_item_id.empty());
}

TEST(GameClientBlockInteractionControllerTest, ContextCollectsOutputBeforeManualActivation) {
    GameClientBlockInteractionController controller(GameClientInteractionConfig{});
    RecordingBlockInteractionSink sink;
    const GameClientMachineInteractionTarget machine{
        .machine_id = "bloomery",
        .has_collectible_output = true,
        .requires_manual_activation = true,
        .activation_hints = 7,
    };

    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, make_target(10), "flint_and_steel", machine, sink));
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands.front().action,
              GameBlockInteractionAction::kCollectMachineOutput);
    EXPECT_TRUE(sink.commands.front().selected_item_id.empty());
    EXPECT_EQ(sink.commands.front().client_hints, 0);
}

TEST(GameClientBlockInteractionControllerTest, ContextActivatesManualMachineWithTrustedHints) {
    GameClientBlockInteractionController controller(GameClientInteractionConfig{});
    RecordingBlockInteractionSink sink;
    const GameClientMachineInteractionTarget machine{
        .machine_id = "anvil",
        .has_collectible_output = false,
        .requires_manual_activation = true,
        .activation_hints = 4,
    };

    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, make_target(11), "hammer", machine, sink));
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands.front().action, GameBlockInteractionAction::kActivateMachine);
    EXPECT_EQ(sink.commands.front().selected_item_id, "hammer");
    EXPECT_EQ(sink.commands.front().client_hints, 4);
}

TEST(GameClientBlockInteractionControllerTest, ContextPlacesSelectedItemIntoAdjacentCell) {
    GameClientBlockInteractionController controller(GameClientInteractionConfig{});
    RecordingBlockInteractionSink sink;

    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, make_target(), "stone", std::nullopt, sink));
    ASSERT_EQ(sink.commands.size(), 1u);
    const GameBlockInteractionCommand& command = sink.commands.front();
    EXPECT_EQ(command.action, GameBlockInteractionAction::kPlace);
    EXPECT_EQ(command.block_x, 12);
    EXPECT_EQ(command.block_y, 64);
    EXPECT_EQ(command.block_z, -2);
    EXPECT_EQ(command.expected_material, 0);
    EXPECT_EQ(command.selected_item_id, "stone");
}

TEST(GameClientBlockInteractionControllerTest, MissingTargetAndUnsupportedMachineDoNotSubmit) {
    GameClientBlockInteractionController controller(GameClientInteractionConfig{});
    RecordingBlockInteractionSink sink;

    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, std::nullopt, "stone", std::nullopt, sink));
    const GameClientMachineInteractionTarget automatic_machine{
        .machine_id = "furnace",
        .has_collectible_output = false,
        .requires_manual_activation = false,
    };
    ASSERT_TRUE(controller.handle_input(
        {.context_pressed = true}, make_target(7), "stone", automatic_machine, sink));
    EXPECT_TRUE(sink.commands.empty());
}

}  // namespace
