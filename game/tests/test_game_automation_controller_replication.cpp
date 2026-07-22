#include "game/network/game_automation_controller_replication.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace snt::game::replication {
namespace {

GameAutomationControllerReplicationState make_controller(uint64_t revision = 7,
                                                          bool online = true) {
    return {
        .anchor_chunk = {"snt:overworld", 1, 2, 3},
        .anchor_entity_id = 0x2000000000000001ull,
        .root_x = 33,
        .root_y = 65,
        .root_z = 97,
        .controller_key = "automation.sfm_manager",
        .authoritative_revision = revision,
        .online = online,
        .sfm_program = {
            .revision = 4,
            .nodes = {
                {
                    .id = 10,
                    .type = SfmFlowNodeType::kInterval,
                    .interval_ticks = 20,
                },
                {
                    .id = 20,
                    .type = SfmFlowNodeType::kTransfer,
                    .transfer = {
                        .source = {.value = "machine.alpha.output"},
                        .destination = {.value = "machine.beta.input"},
                        .requested = ResourceContentStack::item("iron.ingot", 3),
                    },
                },
            },
            .connections = {{.source = 10, .destination = 20}},
        },
    };
}

TEST(GameAutomationControllerReplicationTest, RoundTripsDurableGraphPresentation) {
    const GameAutomationControllerReplicationSnapshot source{
        .controllers = {make_controller()},
    };
    auto encoded = encode_game_automation_controller_replication_snapshot(source);
    ASSERT_TRUE(encoded);
    auto decoded = decode_game_automation_controller_replication_snapshot(*encoded);
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded->controllers.size(), 1u);
    const GameAutomationControllerReplicationState& controller = decoded->controllers.front();
    EXPECT_EQ(controller.anchor_chunk.dimension_id, "snt:overworld");
    EXPECT_EQ(controller.anchor_entity_id, 0x2000000000000001ull);
    EXPECT_EQ(controller.controller_key, "automation.sfm_manager");
    EXPECT_TRUE(controller.online);
    ASSERT_EQ(controller.sfm_program.nodes.size(), 2u);
    EXPECT_EQ(controller.sfm_program.nodes[1].transfer.requested.key.id, "iron.ingot");
    EXPECT_EQ(controller.sfm_program.nodes[1].transfer.requested.amount, 3);
}

TEST(GameAutomationControllerReplicationTest, RemoteCacheUsesAnchorAndPositionIndexes) {
    const auto encoded = encode_game_automation_controller_replication_snapshot({
        .controllers = {make_controller()},
    });
    ASSERT_TRUE(encoded);
    GameRemoteAutomationControllerWorld remote;
    ASSERT_TRUE(remote.apply(GameSnapshot{
        .snapshot_id = 9,
        .values = {{
            .kind = GameReplicationValueKind::kAutomationControllers,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = *encoded,
        }},
    }));
    EXPECT_EQ(remote.controller_count(), 1u);
    const auto by_anchor = remote.find_controller(0x2000000000000001ull);
    ASSERT_TRUE(by_anchor);
    EXPECT_EQ(by_anchor->root_x, 33);
    const auto by_position = remote.find_controller_at("snt:overworld", 33, 65, 97);
    ASSERT_TRUE(by_position);
    EXPECT_EQ(by_position->authoritative_revision, 7u);

    const auto updated_payload = encode_game_automation_controller_replication_snapshot({
        .controllers = {make_controller(8, false)},
    });
    ASSERT_TRUE(updated_payload);
    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 9,
        .sequence = 1,
        .values = {{
            .kind = GameReplicationValueKind::kAutomationControllers,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = *updated_payload,
        }},
    }));
    const auto updated = remote.find_controller(0x2000000000000001ull);
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->authoritative_revision, 8u);
    EXPECT_FALSE(updated->online);

    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 9,
        .sequence = 2,
        .values = {{
            .kind = GameReplicationValueKind::kAutomationControllers,
            .operation = GameReplicationValueOperation::kRemove,
        }},
    }));
    EXPECT_EQ(remote.controller_count(), 0u);
    EXPECT_FALSE(remote.find_controller_at("snt:overworld", 33, 65, 97));
}

TEST(GameAutomationControllerReplicationTest, RejectsMalformedFlowNode) {
    GameAutomationControllerReplicationState malformed = make_controller();
    malformed.sfm_program.nodes[0].interval_ticks = 0;
    EXPECT_FALSE(encode_game_automation_controller_replication_snapshot({
        .controllers = {std::move(malformed)},
    }));
}

TEST(GameAutomationControllerReplicationTest, RejectsGraphThatExceedsSingleValueBudget) {
    GameAutomationControllerReplicationState oversized = make_controller();
    oversized.sfm_program.nodes.clear();
    oversized.sfm_program.connections.clear();
    constexpr size_t kTransferNodeCount = 128;
    constexpr size_t kMaximumEndpointAddressBytes = 512;
    const std::string endpoint(kMaximumEndpointAddressBytes, 'a');
    for (size_t index = 0; index < kTransferNodeCount; ++index) {
        oversized.sfm_program.nodes.push_back({
            .id = static_cast<SfmFlowNodeId>(index + 1),
            .type = SfmFlowNodeType::kTransfer,
            .transfer = {
                .source = {.value = endpoint},
                .destination = {.value = endpoint},
                .requested = ResourceContentStack::item("iron.ingot", 1),
            },
        });
    }
    auto state_size = measure_game_automation_controller_replication_state(oversized);
    ASSERT_TRUE(state_size);
    EXPECT_GT(*state_size + kGameAutomationControllerReplicationHeaderBytes,
              kMaxGameAutomationControllerReplicationPayloadBytes);
    EXPECT_FALSE(encode_game_automation_controller_replication_snapshot({
        .controllers = {std::move(oversized)},
    }));
}

}  // namespace
}  // namespace snt::game::replication
