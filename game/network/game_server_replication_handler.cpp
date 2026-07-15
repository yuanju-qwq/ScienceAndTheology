// Game-owned dedicated-server replication admission and command boundary.

#define SNT_LOG_CHANNEL "game.replication"
#include "game/network/game_server_replication_handler.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
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
    IGamePeerAuthenticator& authenticator, IGameReplicationCommandSink* command_sink,
    IGamePlayerSessionLifecycle* player_lifecycle,
    IGameReplicationInterestProvider* interest_provider,
    IGameReplicationSnapshotSource* snapshot_source,
    GameReplicationBudget replication_budget)
    : authenticator_(&authenticator), command_sink_(command_sink),
      player_lifecycle_(player_lifecycle), interest_provider_(interest_provider),
      snapshot_source_(snapshot_source), replication_budget_(replication_budget) {}

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
    if (state->second.disconnecting) {
        return protocol_error("Game replication peer has been superseded by a newer account login");
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
        if (command_sink_ != nullptr) {
            command_sink_->on_peer_disconnected(*state->second.authenticated_peer, reason);
        }
        if (player_lifecycle_ != nullptr) {
            player_lifecycle_->on_peer_disconnected(*state->second.authenticated_peer, reason);
        }
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
    std::erase_if(pending_disconnects_, [peer](const PendingPeerDisconnect& pending) {
        return pending.peer == peer;
    });
}

snt::core::Expected<void> GameServerReplicationHandler::emit_outbound(
    const snt::network::ReplicationTickContext& context,
    snt::network::IReplicationFrameSink& sink) {
    for (size_t index = 0; index < pending_disconnects_.size();) {
        const PendingPeerDisconnect& pending = pending_disconnects_[index];
        const auto state = peers_.find(pending.peer);
        if (state == peers_.end()) {
            pending_disconnects_.erase(pending_disconnects_.begin() +
                                       static_cast<std::ptrdiff_t>(index));
            continue;
        }
        if (auto result = sink.disconnect(pending.peer, pending.reason); !result) {
            auto error = result.error();
            error.with_context("GameServerReplicationHandler::emit_outbound(account takeover)");
            return error;
        }
        SNT_LOG_INFO("Replication peer %llu disconnected after account session takeover",
                     static_cast<unsigned long long>(pending.peer));
        peers_.erase(state);
        pending_disconnects_.erase(pending_disconnects_.begin() +
                                   static_cast<std::ptrdiff_t>(index));
    }

    if ((interest_provider_ == nullptr) != (snapshot_source_ == nullptr)) {
        return invalid_state(
            "Game replication requires both an interest provider and a snapshot source");
    }
    if (interest_provider_ != nullptr) {
        std::vector<snt::network::PeerId> peers;
        peers.reserve(peers_.size());
        for (const auto& [peer, state] : peers_) {
            if (!state.disconnecting && state.authenticated_peer.has_value()) {
                peers.push_back(peer);
            }
        }
        std::sort(peers.begin(), peers.end());
        for (const snt::network::PeerId peer : peers) {
            const auto state = peers_.find(peer);
            if (state == peers_.end() || state->second.disconnecting ||
                !state->second.authenticated_peer.has_value()) {
                continue;
            }
            if (has_pending_replication(peer)) continue;
            if (auto result = emit_replication_for_peer(peer, state->second, context); !result) {
                return result.error();
            }
        }
    }

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

snt::core::Expected<void> GameServerReplicationHandler::emit_replication_for_peer(
    snt::network::PeerId peer,
    PeerState& state,
    const snt::network::ReplicationTickContext& context) {
    if (!state.authenticated_peer.has_value() || interest_provider_ == nullptr ||
        snapshot_source_ == nullptr) {
        return invalid_state("Game replication peer has no active synchronization services");
    }

    auto interest = interest_provider_->compute_interest(*state.authenticated_peer, context);
    if (!interest) {
        auto error = interest.error();
        error.with_context("GameServerReplicationHandler::emit_replication_for_peer(interest)");
        return error;
    }

    if (!state.initial_snapshot_emitted) {
        auto messages = snapshot_source_->build_initial_snapshot(
            *state.authenticated_peer, *interest, replication_budget_, context);
        if (!messages) {
            auto error = messages.error();
            error.with_context("GameServerReplicationHandler::emit_replication_for_peer(snapshot)");
            return error;
        }
        if (auto result = queue_replication_messages(
                peer, *messages, GameReplicationMessageKind::kServerSnapshot, context);
            !result) {
            return result.error();
        }
        state.initial_snapshot_emitted = !messages->empty();
        return {};
    }

    auto messages = snapshot_source_->build_deltas(
        *state.authenticated_peer, *interest, replication_budget_, context);
    if (!messages) {
        auto error = messages.error();
        error.with_context("GameServerReplicationHandler::emit_replication_for_peer(delta)");
        return error;
    }
    return queue_replication_messages(
        peer, *messages, GameReplicationMessageKind::kServerDelta, context);
}

snt::core::Expected<void> GameServerReplicationHandler::queue_replication_messages(
    snt::network::PeerId peer,
    const std::vector<GameReplicationMessage>& messages,
    GameReplicationMessageKind expected_kind,
    const snt::network::ReplicationTickContext& context) {
    uint64_t total_bytes = 0;
    uint64_t total_chunks = 0;
    uint64_t total_entities = 0;
    uint64_t total_block_deltas = 0;
    std::vector<PendingOutboundFrame> frames;
    frames.reserve(messages.size());

    for (const GameReplicationMessage& message : messages) {
        if (message.kind != expected_kind) {
            return protocol_error("Game replication source returned a message with the wrong direction or phase");
        }

        if (expected_kind == GameReplicationMessageKind::kServerSnapshot) {
            auto snapshot = parse_game_snapshot(message);
            if (!snapshot) return snapshot.error();
            total_chunks += snapshot->chunks.size();
            total_entities += snapshot->entities.size();
        } else {
            auto delta = parse_game_delta(message);
            if (!delta) return delta.error();
            total_entities += delta->entities.size();
            for (const GameChunkDelta& chunk : delta->chunks) {
                total_block_deltas += chunk.blocks.size();
            }
        }

        auto payload = encode_game_replication_message(message);
        if (!payload) return payload.error();
        total_bytes += payload->size();
        if (total_bytes > replication_budget_.max_reliable_bytes_per_tick ||
            total_chunks > replication_budget_.max_chunk_snapshots_per_tick ||
            total_entities > replication_budget_.max_entity_snapshots_per_tick ||
            total_block_deltas > replication_budget_.max_block_deltas_per_tick) {
            return protocol_error("Game replication source exceeded the configured peer budget");
        }

        frames.push_back({
            .peer = peer,
            .frame = {
                .protocol_version = snt::network::kCurrentReplicationProtocolVersion,
                .server_tick = context.tick_index,
                .channel = snt::network::ReplicationChannel::Reliable,
                .payload = std::move(*payload),
            },
            .blocks_replication_generation = true,
        });
    }
    pending_outbound_.insert(pending_outbound_.end(),
                             std::make_move_iterator(frames.begin()),
                             std::make_move_iterator(frames.end()));
    return {};
}

bool GameServerReplicationHandler::has_pending_replication(
    snt::network::PeerId peer) const {
    return std::any_of(pending_outbound_.begin(), pending_outbound_.end(),
                       [peer](const PendingOutboundFrame& pending) {
                           return pending.peer == peer && pending.blocks_replication_generation;
                       });
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
    if (auto result = validate_player_identity(identity.identity); !result) {
        authenticator_->on_peer_disconnected(identity, "authenticator returned an invalid player id");
        return protocol_error("Game authenticator returned an invalid player id");
    }
    // The authenticator owns only account evidence. The handler owns the
    // actual transport association and must overwrite any caller-provided
    // value before a command sink observes this authenticated session.
    identity.peer = peer;
    auto accepted = make_game_login_accepted({.identity = identity.identity});
    if (!accepted) {
        authenticator_->on_peer_disconnected(identity, "login accepted payload could not be encoded");
        return accepted.error();
    }
    auto payload = encode_game_replication_message(*accepted);
    if (!payload) {
        authenticator_->on_peer_disconnected(identity, "login accepted envelope could not be encoded");
        return payload.error();
    }

    if (const auto existing_peer = find_authenticated_peer_for_player_id(
            identity.identity.account_id, peer);
        existing_peer.has_value()) {
        const auto previous = peers_.find(*existing_peer);
        if (previous == peers_.end() || !previous->second.authenticated_peer.has_value()) {
            authenticator_->on_peer_disconnected(identity, "account takeover lost its previous session");
            return protocol_error("Game replication account takeover has no previous authenticated peer");
        }
        if (player_lifecycle_ != nullptr) {
            if (auto result = player_lifecycle_->on_peer_replaced(
                    *previous->second.authenticated_peer, identity, context);
                !result) {
                auto error = result.error();
                error.with_context("GameServerReplicationHandler::handle_login(player takeover)");
                authenticator_->on_peer_disconnected(identity,
                                                      "player lifecycle rejected account takeover");
                return error;
            }
        }
        take_over_existing_account_session(*existing_peer, identity.identity.account_id);
    } else if (player_lifecycle_ != nullptr) {
        if (auto result = player_lifecycle_->on_peer_authenticated(identity, context); !result) {
            auto error = result.error();
            error.with_context("GameServerReplicationHandler::handle_login(player lifecycle)");
            authenticator_->on_peer_disconnected(identity,
                                                  "player lifecycle rejected authenticated session");
            return error;
        }
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
    SNT_LOG_INFO("Replication peer %llu authenticated as '%s' (%s)",
                 static_cast<unsigned long long>(peer),
                 state.authenticated_peer->identity.account_id.c_str(),
                 player_identity_provider_name(state.authenticated_peer->identity.provider));
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

void GameServerReplicationHandler::take_over_existing_account_session(
    snt::network::PeerId peer, std::string_view account_id) {
    const auto state = peers_.find(peer);
    if (state == peers_.end() || !state->second.authenticated_peer.has_value()) return;

    static constexpr std::string_view kTakeoverReason =
        "player account session was replaced by a newer login";
    if (command_sink_ != nullptr) {
        command_sink_->on_peer_disconnected(*state->second.authenticated_peer, kTakeoverReason);
    }
    authenticator_->on_peer_disconnected(*state->second.authenticated_peer, kTakeoverReason);
    state->second.authenticated_peer.reset();
    state->second.disconnecting = true;
    std::erase_if(pending_outbound_, [peer](const PendingOutboundFrame& pending) {
        return pending.peer == peer;
    });
    pending_disconnects_.push_back({.peer = peer, .reason = std::string(kTakeoverReason)});
    SNT_LOG_WARN("Player account '%.*s' session takeover: peer %llu will be disconnected",
                 static_cast<int>(account_id.size()), account_id.data(),
                 static_cast<unsigned long long>(peer));
}

std::optional<snt::network::PeerId>
GameServerReplicationHandler::find_authenticated_peer_for_player_id(
    std::string_view player_id, snt::network::PeerId except_peer) const {
    for (const auto& [peer, state] : peers_) {
        if (peer != except_peer && state.authenticated_peer.has_value() &&
            state.authenticated_peer->identity.account_id == player_id) {
            return peer;
        }
    }
    return std::nullopt;
}

}  // namespace snt::game::replication
