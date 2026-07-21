// Native creature presentation replication and command protocol coverage.

#include "game/network/game_creature_replication.h"
#include "game/server/game_server_creature_replication.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::CreatureRole;
using snt::game::GameCreaturePresentationEvent;
using snt::game::GameCreaturePresentationEventKind;
using snt::game::GameCreaturePresentationState;
using snt::game::replication::GameCaptiveCreatureFeedCommand;
using snt::game::replication::GameCreatureAttackCommand;
using snt::game::replication::GameCreatureCaptureCommand;
using snt::game::replication::GameCreaturePresentationSnapshot;
using snt::game::replication::GameDelta;
using snt::game::replication::GameRemoteCreatureWorld;
using snt::game::replication::GameReplicationBudget;
using snt::game::replication::GameReplicationInterest;
using snt::game::replication::GameReplicationValueCollectionPhase;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameServerCreaturePresentationReplication;
using snt::game::replication::GameServerCreaturePresentationReplicationConfig;
using snt::game::replication::GameSnapshot;
using snt::game::replication::decode_game_creature_presentation_snapshot;
using snt::game::replication::encode_game_creature_presentation_snapshot;
using snt::game::replication::make_game_captive_creature_feed_command;
using snt::game::replication::make_game_creature_attack_command;
using snt::game::replication::make_game_creature_capture_command;
using snt::game::replication::parse_game_captive_creature_feed_command;
using snt::game::replication::parse_game_creature_attack_command;
using snt::game::replication::parse_game_creature_capture_command;

GameCreaturePresentationState make_creature(uint64_t id, int32_t chunk_x) {
    GameCreaturePresentationState creature;
    creature.entity_id = id;
    creature.chunk = {"overworld", chunk_x, 0, 0};
    creature.species_id = 1;
    creature.role = CreatureRole::HERBIVORE;
    creature.position_x = static_cast<float>(chunk_x * 16 + 2);
    creature.position_y = 1.0f;
    creature.position_z = 2.0f;
    creature.health = 0.8f;
    creature.is_interactive = true;
    return creature;
}

GameReplicationBudget creature_budget() {
    return {
        .max_reliable_bytes_per_tick = 4096,
        .max_value_snapshots_per_tick = 1,
    };
}

}  // namespace

TEST(GameCreaturePresentationReplicationTest, RoundTripsCompleteOrderedSnapshot) {
    GameCreaturePresentationState captive = make_creature(17, 0);
    captive.is_interactive = false;
    captive.is_captive = true;
    captive.is_tamed = true;
    const GameCreaturePresentationSnapshot original{
        .source_tick = 48,
        .creatures = {make_creature(9, -1), captive},
    };

    auto encoded = encode_game_creature_presentation_snapshot(original);
    ASSERT_TRUE(encoded) << encoded.error().format();
    auto decoded = decode_game_creature_presentation_snapshot(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_EQ(decoded->source_tick, original.source_tick);
    ASSERT_EQ(decoded->creatures.size(), 2u);
    EXPECT_EQ(decoded->creatures[0].entity_id, 9u);
    EXPECT_EQ(decoded->creatures[1].entity_id, 17u);
    EXPECT_TRUE(decoded->creatures[1].is_captive);
    EXPECT_TRUE(decoded->creatures[1].is_tamed);

    auto malformed = original;
    malformed.creatures[1].is_interactive = true;
    EXPECT_FALSE(encode_game_creature_presentation_snapshot(malformed));
}

TEST(GameCreaturePresentationReplicationTest, RemoteWorldReplacesVisibleSetOnDelta) {
    const GameCreaturePresentationSnapshot initial{
        .source_tick = 8,
        .creatures = {make_creature(10, 0)},
    };
    auto encoded_initial = encode_game_creature_presentation_snapshot(initial);
    ASSERT_TRUE(encoded_initial) << encoded_initial.error().format();

    GameRemoteCreatureWorld remote;
    ASSERT_TRUE(remote.apply({
        .snapshot_id = 71,
        .values = {{
            .kind = GameReplicationValueKind::kCreaturePresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*encoded_initial),
        }},
    }));
    ASSERT_TRUE(remote.find_creature(10).has_value());

    const GameCreaturePresentationSnapshot replacement{
        .source_tick = 9,
        .creatures = {make_creature(20, 1)},
    };
    auto encoded_replacement = encode_game_creature_presentation_snapshot(replacement);
    ASSERT_TRUE(encoded_replacement) << encoded_replacement.error().format();
    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 71,
        .sequence = 1,
        .values = {{
            .kind = GameReplicationValueKind::kCreaturePresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*encoded_replacement),
        }},
    }));
    EXPECT_FALSE(remote.find_creature(10).has_value());
    ASSERT_TRUE(remote.find_creature(20).has_value());
    EXPECT_EQ(remote.latest_source_tick(), 9u);
}

TEST(GameCreaturePresentationReplicationTest, ServerSourceFiltersCompleteSetByObserverChunkAoi) {
    auto source = GameServerCreaturePresentationReplication::create(
        GameServerCreaturePresentationReplicationConfig{.max_visible_creatures = 2});
    ASSERT_TRUE(source) << source.error().format();
    const GameCreaturePresentationState visible = make_creature(10, 0);
    const GameCreaturePresentationState hidden = make_creature(20, 2);
    (*source)->on_creature_presentation_event({
        .kind = GameCreaturePresentationEventKind::kSpawned,
        .source_tick = 15,
        .creature = hidden,
    });
    (*source)->on_creature_presentation_event({
        .kind = GameCreaturePresentationEventKind::kSpawned,
        .source_tick = 14,
        .creature = visible,
    });

    auto values = (*source)->collect_values(
        {}, {.chunks = {visible.chunk}}, creature_budget(), {.tick_index = 20},
        GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(values) << values.error().format();
    ASSERT_EQ(values->size(), 1u);
    EXPECT_EQ(values->front().kind, GameReplicationValueKind::kCreaturePresentation);
    auto decoded = decode_game_creature_presentation_snapshot(values->front().payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_EQ(decoded->source_tick, 15u);
    ASSERT_EQ(decoded->creatures.size(), 1u);
    EXPECT_EQ(decoded->creatures.front().entity_id, visible.entity_id);

    (*source)->on_creature_presentation_event({
        .kind = GameCreaturePresentationEventKind::kKilled,
        .source_tick = 21,
        .creature = visible,
    });
    values = (*source)->collect_values(
        {}, {.chunks = {visible.chunk}}, creature_budget(), {.tick_index = 21},
        GameReplicationValueCollectionPhase::kDelta);
    ASSERT_TRUE(values) << values.error().format();
    decoded = decode_game_creature_presentation_snapshot(values->front().payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_TRUE(decoded->creatures.empty());
    EXPECT_EQ(decoded->source_tick, 21u);
}

TEST(GameCreatureCommandProtocolTest, RoundTripsMinimalAuthorityInputs) {
    auto attack = make_game_creature_attack_command(81, {.creature_entity_id = 101});
    ASSERT_TRUE(attack) << attack.error().format();
    auto parsed_attack = parse_game_creature_attack_command(*attack);
    ASSERT_TRUE(parsed_attack) << parsed_attack.error().format();
    EXPECT_EQ(parsed_attack->creature_entity_id, 101u);

    auto capture = make_game_creature_capture_command(82, {.creature_entity_id = 102});
    ASSERT_TRUE(capture) << capture.error().format();
    auto parsed_capture = parse_game_creature_capture_command(*capture);
    ASSERT_TRUE(parsed_capture) << parsed_capture.error().format();
    EXPECT_EQ(parsed_capture->creature_entity_id, 102u);

    auto feed = make_game_captive_creature_feed_command(
        83, {.creature_entity_id = 103, .feed_item_id = "purifying_pollen"});
    ASSERT_TRUE(feed) << feed.error().format();
    auto parsed_feed = parse_game_captive_creature_feed_command(*feed);
    ASSERT_TRUE(parsed_feed) << parsed_feed.error().format();
    EXPECT_EQ(parsed_feed->creature_entity_id, 103u);
    EXPECT_EQ(parsed_feed->feed_item_id, "purifying_pollen");

    EXPECT_FALSE(make_game_creature_attack_command(84, {.creature_entity_id = 0}));
    EXPECT_FALSE(make_game_creature_capture_command(85, {.creature_entity_id = 0}));
    EXPECT_FALSE(make_game_captive_creature_feed_command(
        86, {.creature_entity_id = 103, .feed_item_id = ""}));
}
