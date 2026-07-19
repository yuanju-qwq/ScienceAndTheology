// Dedicated-server authoritative gameplay command sink implementation.

#define SNT_LOG_CHANNEL "game.server_commands"
#include "game/server/game_server_command_sink.h"

#include "game/server/game_server_inventory_replication.h"
#include "game/server/game_server_player_movement.h"
#include "game/server/game_server_player_interaction.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxPendingAuthoritativeCommands = 1024;
constexpr uint64_t kRejectionLogIntervalTicks = 20;

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

}  // namespace

GameServerCommandSink::GameServerCommandSink(
    QuestRegistry& quests, IGameServerPlayerMovementInputSink* player_movement,
    IGameServerPlayerInteractionService* player_interactions,
    GameServerInventoryReplication* inventory_replication)
    : quests_(&quests), player_movement_(player_movement),
      player_interactions_(player_interactions), inventory_replication_(inventory_replication) {}

snt::core::Expected<void> GameServerCommandSink::enqueue_client_command(
    const GameAuthenticatedPeer& peer, GameClientCommand command,
    const snt::network::ReplicationTickContext&) {
    if (quests_ == nullptr) return invalid_state("Game server command sink has no QuestRegistry");
    if (peer.peer == snt::network::kInvalidPeerId) {
        return protocol_error("Authoritative game command has no authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) {
        return protocol_error("Authoritative game command has an invalid authenticated player identity");
    }
    if (pending_.size() + pending_movement_.size() >= kMaxPendingAuthoritativeCommands) {
        return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                "Authoritative game command queue limit exceeded"};
    }

    PendingCommand pending{
        .peer = peer,
        .client_sequence = command.client_sequence,
    };
    switch (static_cast<GameClientCommandType>(command.command_type)) {
        case GameClientCommandType::kQuestClaimReward: {
            auto parsed = parse_game_quest_claim_reward_command(command);
            if (!parsed) {
                auto error = parsed.error();
                error.with_context(
                    "GameServerCommandSink::enqueue_client_command(QuestClaimReward)");
                return error;
            }
            pending.type = GameClientCommandType::kQuestClaimReward;
            pending.quest_claim_reward = std::move(*parsed);
            break;
        }
        case GameClientCommandType::kBlockInteraction: {
            if (player_interactions_ == nullptr) {
                return snt::core::Error{
                    snt::core::ErrorCode::kNotImplemented,
                    "Dedicated server has no host player interaction service"};
            }
            auto parsed = parse_game_block_interaction_command(command);
            if (!parsed) {
                auto error = parsed.error();
                error.with_context(
                    "GameServerCommandSink::enqueue_client_command(BlockInteraction)");
                return error;
            }
            pending.type = GameClientCommandType::kBlockInteraction;
            pending.block_interaction = std::move(*parsed);
            break;
        }
        case GameClientCommandType::kInventorySlotTransfer: {
            if (inventory_replication_ == nullptr) {
                return snt::core::Error{
                    snt::core::ErrorCode::kNotImplemented,
                    "Dedicated server has no authoritative inventory replication service"};
            }
            auto parsed = parse_game_inventory_slot_transfer_command(command);
            if (!parsed) {
                auto error = parsed.error();
                error.with_context(
                    "GameServerCommandSink::enqueue_client_command(InventorySlotTransfer)");
                return error;
            }
            pending.type = GameClientCommandType::kInventorySlotTransfer;
            pending.inventory_slot_transfer = std::move(*parsed);
            break;
        }
        case GameClientCommandType::kMachineInputSlotTransfer: {
            if (player_interactions_ == nullptr) {
                return snt::core::Error{
                    snt::core::ErrorCode::kNotImplemented,
                    "Dedicated server has no host player interaction service"};
            }
            auto parsed = parse_game_machine_input_slot_transfer_command(command);
            if (!parsed) {
                auto error = parsed.error();
                error.with_context(
                    "GameServerCommandSink::enqueue_client_command(MachineInputSlotTransfer)");
                return error;
            }
            pending.type = GameClientCommandType::kMachineInputSlotTransfer;
            pending.machine_input_slot_transfer = std::move(*parsed);
            break;
        }
        default:
            return protocol_error("Game client command type is not implemented by this server");
    }

    if (auto result = validate_and_advance_sequence(peer, command.client_sequence); !result) {
        return result.error();
    }
    pending_.push_back(std::move(pending));
    return {};
}

snt::core::Expected<void> GameServerCommandSink::enqueue_player_movement_input(
    const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
    const snt::network::ReplicationTickContext&) {
    if (quests_ == nullptr) return invalid_state("Game server command sink has no QuestRegistry");
    if (player_movement_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                "Dedicated server has no authoritative player movement service"};
    }
    if (peer.peer == snt::network::kInvalidPeerId) {
        return protocol_error("Authoritative player movement input has no authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) {
        return protocol_error("Authoritative player movement input has an invalid player identity");
    }
    if (auto result = validate_game_player_movement_input(input); !result) {
        return result.error();
    }
    const bool replaces_pending_input = pending_movement_.contains(peer.peer);
    if (!replaces_pending_input &&
        pending_.size() + pending_movement_.size() >= kMaxPendingAuthoritativeCommands) {
        return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                "Authoritative game command queue limit exceeded"};
    }
    if (auto result = validate_and_advance_sequence(peer, input.client_sequence); !result) {
        return result.error();
    }
    pending_movement_.insert_or_assign(peer.peer, PendingMovementInput{
        .peer = peer,
        .client_sequence = input.client_sequence,
        .input = std::move(input),
    });
    return {};
}

void GameServerCommandSink::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                  std::string_view) noexcept {
    if (peer.peer == snt::network::kInvalidPeerId) return;
    sequences_.erase(peer.peer);
    std::erase_if(pending_, [&peer](const PendingCommand& command) {
        return command.peer.peer == peer.peer;
    });
    pending_movement_.erase(peer.peer);
    if (player_movement_ != nullptr) player_movement_->on_peer_disconnected(peer, "peer disconnected");
}

snt::core::Expected<void> GameServerCommandSink::apply_pending_commands(uint64_t tick_index) {
    if (quests_ == nullptr) return invalid_state("Game server command sink has no QuestRegistry");

    std::sort(pending_.begin(), pending_.end(), [](const PendingCommand& lhs,
                                                    const PendingCommand& rhs) {
        if (lhs.peer.identity.account_id != rhs.peer.identity.account_id) {
            return lhs.peer.identity.account_id < rhs.peer.identity.account_id;
        }
        if (lhs.peer.peer != rhs.peer.peer) return lhs.peer.peer < rhs.peer.peer;
        return lhs.client_sequence < rhs.client_sequence;
    });

    for (const PendingCommand& command : pending_) {
        switch (command.type) {
            case GameClientCommandType::kQuestClaimReward:
                if (auto result = quests_->claim_reward(
                        command.peer.identity.account_id,
                        command.quest_claim_reward.quest_id, tick_index);
                    !result) {
                    record_gameplay_rejection(tick_index, command, result.error());
                }
                break;
            case GameClientCommandType::kBlockInteraction:
                if (player_interactions_ == nullptr) {
                    record_gameplay_rejection(
                        tick_index, command,
                        invalid_state("Game server command sink lost its player interaction service"));
                    break;
                }
                if (auto result = player_interactions_->apply_block_interaction(
                        command.peer, command.block_interaction, tick_index);
                    !result) {
                    record_gameplay_rejection(tick_index, command, result.error());
                }
                break;
            case GameClientCommandType::kInventorySlotTransfer:
                if (inventory_replication_ == nullptr) {
                    record_gameplay_rejection(
                        tick_index, command,
                        invalid_state("Game server command sink lost its inventory replication service"));
                    break;
                }
                if (auto result = inventory_replication_->submit_slot_transfer(
                        command.peer, command.inventory_slot_transfer);
                    !result) {
                    record_gameplay_rejection(tick_index, command, result.error());
                }
                break;
            case GameClientCommandType::kMachineInputSlotTransfer:
                if (player_interactions_ == nullptr) {
                    record_gameplay_rejection(
                        tick_index, command,
                        invalid_state("Game server command sink lost its player interaction service"));
                    break;
                }
                if (auto result = player_interactions_->submit_machine_input_slot_transfer(
                        command.peer, command.machine_input_slot_transfer, tick_index);
                    !result) {
                    record_gameplay_rejection(tick_index, command, result.error());
                }
                break;
        }
    }
    pending_.clear();

    std::vector<PendingMovementInput> movement_inputs;
    movement_inputs.reserve(pending_movement_.size());
    for (const auto& [peer, input] : pending_movement_) {
        static_cast<void>(peer);
        movement_inputs.push_back(input);
    }
    pending_movement_.clear();
    std::sort(movement_inputs.begin(), movement_inputs.end(),
              [](const PendingMovementInput& lhs, const PendingMovementInput& rhs) {
                  if (lhs.peer.identity.account_id != rhs.peer.identity.account_id) {
                      return lhs.peer.identity.account_id < rhs.peer.identity.account_id;
                  }
                  if (lhs.peer.peer != rhs.peer.peer) return lhs.peer.peer < rhs.peer.peer;
                  return lhs.client_sequence < rhs.client_sequence;
              });
    for (PendingMovementInput& input : movement_inputs) {
        if (player_movement_ == nullptr) {
            return invalid_state("Game server command sink lost its authoritative player movement service");
        }
        if (auto result = player_movement_->enqueue_player_movement_input(
                input.peer, std::move(input.input),
                {.tick_index = tick_index, .delta_seconds = 0.0f});
            !result) {
            auto error = result.error();
            error.with_context("GameServerCommandSink::apply_pending_commands(player movement)");
            return error;
        }
    }
    return {};
}

snt::core::Expected<void> GameServerCommandSink::validate_and_advance_sequence(
    const GameAuthenticatedPeer& peer, uint64_t client_sequence) {
    if (client_sequence == 0) {
        return protocol_error("Game client command sequence must be non-zero");
    }
    PeerSequenceState& sequence = sequences_[peer.peer];
    if (sequence.has_sequence && client_sequence <= sequence.last_sequence) {
        return protocol_error("Game client command sequence must increase strictly for one peer session");
    }
    sequence.last_sequence = client_sequence;
    sequence.has_sequence = true;
    return {};
}

void GameServerCommandSink::record_gameplay_rejection(
    uint64_t tick_index, const PendingCommand& command,
    const snt::core::Error& error) noexcept {
    ++suppressed_rejections_;
    if (has_rejection_log_tick_ && tick_index - last_rejection_log_tick_ < kRejectionLogIntervalTicks) {
        return;
    }

    const char* command_name = "unknown";
    switch (command.type) {
        case GameClientCommandType::kQuestClaimReward:
            command_name = "quest_claim_reward";
            break;
        case GameClientCommandType::kBlockInteraction:
            command_name = "block_interaction";
            break;
        case GameClientCommandType::kInventorySlotTransfer:
            command_name = "inventory_slot_transfer";
            break;
        case GameClientCommandType::kMachineInputSlotTransfer:
            command_name = "machine_input_slot_transfer";
            break;
    }
    SNT_LOG_WARN("Rejected %u host game command(s); latest peer=%llu player='%s' command=%s: %s",
                 suppressed_rejections_,
                 static_cast<unsigned long long>(command.peer.peer),
                 command.peer.identity.account_id.c_str(), command_name,
                 error.format().c_str());
    suppressed_rejections_ = 0;
    last_rejection_log_tick_ = tick_index;
    has_rejection_log_tick_ = true;
}

}  // namespace snt::game::replication
