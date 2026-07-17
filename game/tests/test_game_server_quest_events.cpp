// Dedicated-server quest event and reward transaction coverage.

#include "game/server/game_server_quest_events.h"

#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/player/player_identity.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace {

using snt::game::GameContentRegistry;
using snt::game::GamePlayerInventoryTransaction;
using snt::game::QuestDefinition;
using snt::game::QuestObjectiveDefinition;
using snt::game::QuestObjectiveKind;
using snt::game::QuestRewardDefinition;
using snt::game::QuestRewardKind;
using snt::game::QuestState;
using snt::game::QuestRegistry;
using snt::game::MachineItemStack;
using snt::game::MachineTickEvent;
using snt::game::MachineTickEventKind;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameServerPlayerInteractionEvent;
using snt::game::replication::GameServerPlayerInteractionEventKind;
using snt::game::replication::GameServerPlayerState;
using snt::game::replication::GameServerQuestEventService;
using snt::game::replication::IGameServerPlayerStateCheckpointSink;

GameAuthenticatedPeer make_peer(snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

struct CheckpointSink final : IGameServerPlayerStateCheckpointSink {
    int marks = 0;

    snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer&) override {
        ++marks;
        return {};
    }
};

int32_t count_item(const snt::game::GamePlayerInventory& inventory,
                   std::string_view item_id) {
    int32_t count = 0;
    for (const snt::game::GamePlayerItemStack& stack : inventory.slots) {
        if (stack.item_id == item_id) count += stack.count;
    }
    return count;
}

}  // namespace

TEST(GameServerQuestEventServiceTest,
     CommittedInteractionsAndMachineOutputDriveProgressAndExplicitRewards) {
    GameContentRegistry content;
    QuestDefinition gate;
    gate.id = "p7.reward_gate";
    gate.title = "Reward gate";
    gate.description = "Stays incomplete during this test";
    gate.objectives = {
        {.id = "wait", .kind = QuestObjectiveKind::kReachTick, .target_id = "", .required_count = 100},
    };
    ASSERT_TRUE(content.register_builtin_quest(std::move(gate)));
    QuestDefinition unlocked;
    unlocked.id = "p7.unlocked";
    unlocked.title = "Unlocked";
    unlocked.description = "Unlocked by a claimed reward";
    unlocked.hidden = true;
    unlocked.prerequisites = {"p7.reward_gate"};
    unlocked.objectives = {
        {.id = "wait", .kind = QuestObjectiveKind::kReachTick, .target_id = "", .required_count = 100},
    };
    ASSERT_TRUE(content.register_builtin_quest(std::move(unlocked)));

    QuestDefinition quest;
    quest.id = "p7.event_flow";
    quest.title = "Event flow";
    quest.description = "Mine, collect, and craft";
    quest.objectives = {
        {.id = "mine.stone", .kind = QuestObjectiveKind::kMineBlock,
         .target_id = "stone", .required_count = 1},
        {.id = "acquire.charcoal", .kind = QuestObjectiveKind::kAcquireItem,
         .target_id = "charcoal", .required_count = 2},
        {.id = "craft.iron", .kind = QuestObjectiveKind::kCraftItem,
         .target_id = "iron_ingot", .required_count = 1},
    };
    quest.rewards = {
        {.kind = QuestRewardKind::kItem, .target_id = "wrought_iron", .count = 2},
        {.kind = QuestRewardKind::kUnlockQuest, .target_id = "p7.unlocked", .count = 1},
    };
    ASSERT_TRUE(content.register_builtin_quest(std::move(quest)));

    QuestRegistry quests(content);
    GameServerQuestEventService events(quests);
    quests.set_reward_sink(&events);
    snt::ecs::World world;
    auto player_state = GameServerPlayerState::create(
        world,
        {
            .spawn = {.dimension_id = "overworld", .position = {}},
            .inventory_slots = 6,
            .inventory_max_stack_size = 8,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const GameAuthenticatedPeer peer = make_peer(701, "Quest Event Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));
    CheckpointSink checkpoint;
    events.bind_player_state(*(*player_state), &checkpoint);

    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, GamePlayerInventoryTransaction{.additions = {{.item_id = "charcoal", .count = 2}}}));
    events.on_player_interaction({
        .kind = GameServerPlayerInteractionEventKind::kMachineOutputCollected,
        .account_id = peer.identity.account_id,
        .tick_index = 2,
    });
    events.on_player_interaction({
        .kind = GameServerPlayerInteractionEventKind::kBlockMined,
        .account_id = peer.identity.account_id,
        .tick_index = 3,
        .item_id = "stone",
    });
    events.on_machine_tick_event({
        .kind = MachineTickEventKind::RecipeCompleted,
        .tick_index = 4,
        .account_id = peer.identity.account_id,
        .outputs = {MachineItemStack{.item_id = "iron_ingot", .count = 1}},
    });

    const auto* progress = quests.find_progress(peer.identity.account_id, "p7.event_flow");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->state, QuestState::kCompleted);
    EXPECT_EQ(progress->objective_counts.at("mine.stone"), 1);
    EXPECT_EQ(progress->objective_counts.at("acquire.charcoal"), 2);
    EXPECT_EQ(progress->objective_counts.at("craft.iron"), 1);
    EXPECT_FALSE(progress->reward_claimed);

    ASSERT_TRUE(quests.claim_reward(peer.identity.account_id, "p7.event_flow", 5));
    progress = quests.find_progress(peer.identity.account_id, "p7.event_flow");
    ASSERT_NE(progress, nullptr);
    EXPECT_TRUE(progress->reward_claimed);
    const auto* unlocked_progress = quests.find_progress(peer.identity.account_id, "p7.unlocked");
    ASSERT_NE(unlocked_progress, nullptr);
    EXPECT_EQ(unlocked_progress->state, QuestState::kInProgress);

    auto inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(count_item(*inventory, "wrought_iron"), 2);
    EXPECT_EQ(checkpoint.marks, 1);
    EXPECT_FALSE(quests.claim_reward(peer.identity.account_id, "p7.event_flow", 6));

    events.unbind_player_state();
    (*player_state)->shutdown();
}
