// Game-owned client replication session implementation.
//
// The handler is deliberately local to this translation unit: only the
// session's value status and command boundary are public. This keeps the
// transport/event callback surface inside the networking module and prevents
// presentation code from reaching directly into per-peer protocol state.

#define SNT_LOG_CHANNEL "game.client_replication"
#include "game/network/game_client_replication_session.h"

#include "network/tcp_udp_transport.h"

#include "core/error.h"
#include "core/log.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxQueuedClientGameBytes = 512u * 1024u;
constexpr size_t kMaxQueuedClientReplicationUpdates = 64;
constexpr size_t kMaxQueuedClientReplicationBytes =
    kGameReplicationHeaderBytes + kMaxGameReplicationPayloadBytes;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] snt::core::Expected<GameLoginRequest> make_login_request(
    const GameClientAuthentication& authentication) {
    if (auto result = validate_player_identity(authentication.local_identity); !result) {
        auto error = result.error();
        error.with_context("GameClientReplicationSession::create(local identity)");
        return error;
    }

    GameLoginRequest request{
        .identity_provider = authentication.local_identity.provider,
        .display_name = authentication.local_identity.display_name,
        .credential = authentication.credential,
        .server_password = authentication.server_password,
    };
    // Reuse the public codec's validation rules so both transport factories
    // reject malformed local and future Steam credentials identically.
    if (auto encoded = make_game_login_request(request); !encoded) {
        auto error = encoded.error();
        error.with_context("GameClientReplicationSession::create(login request)");
        return error;
    }
    return request;
}

class GameClientReplicationHandler final : public snt::network::IReplicationHandler {
public:
    GameClientReplicationHandler(GameClientAuthentication authentication,
                                 GameClientReplicationSessionConfig config)
        : authentication_(std::move(authentication)), server_peer_(config.server_peer) {
        status_.state = GameClientConnectionState::kTransportConnecting;
    }

    [[nodiscard]] snt::core::Expected<void> on_peer_connected(
        snt::network::PeerId peer, const snt::network::ReplicationTickContext&) override {
        if (peer != server_peer_) {
            return protocol_error("Client replication transport connected an unexpected peer");
        }
        if (status_.state != GameClientConnectionState::kTransportConnecting) {
            return protocol_error("Client replication transport reported a duplicate server connection");
        }

        auto request = make_login_request(authentication_);
        if (!request) return request.error();
        auto message = make_game_login_request(*request);
        if (!message) return message.error();
        if (auto result = queue_message(peer, std::move(*message)); !result) return result.error();

        status_.state = GameClientConnectionState::kAwaitingLoginAccepted;
        SNT_LOG_INFO("Game client transport connected; login queued for '%s' (%s)",
                     authentication_.local_identity.account_id.c_str(),
                     player_identity_provider_name(authentication_.local_identity.provider));
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> on_frame(
        snt::network::PeerId peer, const snt::network::ReplicationFrame& frame,
        const snt::network::ReplicationTickContext&) override {
        if (peer != server_peer_) {
            return protocol_error("Client replication frame arrived from an unexpected peer");
        }
        if (status_.state == GameClientConnectionState::kTransportConnecting ||
            status_.state == GameClientConnectionState::kDisconnected ||
            status_.state == GameClientConnectionState::kStopped) {
            return protocol_error("Client replication frame arrived outside an active connection");
        }
        auto message = decode_game_replication_message(frame.payload);
        if (!message) {
            auto error = message.error();
            error.with_context("GameClientReplicationHandler::on_frame(decode envelope)");
            return error;
        }
        if (!is_server_game_replication_message(message->kind)) {
            return protocol_error("Client replication received a client-originated game message");
        }
        if (auto result = validate_game_replication_channel(message->kind, frame.channel); !result) {
            // Each game kind owns its channel contract. A future unreliable
            // kind must update the shared codec validation rather than inherit
            // the current reliable-login behavior implicitly.
            return result.error();
        }

        switch (message->kind) {
            case GameReplicationMessageKind::kServerLoginAccepted:
                return handle_login_accepted(*message);
            case GameReplicationMessageKind::kServerSnapshot:
                return handle_snapshot(*message, frame.payload.size());
            case GameReplicationMessageKind::kServerDelta:
                return handle_delta(*message, frame.payload.size());
            case GameReplicationMessageKind::kServerNotice:
                return snt::core::Error{
                    snt::core::ErrorCode::kNotImplemented,
                    "Game replication server payload is declared but no client codec is implemented"};
            case GameReplicationMessageKind::kClientLoginRequest:
            case GameReplicationMessageKind::kClientCommand:
            case GameReplicationMessageKind::kClientMovementInput:
            case GameReplicationMessageKind::kClientInterestUpdate:
            case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
                return protocol_error("Client replication received an invalid game message direction");
        }
        return protocol_error("Client replication received an unknown game message kind");
    }

    void on_peer_disconnected(snt::network::PeerId peer, std::string_view reason) noexcept override {
        if (peer != server_peer_ || status_.state == GameClientConnectionState::kStopped) return;

        const bool was_authenticated =
            status_.state == GameClientConnectionState::kAuthenticated;
        status_.state = GameClientConnectionState::kDisconnected;
        status_.authenticated_identity.reset();
        pending_outbound_.clear();
        pending_outbound_bytes_ = 0;
        pending_replication_updates_.clear();
        pending_replication_bytes_ = 0;
        has_snapshot_ = false;
        active_snapshot_id_ = 0;
        last_delta_sequence_ = 0;
        SNT_LOG_WARN("Game client replication server disconnected%s: %.*s",
                     was_authenticated ? " after login" : " before login",
                     static_cast<int>(reason.size()), reason.data());
    }

    [[nodiscard]] snt::core::Expected<void> emit_outbound(
        const snt::network::ReplicationTickContext& context,
        snt::network::IReplicationFrameSink& sink) override {
        while (!pending_outbound_.empty()) {
            const PendingOutboundMessage& pending = pending_outbound_.front();
            snt::network::ReplicationFrame frame{
                .protocol_version = snt::network::kCurrentReplicationProtocolVersion,
                .server_tick = context.tick_index,
                .channel = pending.channel,
                .payload = pending.payload,
            };
            if (auto result = sink.send(pending.peer, frame); !result) return result.error();

            pending_outbound_bytes_ -= pending.payload.size();
            pending_outbound_.erase(pending_outbound_.begin());
        }
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> enqueue_command(GameClientCommand command) {
        if (status_.state != GameClientConnectionState::kAuthenticated) {
            return invalid_state("Game client command requires an authenticated server session");
        }
        auto message = make_game_client_command(command);
        if (!message) return message.error();
        return enqueue_sequenced_message(command.client_sequence, std::move(*message));
    }

    [[nodiscard]] snt::core::Expected<void> enqueue_player_movement_input(
        GamePlayerMovementInput input) {
        if (status_.state != GameClientConnectionState::kAuthenticated) {
            return invalid_state("Game player movement input requires an authenticated server session");
        }
        auto message = make_game_player_movement_input(input);
        if (!message) return message.error();
        return enqueue_sequenced_message(input.client_sequence, std::move(*message));
    }

    [[nodiscard]] GameClientReplicationStatus status() const { return status_; }

    void shutdown() noexcept {
        if (status_.state == GameClientConnectionState::kStopped) return;
        status_.state = GameClientConnectionState::kStopped;
        status_.authenticated_identity.reset();
        pending_outbound_.clear();
        pending_outbound_bytes_ = 0;
        pending_replication_updates_.clear();
        pending_replication_bytes_ = 0;
        has_snapshot_ = false;
        active_snapshot_id_ = 0;
        last_delta_sequence_ = 0;
    }

    [[nodiscard]] std::vector<GameClientReplicationUpdate> drain_replication_updates() {
        pending_replication_bytes_ = 0;
        return std::exchange(pending_replication_updates_, {});
    }

private:
    struct PendingOutboundMessage {
        snt::network::PeerId peer = snt::network::kInvalidPeerId;
        snt::network::ReplicationChannel channel = snt::network::ReplicationChannel::Reliable;
        std::vector<std::byte> payload;
    };

    [[nodiscard]] snt::core::Expected<void> enqueue_sequenced_message(
        uint64_t client_sequence, GameReplicationMessage message) {
        if (client_sequence == 0) {
            return invalid_argument("Game client command sequence must be non-zero");
        }
        if (has_last_client_sequence_ && client_sequence <= last_client_sequence_) {
            return invalid_argument("Game client command sequence must increase strictly");
        }
        if (auto result = queue_message(server_peer_, std::move(message)); !result) return result.error();
        has_last_client_sequence_ = true;
        last_client_sequence_ = client_sequence;
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> queue_message(
        snt::network::PeerId peer, GameReplicationMessage message) {
        if (!is_client_game_replication_message(message.kind)) {
            return protocol_error("Game client attempted to queue a server-originated replication message");
        }
        const snt::network::ReplicationChannel channel =
            message.kind == GameReplicationMessageKind::kClientMovementInput
                ? snt::network::ReplicationChannel::Unreliable
                : snt::network::ReplicationChannel::Reliable;
        if (auto result = validate_game_replication_channel(message.kind, channel); !result) {
            return result.error();
        }
        auto encoded = encode_game_replication_message(message);
        if (!encoded) return encoded.error();
        if (encoded->size() > kMaxQueuedClientGameBytes - pending_outbound_bytes_) {
            return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                    "Client game replication queue limit exceeded"};
        }

        pending_outbound_bytes_ += encoded->size();
        pending_outbound_.push_back(
            {.peer = peer, .channel = channel, .payload = std::move(*encoded)});
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> handle_login_accepted(
        const GameReplicationMessage& message) {
        if (status_.state != GameClientConnectionState::kAwaitingLoginAccepted) {
            return protocol_error("Game client received LoginAccepted outside the login state");
        }

        auto accepted = parse_game_login_accepted(message);
        if (!accepted) return accepted.error();
        if (accepted->identity.provider != authentication_.local_identity.provider ||
            accepted->identity.account_id != authentication_.local_identity.account_id) {
            return protocol_error("Server accepted a player identity different from the local authenticated account");
        }

        status_.state = GameClientConnectionState::kAuthenticated;
        status_.authenticated_identity = std::move(accepted->identity);
        SNT_LOG_INFO("Game client login accepted as '%s' (%s)",
                     status_.authenticated_identity->account_id.c_str(),
                     player_identity_provider_name(status_.authenticated_identity->provider));
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> handle_snapshot(
        const GameReplicationMessage& message, size_t wire_bytes) {
        if (status_.state != GameClientConnectionState::kAuthenticated) {
            return protocol_error("Game client received Snapshot before LoginAccepted");
        }
        auto snapshot = parse_game_snapshot(message);
        if (!snapshot) return snapshot.error();
        const uint64_t snapshot_id = snapshot->snapshot_id;
        if (auto result = queue_replication_update(std::move(*snapshot), wire_bytes); !result) {
            return result.error();
        }

        has_snapshot_ = true;
        active_snapshot_id_ = snapshot_id;
        last_delta_sequence_ = 0;
        SNT_LOG_INFO("Game client queued snapshot %llu",
                     static_cast<unsigned long long>(active_snapshot_id_));
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> handle_delta(
        const GameReplicationMessage& message, size_t wire_bytes) {
        if (status_.state != GameClientConnectionState::kAuthenticated) {
            return protocol_error("Game client received Delta before LoginAccepted");
        }
        auto delta = parse_game_delta(message);
        if (!delta) return delta.error();
        if (!has_snapshot_ || delta->base_snapshot_id != active_snapshot_id_) {
            return protocol_error("Game client Delta does not match the active snapshot");
        }
        if (last_delta_sequence_ != 0 && delta->sequence <= last_delta_sequence_) {
            return protocol_error("Game client Delta sequence must increase strictly");
        }
        const uint64_t delta_sequence = delta->sequence;
        if (auto result = queue_replication_update(std::move(*delta), wire_bytes); !result) {
            return result.error();
        }
        last_delta_sequence_ = delta_sequence;
        return {};
    }

    [[nodiscard]] snt::core::Expected<void> queue_replication_update(
        GameClientReplicationUpdate update, size_t wire_bytes) {
        if (wire_bytes > kMaxQueuedClientReplicationBytes ||
            pending_replication_updates_.size() >= kMaxQueuedClientReplicationUpdates ||
            pending_replication_bytes_ > kMaxQueuedClientReplicationBytes - wire_bytes) {
            return protocol_error("Game client replication update queue limit exceeded");
        }
        pending_replication_bytes_ += wire_bytes;
        pending_replication_updates_.push_back(std::move(update));
        return {};
    }

    GameClientAuthentication authentication_;
    snt::network::PeerId server_peer_ = snt::network::kInvalidPeerId;
    GameClientReplicationStatus status_;
    std::vector<PendingOutboundMessage> pending_outbound_;
    size_t pending_outbound_bytes_ = 0;
    std::vector<GameClientReplicationUpdate> pending_replication_updates_;
    size_t pending_replication_bytes_ = 0;
    bool has_snapshot_ = false;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
    uint64_t last_client_sequence_ = 0;
    bool has_last_client_sequence_ = false;
};

}  // namespace

struct GameClientReplicationSession::Impl {
    std::unique_ptr<snt::network::IReplicationTransport> transport;
    std::unique_ptr<GameClientReplicationHandler> handler;
    std::unique_ptr<snt::network::ReplicationService> service;
};

snt::core::Expected<std::unique_ptr<GameClientReplicationSession>>
GameClientReplicationSession::create(
    std::unique_ptr<snt::network::IReplicationTransport> transport,
    GameClientAuthentication authentication, GameClientReplicationSessionConfig config) {
    if (!transport) {
        return invalid_argument("Game client replication session requires a transport");
    }
    if (config.server_peer == snt::network::kInvalidPeerId) {
        return invalid_argument("Game client replication session requires a valid server peer id");
    }
    if (auto request = make_login_request(authentication); !request) return request.error();

    auto impl = std::make_unique<Impl>();
    impl->transport = std::move(transport);
    impl->handler = std::make_unique<GameClientReplicationHandler>(std::move(authentication), config);
    impl->service = std::make_unique<snt::network::ReplicationService>(
        *impl->transport, *impl->handler);
    return std::unique_ptr<GameClientReplicationSession>(
        new GameClientReplicationSession(std::move(impl)));
}

snt::core::Expected<std::unique_ptr<GameClientReplicationSession>>
GameClientReplicationSession::connect_tcp_udp(
    snt::network::TcpUdpConnectConfig config, GameClientAuthentication authentication) {
    auto transport = snt::network::TcpUdpReplicationTransport::connect(std::move(config));
    if (!transport) {
        auto error = transport.error();
        error.with_context("GameClientReplicationSession::connect_tcp_udp(transport)");
        return error;
    }
    return create(std::move(*transport), std::move(authentication));
}

GameClientReplicationSession::GameClientReplicationSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

GameClientReplicationSession::~GameClientReplicationSession() { shutdown(); }

snt::core::Expected<void> GameClientReplicationSession::poll_inbound(
    const snt::network::ReplicationTickContext& context) {
    if (!impl_ || !impl_->service) {
        return invalid_state("Game client replication session was already shut down");
    }
    return impl_->service->poll_inbound(context);
}

snt::core::Expected<void> GameClientReplicationSession::emit_outbound(
    const snt::network::ReplicationTickContext& context) {
    if (!impl_ || !impl_->service) {
        return invalid_state("Game client replication session was already shut down");
    }
    return impl_->service->emit_outbound(context);
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_command(GameClientCommand command) {
    if (!impl_ || !impl_->handler) {
        return invalid_state("Game client replication session was already shut down");
    }
    return impl_->handler->enqueue_command(std::move(command));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_quest_claim_reward(
    uint64_t client_sequence, GameQuestClaimRewardCommand command) {
    auto encoded = make_game_quest_claim_reward_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_block_interaction(
    uint64_t client_sequence, GameBlockInteractionCommand command) {
    auto encoded = make_game_block_interaction_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_inventory_slot_transfer(
    uint64_t client_sequence, GameInventorySlotTransferCommand command) {
    auto encoded = make_game_inventory_slot_transfer_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_machine_input_slot_transfer(
    uint64_t client_sequence, GameMachineInputSlotTransferCommand command) {
    auto encoded = make_game_machine_input_slot_transfer_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_sfm_program_replace(
    uint64_t client_sequence, GameSfmProgramReplaceCommand command) {
    auto encoded = make_game_sfm_program_replace_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_creature_attack(
    uint64_t client_sequence, GameCreatureAttackCommand command) {
    auto encoded = make_game_creature_attack_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_creature_capture(
    uint64_t client_sequence, GameCreatureCaptureCommand command) {
    auto encoded = make_game_creature_capture_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_captive_creature_feed(
    uint64_t client_sequence, GameCaptiveCreatureFeedCommand command) {
    auto encoded = make_game_captive_creature_feed_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_ground_loot_pickup(
    uint64_t client_sequence, GameGroundLootPickupCommand command) {
    auto encoded = make_game_ground_loot_pickup_command(client_sequence, command);
    if (!encoded) return encoded.error();
    return enqueue_command(std::move(*encoded));
}

snt::core::Expected<void> GameClientReplicationSession::enqueue_player_movement_input(
    GamePlayerMovementInput input) {
    if (!impl_ || !impl_->handler) {
        return invalid_state("Game client replication session was already shut down");
    }
    return impl_->handler->enqueue_player_movement_input(std::move(input));
}

std::vector<GameClientReplicationUpdate>
GameClientReplicationSession::drain_replication_updates() {
    if (!impl_ || !impl_->handler) return {};
    return impl_->handler->drain_replication_updates();
}

GameClientReplicationStatus GameClientReplicationSession::status() const {
    if (impl_ && impl_->handler) return impl_->handler->status();
    return {.state = GameClientConnectionState::kStopped};
}

void GameClientReplicationSession::shutdown() noexcept {
    if (!impl_) return;
    if (impl_->handler) impl_->handler->shutdown();
    if (impl_->service) impl_->service->shutdown();
    impl_.reset();
}

}  // namespace snt::game::replication
