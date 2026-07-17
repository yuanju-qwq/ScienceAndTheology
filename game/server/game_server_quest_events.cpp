// Dedicated-server bridge from committed gameplay events to quest values.

#define SNT_LOG_CHANNEL "game.server_quest_events"
#include "game/server/game_server_quest_events.h"

#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::replication {
namespace {

constexpr uint64_t kErrorLogIntervalTicks = 20;

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int32_t inventory_item_count(const GamePlayerInventory& inventory,
                                           std::string_view item_id) noexcept {
    int64_t count = 0;
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (stack.item_id != item_id || stack.count <= 0) continue;
        count = std::min<int64_t>(
            count + stack.count, std::numeric_limits<int32_t>::max());
    }
    return static_cast<int32_t>(count);
}

}  // namespace

GameServerQuestEventService::GameServerQuestEventService(QuestRegistry& quests) noexcept
    : quests_(&quests) {}

void GameServerQuestEventService::bind_player_state(
    GameServerPlayerState& player_state,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink) noexcept {
    player_state_ = &player_state;
    checkpoint_sink_ = checkpoint_sink;
}

void GameServerQuestEventService::unbind_player_state() noexcept {
    checkpoint_sink_ = nullptr;
    player_state_ = nullptr;
}

void GameServerQuestEventService::on_player_interaction(
    const GameServerPlayerInteractionEvent& event) {
    if (quests_ == nullptr || event.account_id.empty()) return;

    switch (event.kind) {
        case GameServerPlayerInteractionEventKind::kBlockMined:
            if (auto result = update_inventory_objectives(event.account_id, event.tick_index); !result) {
                record_error(event.tick_index, "block mine inventory", result.error());
            }
            record_progress(event.account_id, QuestObjectiveKind::kMineBlock, event.item_id, 1,
                            event.tick_index);
            break;
        case GameServerPlayerInteractionEventKind::kBlockPlaced:
        case GameServerPlayerInteractionEventKind::kMachineOutputCollected:
            if (auto result = update_inventory_objectives(event.account_id, event.tick_index); !result) {
                record_error(event.tick_index, "interaction inventory", result.error());
            }
            break;
        case GameServerPlayerInteractionEventKind::kMachinePlaced:
            if (auto result = update_inventory_objectives(event.account_id, event.tick_index); !result) {
                record_error(event.tick_index, "machine placement inventory", result.error());
            }
            record_progress(event.account_id, QuestObjectiveKind::kPlaceMachine, event.machine_id, 1,
                            event.tick_index);
            break;
        case GameServerPlayerInteractionEventKind::kBedUsed:
        case GameServerPlayerInteractionEventKind::kMachineActivated:
            break;
    }
}

void GameServerQuestEventService::on_machine_tick_event(const MachineTickEvent& event) {
    if (quests_ == nullptr || event.kind != MachineTickEventKind::RecipeCompleted ||
        event.account_id.empty()) {
        return;
    }
    for (const MachineItemStack& output : event.outputs) {
        if (output.empty()) continue;
        record_progress(event.account_id, QuestObjectiveKind::kCraftItem, output.item_id,
                        output.count, event.tick_index);
    }
}

snt::core::Expected<void> GameServerQuestEventService::grant_item_rewards(
    std::string_view player_id, std::string_view quest_id,
    std::span<const QuestItemReward> rewards) {
    if (player_state_ == nullptr) {
        return invalid_state("Quest reward service has no authoritative player state");
    }
    if (player_id.empty() || quest_id.empty()) {
        return invalid_state("Quest reward service received an invalid player or quest id");
    }
    if (rewards.empty()) return {};

    auto peer = player_state_->active_peer_for_account(player_id);
    if (!peer) return peer.error();

    GamePlayerInventoryTransaction transaction;
    transaction.additions.reserve(rewards.size());
    for (const QuestItemReward& reward : rewards) {
        if (reward.item_id.empty() || reward.count <= 0) {
            return invalid_state("Quest reward service received an invalid item reward");
        }
        transaction.additions.push_back({.item_id = reward.item_id, .count = reward.count});
    }
    auto can_apply = player_state_->can_apply_inventory_transaction(*peer, transaction);
    if (!can_apply) return can_apply.error();
    if (!*can_apply) {
        return invalid_state("Quest reward inventory transaction does not fit the active player");
    }
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(*peer); !result) {
            return result.error();
        }
    }
    if (auto result = player_state_->apply_inventory_transaction(*peer, transaction); !result) {
        return result.error();
    }
    SNT_LOG_INFO("Granted %zu claimed item reward stack(s) for quest '%.*s' to account '%.*s'",
                 rewards.size(), static_cast<int>(quest_id.size()), quest_id.data(),
                 static_cast<int>(player_id.size()), player_id.data());
    return {};
}

snt::core::Expected<void> GameServerQuestEventService::update_inventory_objectives(
    std::string_view account_id, uint64_t tick_index) {
    if (quests_ == nullptr) return invalid_state("Quest event service has no QuestRegistry");
    if (player_state_ == nullptr) {
        return invalid_state("Quest event service has no authoritative player state");
    }
    auto peer = player_state_->active_peer_for_account(account_id);
    if (!peer) return peer.error();
    auto inventory = player_state_->inventory_for_peer(*peer);
    if (!inventory) return inventory.error();
    return quests_->update_inventory(
        std::string(account_id),
        [&inventory](std::string_view item_id) {
            return inventory_item_count(*inventory, item_id);
        },
        tick_index);
}

void GameServerQuestEventService::record_progress(
    std::string_view account_id, QuestObjectiveKind kind, std::string_view target_id,
    int32_t amount, uint64_t tick_index) noexcept {
    if (target_id.empty() || amount <= 0 || quests_ == nullptr) return;
    if (auto result = quests_->record_progress(
            std::string(account_id),
            {.kind = kind, .target_id = std::string(target_id), .amount = amount}, tick_index);
        !result) {
        record_error(tick_index, "progress event", result.error());
    }
}

void GameServerQuestEventService::record_error(
    uint64_t tick_index, std::string_view source, const snt::core::Error& error) noexcept {
    ++suppressed_errors_;
    if (has_last_error_tick_ && tick_index - last_error_tick_ < kErrorLogIntervalTicks) return;
    SNT_LOG_WARN("Dropped %u quest bridge event(s); latest source=%.*s: %s",
                 suppressed_errors_, static_cast<int>(source.size()), source.data(),
                 error.format().c_str());
    suppressed_errors_ = 0;
    last_error_tick_ = tick_index;
    has_last_error_tick_ = true;
}

}  // namespace snt::game::replication
