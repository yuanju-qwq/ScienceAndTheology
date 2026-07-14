// Game-owned dedicated-server replication admission and command boundary.

#define SNT_LOG_CHANNEL "game.replication"
#include "game/network/game_server_replication_handler.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

}  // namespace

snt::core::Expected<GameAuthenticatedPeer> ClosedGamePeerAuthenticator::authenticate(
    snt::network::PeerId, const GameLoginRequest&,
    const snt::network::ReplicationTickContext&) {
    return protocol_error(
        "Game peer authentication is not configured; install an IGamePeerAuthenticator before "
        "admitting players");
}

void ClosedGamePeerAuthenticator::on_peer_disconnected(const GameAuthenticatedPeer&,
                                                        std::string_view) noexcept {}

GameServerReplicationHandler::GameServerReplicationHandler(
    IGamePeerAuthenticator& authenticator, IGameReplicationCommandSink* command_sink)
    : authenticator_(&authenticator), command_sink_(command_sink) {}

snt::core::Expected<void> GameServerReplicationHandler::on_peer_connected(
    snt::network::PeerId peer, const snt::network::ReplicationTickContext&) {
    if (peer == snt::network::kInvalidPeerId) {
        return protocol_error("Game replication received an invalid peer id");
    }
    const auto [iterator, inserted] = peers_.try_emplace(peer);
    static_cast<void>(iterator);
    if (!inserted) return protocol_error("Game replication peer connected twice");

    SNT_LOG_INFO("Replication peer %llu connected; awaiting game login",
                 static_cast<unsigned long long>(peer));
    return {};
}

snt::core::Expected<void> GameServerReplicationHandler::on_frame(
    snt::network::PeerId peer, const snt::network::ReplicationFrame& frame,
    const snt::network::ReplicationTickContext& context) {
    const auto state = peers_.find(peer);
    if (state == peers_.end()) {
        return protocol_error("Game replication frame arrived before peer connection");
    }
    if (frame.protocol_version != snt::network::kCurrentReplicationProtocolVersion) {
        return protocol_error("Replication transport protocol version does not match");
    }

    auto message = decode_game_replication_message(frame.payload);
    if (!message) {
        auto error = message.error();
        error.with_context("GameServerReplicationHandler::on_frame(decode message)");
        return error;
    }
    if (!is_client_game_replication_message(message->kind)) {
        return protocol_error("Dedicated server received a server-originated game replication message");
    }
    if (auto result = validate_game_replication_channel(message->kind, frame.channel); !result) {
        auto error = result.error();
        error.with_context("GameServerReplicationHandler::on_frame(channel)");
        return error;
    }

    switch (message->kind) {
        case GameReplicationMessageKind::kClientLoginRequest:
            return handle_login(peer, state->second, *message, context);
        case GameReplicationMessageKind::kClientCommand:
            return handle_command(peer, state->second, *message, context);
        case GameReplicationMessageKind::kClientInterestUpdate:
        case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
            return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                    "Game replication message is declared but not implemented"};
        case GameReplicationMessageKind::kServerLoginAccepted:
        case GameReplicationMessageKind::kServerSnapshot:
        case GameReplicationMessageKind::kServerDelta:
        case GameReplicationMessageKind::kServerNotice:
            return protocol_error("Dedicated server received a server-originated message");
    }
    return protocol_error("Dedicated server received an unknown game replication message");
}

void GameServerReplicationHandler::on_peer_disconnected(snt::network::PeerId peer,
                                                         std::string_view reason) noexcept {
    const auto state = peers_.find(peer);
    if (state == peers_.end()) return;

    if (state->second.authenticated_peer.has_value()) {
        authenticator_->on_peer_disconnected(*state->second.authenticated_peer, reason);
        SNT_LOG_INFO("Authenticated replication peer %llu disconnected: %.*s",
                     static_cast<unsigned long long>(peer),
                     static_cast<int>(reason.size()), reason.data());
    } else {
        SNT_LOG_INFO("Unauthenticated replication peer %llu disconnected: %.*s",
                     static_cast<unsigned long long>(peer),
                     static_cast<int>(reason.size()), reason.data());
    }
    peers_.erase(state);
    std::erase_if(pending_outbound_, [peer](const PendingOutboundFrame& pending) {
        return pending.peer == peer;
    });
}

snt::core::Expected<void> GameServerReplicationHandler::emit_outbound(
    const snt::network::ReplicationTickContext&,
    snt::network::IReplicationFrameSink& sink) {
    size_t sent = 0;
    for (; sent < pending_outbound_.size(); ++sent) {
        const PendingOutboundFrame& pending = pending_outbound_[sent];
        if (auto result = sink.send(pending.peer, pending.frame); !result) {
            if (sent > 0) {
                pending_outbound_.erase(pending_outbound_.begin(),
                                        pending_outbound_.begin() + static_cast<std::ptrdiff_t>(sent));
            }
            auto error = result.error();
            error.with_context("GameServerReplicationHandler::emit_outbound(login accepted)");
            return error;
        }
    }
    pending_outbound_.clear();
    return {};
}

snt::core::Expected<void> GameServerReplicationHandler::handle_login(
    snt::network::PeerId peer, PeerState& state, const GameReplicationMessage& message,
    const snt::network::ReplicationTickContext& context) {
    if (state.authenticated_peer.has_value()) {
        return protocol_error("Game replication peer attempted to log in twice");
    }

    auto request = parse_game_login_request(message);
    if (!request) {
        auto error = request.error();
        error.with_context("GameServerReplicationHandler::handle_login(parse request)");
        return error;
    }
    auto authenticated = authenticator_->authenticate(peer, *request, context);
    if (!authenticated) {
        auto error = authenticated.error();
        error.with_context("GameServerReplicationHandler::handle_login(authenticate)");
        return error;
    }

    GameAuthenticatedPeer identity = std::move(*authenticated);
    if (identity.player_id.empty() || identity.player_id.size() > kMaxGamePlayerIdBytes) {
        authenticator_->on_peer_disconnected(identity, "authenticator returned an invalid player id");
        return protocol_error("Game authenticator returned an invalid player id");
    }
    if (is_player_id_in_use(identity.player_id, peer)) {
        authenticator_->on_peer_disconnected(identity, "player id is already connected");
        return protocol_error("Game player id is already connected");
    }

    auto accepted = make_game_login_accepted({.player_id = identity.player_id});
    if (!accepted) {
        authenticator_->on_peer_disconnected(identity, "login accepted payload could not be encoded");
        return accepted.error();
    }
    auto payload = encode_game_replication_message(*accepted);
    if (!payload) {
        authenticator_->on_peer_disconnected(identity, "login accepted envelope could not be encoded");
        return payload.error();
    }

    state.authenticated_peer = std::move(identity);
    pending_outbound_.push_back({
        .peer = peer,
        .frame = {
            .protocol_version = snt::network::kCurrentReplicationProtocolVersion,
            .server_tick = context.tick_index,
            .channel = snt::network::ReplicationChannel::Reliable,
            .payload = std::move(*payload),
        },
    });
    SNT_LOG_INFO("Replication peer %llu authenticated as '%s'",
                 static_cast<unsigned long long>(peer),
                 state.authenticated_peer->player_id.c_str());
    return {};
}

snt::core::Expected<void> GameServerReplicationHandler::handle_command(
    snt::network::PeerId, const PeerState& state, const GameReplicationMessage& message,
    const snt::network::ReplicationTickContext& context) {
    if (!state.authenticated_peer.has_value()) {
        return protocol_error("Game client command arrived before successful login");
    }
    if (command_sink_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                "Dedicated server has no game command sink"};
    }

    auto command = parse_game_client_command(message);
    if (!command) {
        auto error = command.error();
        error.with_context("GameServerReplicationHandler::handle_command(parse command)");
        return error;
    }
    auto result = command_sink_->enqueue_client_command(*state.authenticated_peer,
                                                         std::move(*command), context);
    if (!result) {
        auto error = result.error();
        error.with_context("GameServerReplicationHandler::handle_command(enqueue)");
        return error;
    }
    return {};
}

bool GameServerReplicationHandler::is_player_id_in_use(std::string_view player_id,
                                                        snt::network::PeerId except_peer) const {
    return std::any_of(peers_.begin(), peers_.end(), [player_id, except_peer](const auto& entry) {
        return entry.first != except_peer && entry.second.authenticated_peer.has_value() &&
               entry.second.authenticated_peer->player_id == player_id;
    });
}

}  // namespace snt::game::replication
