#include "game/network/game_ae_network_replication.h"

#include <gtest/gtest.h>

namespace snt::game::replication {
namespace {

GameAeNetworkReplicationState make_node(uint64_t revision = 7, bool online = true) {
    return {
        .anchor_chunk = {"snt:overworld", 1, 2, 3},
        .anchor_entity_id = 0x2000000000000001ull,
        .root_x = 33,
        .root_y = 65,
        .root_z = 97,
        .type = AeNetworkNodeType::kController,
        .enabled = true,
        .online = online,
        .component_id = 41,
        .provided_channels = 32,
        .authoritative_revision = revision,
        .topology_revision = 19,
        .component_node_count = 3,
        .component_controller_count = 1,
        .component_total_channels = 32,
        .component_online_devices = 1,
        .component_offline_devices = 1,
        .component_powered = true,
    };
}

TEST(GameAeNetworkReplicationTest, RoundTripsPhysicalNodeAndComponentProjection) {
    const GameAeNetworkReplicationSnapshot source{.nodes = {make_node()}};
    const auto encoded = encode_game_ae_network_replication_snapshot(source);
    ASSERT_TRUE(encoded);
    const auto decoded = decode_game_ae_network_replication_snapshot(*encoded);
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded->nodes.size(), 1u);
    const GameAeNetworkReplicationState& node = decoded->nodes.front();
    EXPECT_EQ(node.anchor_chunk.dimension_id, "snt:overworld");
    EXPECT_EQ(node.anchor_entity_id, 0x2000000000000001ull);
    EXPECT_EQ(node.type, AeNetworkNodeType::kController);
    EXPECT_EQ(node.component_id, 41u);
    EXPECT_EQ(node.component_node_count, 3u);
    EXPECT_EQ(node.component_total_channels, 32);
    EXPECT_TRUE(node.component_powered);
}

TEST(GameAeNetworkReplicationTest, RemoteCacheUsesAnchorAndPositionIndexes) {
    const auto encoded = encode_game_ae_network_replication_snapshot({.nodes = {make_node()}});
    ASSERT_TRUE(encoded);
    GameRemoteAeNetworkWorld remote;
    ASSERT_TRUE(remote.apply(GameSnapshot{
        .snapshot_id = 9,
        .values = {{
            .kind = GameReplicationValueKind::kAeNetworkNodes,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = *encoded,
        }},
    }));
    EXPECT_EQ(remote.node_count(), 1u);
    const auto by_anchor = remote.find_node(0x2000000000000001ull);
    ASSERT_TRUE(by_anchor);
    EXPECT_EQ(by_anchor->root_x, 33);
    const auto by_position = remote.find_node_at("snt:overworld", 33, 65, 97);
    ASSERT_TRUE(by_position);
    EXPECT_EQ(by_position->authoritative_revision, 7u);

    auto changed = make_node(8, false);
    changed.component_powered = false;
    changed.component_controller_count = 2;
    changed.component_total_channels = 0;
    changed.component_online_devices = 0;
    changed.component_offline_devices = 2;
    changed.topology_revision = 20;
    const auto update = encode_game_ae_network_replication_snapshot({.nodes = {changed}});
    ASSERT_TRUE(update);
    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 9,
        .sequence = 1,
        .values = {{
            .kind = GameReplicationValueKind::kAeNetworkNodes,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = *update,
        }},
    }));
    const auto updated = remote.find_node(0x2000000000000001ull);
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->authoritative_revision, 8u);
    EXPECT_FALSE(updated->online);
    EXPECT_FALSE(updated->component_powered);

    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 9,
        .sequence = 2,
        .values = {{
            .kind = GameReplicationValueKind::kAeNetworkNodes,
            .operation = GameReplicationValueOperation::kRemove,
        }},
    }));
    EXPECT_EQ(remote.node_count(), 0u);
    EXPECT_FALSE(remote.find_node_at("snt:overworld", 33, 65, 97));
}

TEST(GameAeNetworkReplicationTest, RejectsInconsistentComponentSummary) {
    GameAeNetworkReplicationState malformed = make_node();
    malformed.component_powered = false;
    EXPECT_FALSE(encode_game_ae_network_replication_snapshot({.nodes = {std::move(malformed)}}));
}

}  // namespace
}  // namespace snt::game::replication
