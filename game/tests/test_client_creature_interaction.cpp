// Native creature selection and pointer-command mapping coverage.

#include "game/client/creature_interaction.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameClientCreatureInteractionController;
using snt::game::GameClientCreatureInteractionInput;
using snt::game::GameClientCreatureInteractionTarget;
using snt::game::GameClientCreatureInteractionTargetKind;
using snt::game::GameClientCreatureRaycast;
using snt::game::GameCreaturePresentationState;
using snt::game::CreatureRole;
using snt::game::replication::GameCaptiveCreatureFeedCommand;
using snt::game::replication::GameCreatureAttackCommand;
using snt::game::replication::GameCreatureCaptureCommand;

GameCreaturePresentationState make_creature(uint64_t id, float z) {
    GameCreaturePresentationState creature;
    creature.entity_id = id;
    creature.chunk = {"overworld", 0, 0, 0};
    creature.species_id = 1;
    creature.role = CreatureRole::HERBIVORE;
    creature.position_x = 0.0f;
    creature.position_y = 0.0f;
    creature.position_z = z;
    creature.health = 1.0f;
    return creature;
}

GameClientCreatureRaycast forward_raycast() {
    return {
        .origin_x = 0.0f,
        .origin_y = 0.5f,
        .origin_z = 0.0f,
        .direction_x = 0.0f,
        .direction_y = 0.0f,
        .direction_z = 1.0f,
        .max_distance_blocks = 5.0f,
    };
}

class RecordingCreatureCommandSink final
    : public snt::game::IGameClientCreatureInteractionCommandSink {
public:
    snt::core::Expected<void> submit_creature_attack(GameCreatureAttackCommand command) override {
        attack = std::move(command);
        return {};
    }

    snt::core::Expected<void> submit_creature_capture(GameCreatureCaptureCommand command) override {
        capture = std::move(command);
        return {};
    }

    snt::core::Expected<void> submit_captive_creature_feed(
        GameCaptiveCreatureFeedCommand command) override {
        feed = std::move(command);
        return {};
    }

    std::optional<GameCreatureAttackCommand> attack;
    std::optional<GameCreatureCaptureCommand> capture;
    std::optional<GameCaptiveCreatureFeedCommand> feed;
};

}  // namespace

TEST(GameClientCreatureInteractionTest, PicksNearestInteractiveOrCaptiveCreature) {
    GameCreaturePresentationState far_visual = make_creature(10, 1.0f);
    GameCreaturePresentationState wild = make_creature(20, 3.5f);
    wild.is_interactive = true;
    GameCreaturePresentationState captive = make_creature(30, 2.0f);
    captive.is_captive = true;

    const std::vector<GameCreaturePresentationState> creatures{
        far_visual, wild, captive,
    };
    const auto target = snt::game::pick_game_client_creature_interaction_target(
        creatures, "overworld", forward_raycast());

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->creature_entity_id, 30u);
    EXPECT_EQ(target->kind, GameClientCreatureInteractionTargetKind::kCaptive);
}

TEST(GameClientCreatureInteractionTest, RejectsCreatureBehindTerrainOrOutsideDimension) {
    GameCreaturePresentationState wild = make_creature(20, 3.5f);
    wild.is_interactive = true;
    GameClientCreatureRaycast raycast = forward_raycast();
    raycast.terrain_hit_distance_blocks = 2.5f;

    EXPECT_FALSE(snt::game::pick_game_client_creature_interaction_target(
        std::vector<GameCreaturePresentationState>{wild}, "overworld", raycast));

    raycast.terrain_hit_distance_blocks.reset();
    wild.chunk.dimension_id = "other_dimension";
    EXPECT_FALSE(snt::game::pick_game_client_creature_interaction_target(
        std::vector<GameCreaturePresentationState>{wild}, "overworld", raycast));
}

TEST(GameClientCreatureInteractionTest, MapsWildAndCaptiveInputToAuthorityCommands) {
    GameClientCreatureInteractionController controller;
    RecordingCreatureCommandSink sink;
    const GameClientCreatureInteractionTarget wild{
        .creature_entity_id = 44,
        .kind = GameClientCreatureInteractionTargetKind::kWild,
    };

    auto attacked = controller.handle_input(
        {.attack_pressed = true, .context_pressed = true}, wild, "", sink);
    ASSERT_TRUE(attacked) << attacked.error().format();
    EXPECT_TRUE(*attacked);
    ASSERT_TRUE(sink.attack.has_value());
    EXPECT_EQ(sink.attack->creature_entity_id, 44u);
    EXPECT_FALSE(sink.capture.has_value());

    sink = {};
    auto captured = controller.handle_input(
        {.context_pressed = true}, wild, "", sink);
    ASSERT_TRUE(captured) << captured.error().format();
    EXPECT_TRUE(*captured);
    ASSERT_TRUE(sink.capture.has_value());
    EXPECT_EQ(sink.capture->creature_entity_id, 44u);

    sink = {};
    const GameClientCreatureInteractionTarget captive{
        .creature_entity_id = 55,
        .kind = GameClientCreatureInteractionTargetKind::kCaptive,
    };
    auto fed = controller.handle_input(
        {.context_pressed = true}, captive, "purifying_pollen", sink);
    ASSERT_TRUE(fed) << fed.error().format();
    EXPECT_TRUE(*fed);
    ASSERT_TRUE(sink.feed.has_value());
    EXPECT_EQ(sink.feed->creature_entity_id, 55u);
    EXPECT_EQ(sink.feed->feed_item_id, "purifying_pollen");
}

TEST(GameClientCreatureInteractionTest, TamedCaptiveConsumesContextWithoutFeeding) {
    GameClientCreatureInteractionController controller;
    RecordingCreatureCommandSink sink;
    const GameClientCreatureInteractionTarget captive{
        .creature_entity_id = 77,
        .kind = GameClientCreatureInteractionTargetKind::kCaptive,
        .is_tamed = true,
    };

    auto handled = controller.handle_input(
        {.context_pressed = true}, captive, "purifying_pollen", sink);
    ASSERT_TRUE(handled) << handled.error().format();
    EXPECT_TRUE(*handled);
    EXPECT_FALSE(sink.attack.has_value());
    EXPECT_FALSE(sink.capture.has_value());
    EXPECT_FALSE(sink.feed.has_value());
}
