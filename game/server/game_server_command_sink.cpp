// Dedicated-server authoritative gameplay command sink implementation.

#define SNT_LOG_CHANNEL "game.server_commands"
#include "game/server/game_server_command_sink.h"

#include "game/server/game_server_player_movement.h"

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
    QuestRegistry& quests, IGameServerPlayerMovementInputSink* player_movement)
    : quests_(&quests), player_movement_(player_movement) {}

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

    GameQuestAcceptCommand accept;
    switch (static_cast<GameClientCommandType>(command.command_type)) {
        case GameClientCommandType::kQuestAccept: {
            auto parsed = parse_game_quest_accept_command(command);
            if (!parsed) {
                auto error = parsed.error();
                error.with_context("GameServerCommandSink::enqueue_client_command(QuestAccept)");
                return error;
            }
            accept = std::move(*parsed);
            break;
        }
        default:
            return protocol_error("Game client command type is not implemented by this server");
    }

    if (auto result = validate_and_advance_sequence(peer, command.client_sequence); !result) {
        return result.error();
    }
    pending_.push_back({
        .account_id = peer.identity.account_id,
        .peer = peer.peer,
        .client_sequence = command.client_sequence,
        .quest_id = std::move(accept.quest_id),
    });
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
    std::erase_if(pending_, [&peer](const PendingQuestAccept& command) {
        return command.peer == peer.peer;
    });
    pending_movement_.erase(peer.peer);
    if (player_movement_ != nullptr) player_movement_->on_peer_disconnected(peer, "peer disconnected");
}

snt::core::Expected<void> GameServerCommandSink::apply_pending_commands(uint64_t tick_index) {
    if (quests_ == nullptr) return invalid_state("Game server command sink has no QuestRegistry");

    std::sort(pending_.begin(), pending_.end(), [](const PendingQuestAccept& lhs,
                                                    const PendingQuestAccept& rhs) {
        if (lhs.account_id != rhs.account_id) return lhs.account_id < rhs.account_id;
        if (lhs.peer != rhs.peer) return lhs.peer < rhs.peer;
        return lhs.client_sequence < rhs.client_sequence;
    });

    for (const PendingQuestAccept& command : pending_) {
        if (auto result = quests_->accept(command.account_id, command.quest_id, tick_index); !result) {
            record_gameplay_rejection(tick_index, command, result.error());
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
    uint64_t tick_index, const PendingQuestAccept& command,
    const snt::core::Error& error) noexcept {
    ++suppressed_rejections_;
    if (has_rejection_log_tick_ && tick_index - last_rejection_log_tick_ < kRejectionLogIntervalTicks) {
        return;
    }

    SNT_LOG_WARN("Rejected %u authoritative game command(s); latest peer=%llu player='%s' quest='%s': %s",
                 suppressed_rejections_, static_cast<unsigned long long>(command.peer),
                 command.account_id.c_str(), command.quest_id.c_str(), error.format().c_str());
    suppressed_rejections_ = 0;
    last_rejection_log_tick_ = tick_index;
    has_rejection_log_tick_ = true;
}

}  // namespace snt::game::replication
