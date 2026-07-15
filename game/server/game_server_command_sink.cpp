// Dedicated-server authoritative gameplay command sink implementation.

#define SNT_LOG_CHANNEL "game.server_commands"
#include "game/server/game_server_command_sink.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxPendingAuthoritativeCommands = 1024;
constexpr uint64_t kRejectionLogIntervalTicks = 20;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

}  // namespace

GameServerCommandSink::GameServerCommandSink(QuestRegistry& quests) : quests_(&quests) {}

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
    if (pending_.size() >= kMaxPendingAuthoritativeCommands) {
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

    PeerSequenceState& sequence = sequences_[peer.peer];
    if (sequence.has_sequence && command.client_sequence <= sequence.last_sequence) {
        return protocol_error("Game client command sequence must increase strictly for one peer session");
    }
    sequence.last_sequence = command.client_sequence;
    sequence.has_sequence = true;
    pending_.push_back({
        .account_id = peer.identity.account_id,
        .peer = peer.peer,
        .client_sequence = command.client_sequence,
        .quest_id = std::move(accept.quest_id),
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
