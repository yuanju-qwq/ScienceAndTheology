// Game-owned replication protocol and authoritative admission tests.

#include "game/network/game_replication_protocol.h"
#include "game/network/game_account_peer_authenticator.h"
#include "game/network/game_server_replication_handler.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameClientCommand;
using snt::game::replication::GameLoginRequest;
using snt::game::replication::GameReplicationMessage;
using snt::game::replication::GameReplicationMessageKind;
using snt::game::PlayerIdentity;
using snt::game::PlayerIdentityProvider;

PlayerIdentity make_local_identity(std::string display_name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(display_name));
    return identity ? std::move(*identity) : PlayerIdentity{};
}

std::vector<std::byte> bytes_from_text(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char value : text) bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

class RecordingAuthenticator final : public snt::game::replication::IGamePeerAuthenticator {
public:
    snt::core::Expected<GameAuthenticatedPeer> authenticate(
        snt::network::PeerId peer, const GameLoginRequest& request,
        const snt::network::ReplicationTickContext&) override {
        ++authenticate_calls;
        last_peer = peer;
        last_identity_provider = request.identity_provider;
        last_display_name = request.display_name;
        last_credential = request.credential;
        if (reject) {
            return snt::core::Error{snt::core::ErrorCode::kProtocolError,
                                    "test authenticator rejected login"};
        }
        return GameAuthenticatedPeer{.identity = identity};
    }

    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override {
        disconnected_player_id = peer.identity.account_id;
        disconnect_reason = reason;
    }

    PlayerIdentity identity = make_local_identity("Alice");
    bool reject = false;
    int authenticate_calls = 0;
    snt::network::PeerId last_peer = snt::network::kInvalidPeerId;
    PlayerIdentityProvider last_identity_provider = PlayerIdentityProvider::kLocalName;
    std::string last_display_name;
    std::vector<std::byte> last_credential;
    std::string disconnected_player_id;
    std::string disconnect_reason;
};

class RecordingCommandSink final : public snt::game::replication::IGameReplicationCommandSink {
public:
    snt::core::Expected<void> enqueue_client_command(
        const GameAuthenticatedPeer& peer, GameClientCommand command,
        const snt::network::ReplicationTickContext& context) override {
        ++call_count;
        last_player_id = peer.identity.account_id;
        last_command = std::move(command);
        last_tick_index = context.tick_index;
        return {};
    }

    int call_count = 0;
    std::string last_player_id;
    GameClientCommand last_command;
    uint64_t last_tick_index = 0;
};

class RecordingSteamTicketVerifier final : public snt::game::replication::ISteamSessionTicketVerifier {
public:
    snt::core::Expected<snt::game::replication::VerifiedSteamAccount> verify_ticket(
        snt::network::PeerId peer, std::span<const std::byte> ticket,
        const snt::network::ReplicationTickContext&) override {
        last_peer = peer;
        last_ticket.assign(ticket.begin(), ticket.end());
        return snt::game::replication::VerifiedSteamAccount{
            .steam_id = steam_id,
            .display_name = display_name,
        };
    }

    uint64_t steam_id = 76561198000000001ull;
    std::string display_name = "Steam Alice";
    snt::network::PeerId last_peer = snt::network::kInvalidPeerId;
    std::vector<std::byte> last_ticket;
};

class RecordingFrameSink final : public snt::network::IReplicationFrameSink {
public:
    snt::core::Expected<void> send(snt::network::PeerId peer,
                                   const snt::network::ReplicationFrame& frame) override {
        sent.emplace_back(peer, frame);
        return {};
    }

    snt::core::Expected<void> broadcast(const snt::network::ReplicationFrame& frame) override {
        broadcasts.push_back(frame);
        return {};
    }

    snt::core::Expected<void> disconnect(snt::network::PeerId peer,
                                         std::string_view reason) override {
        disconnected.emplace_back(peer, std::string(reason));
        return {};
    }

    std::vector<std::pair<snt::network::PeerId, snt::network::ReplicationFrame>> sent;
    std::vector<snt::network::ReplicationFrame> broadcasts;
    std::vector<std::pair<snt::network::PeerId, std::string>> disconnected;
};

snt::network::ReplicationFrame make_frame(const GameReplicationMessage& message,
    snt::network::ReplicationChannel channel =
                                               snt::network::ReplicationChannel::Reliable) {
    auto encoded = snt::game::replication::encode_game_replication_message(message);
    if (!encoded) {
        ADD_FAILURE() << encoded.error().format();
        return {};
    }
    return {
        .protocol_version = snt::network::kCurrentReplicationProtocolVersion,
        .server_tick = 0,
        .channel = channel,
        .payload = std::move(*encoded),
    };
}

TEST(GameReplicationProtocolTest, RoundTripsTypedLoginAndCommandMessages) {
    const GameLoginRequest request{
        .identity_provider = PlayerIdentityProvider::kSteam,
        .display_name = "Alice",
        .credential = bytes_from_text("opaque-token"),
    };
    auto login_message = snt::game::replication::make_game_login_request(request);
    ASSERT_TRUE(login_message) << login_message.error().format();
    auto encoded_login = snt::game::replication::encode_game_replication_message(*login_message);
    ASSERT_TRUE(encoded_login) << encoded_login.error().format();
    auto decoded_login = snt::game::replication::decode_game_replication_message(*encoded_login);
    ASSERT_TRUE(decoded_login) << decoded_login.error().format();
    auto parsed_request = snt::game::replication::parse_game_login_request(*decoded_login);
    ASSERT_TRUE(parsed_request) << parsed_request.error().format();
    EXPECT_EQ(parsed_request->identity_provider, request.identity_provider);
    EXPECT_EQ(parsed_request->display_name, request.display_name);
    EXPECT_EQ(parsed_request->credential, request.credential);

    const GameClientCommand command{
        .client_sequence = 91,
        .command_type = 7,
        .payload = bytes_from_text("place-block"),
    };
    auto command_message = snt::game::replication::make_game_client_command(command);
    ASSERT_TRUE(command_message) << command_message.error().format();
    auto parsed_command = snt::game::replication::parse_game_client_command(*command_message);
    ASSERT_TRUE(parsed_command) << parsed_command.error().format();
    EXPECT_EQ(parsed_command->client_sequence, command.client_sequence);
    EXPECT_EQ(parsed_command->command_type, command.command_type);
    EXPECT_EQ(parsed_command->payload, command.payload);
}

TEST(GameReplicationProtocolTest, RejectsInvalidEnvelopeAndUnreliableMessages) {
    auto message = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
        .credential = {},
    });
    ASSERT_TRUE(message) << message.error().format();
    auto encoded = snt::game::replication::encode_game_replication_message(*message);
    ASSERT_TRUE(encoded) << encoded.error().format();

    auto bad_magic = *encoded;
    bad_magic[0] = std::byte{0};
    EXPECT_FALSE(snt::game::replication::decode_game_replication_message(bad_magic));

    auto bad_version = *encoded;
    bad_version[5] = std::byte{1};
    EXPECT_FALSE(snt::game::replication::decode_game_replication_message(bad_version));

    auto trailing = *encoded;
    trailing.push_back(std::byte{0});
    EXPECT_FALSE(snt::game::replication::decode_game_replication_message(trailing));

    const auto wrong_channel = snt::game::replication::validate_game_replication_channel(
        GameReplicationMessageKind::kClientLoginRequest,
        snt::network::ReplicationChannel::Unreliable);
    ASSERT_FALSE(wrong_channel);
    EXPECT_EQ(wrong_channel.error().code(), snt::core::ErrorCode::kProtocolError);
}

TEST(GameReplicationHandlerTest, AuthenticatesThenQueuesCommandsAndAcknowledgesLogin) {
    RecordingAuthenticator authenticator;
    RecordingCommandSink command_sink;
    snt::game::replication::GameServerReplicationHandler handler(authenticator, &command_sink);
    const snt::network::ReplicationTickContext context{.tick_index = 37, .delta_seconds = 0.05f};
    constexpr snt::network::PeerId kPeer = 17;

    ASSERT_TRUE(handler.on_peer_connected(kPeer, context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kSteam,
        .display_name = "Alice",
        .credential = bytes_from_text("credential"),
    });
    ASSERT_TRUE(login) << login.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*login), context));
    EXPECT_EQ(authenticator.authenticate_calls, 1);
    EXPECT_EQ(authenticator.last_identity_provider, PlayerIdentityProvider::kSteam);
    EXPECT_EQ(authenticator.last_display_name, "Alice");

    RecordingFrameSink sink;
    ASSERT_TRUE(handler.emit_outbound(context, sink));
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent.front().first, kPeer);
    EXPECT_EQ(sink.sent.front().second.server_tick, context.tick_index);
    auto accepted_envelope = snt::game::replication::decode_game_replication_message(
        sink.sent.front().second.payload);
    ASSERT_TRUE(accepted_envelope) << accepted_envelope.error().format();
    auto accepted = snt::game::replication::parse_game_login_accepted(*accepted_envelope);
    ASSERT_TRUE(accepted) << accepted.error().format();
    EXPECT_EQ(accepted->identity.account_id, authenticator.identity.account_id);

    auto command = snt::game::replication::make_game_client_command({
        .client_sequence = 19,
        .command_type = 12,
        .payload = bytes_from_text("interact"),
    });
    ASSERT_TRUE(command) << command.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*command), context));
    EXPECT_EQ(command_sink.call_count, 1);
    EXPECT_EQ(command_sink.last_player_id, authenticator.identity.account_id);
    EXPECT_EQ(command_sink.last_command.client_sequence, 19u);
    EXPECT_EQ(command_sink.last_command.command_type, 12u);
    EXPECT_EQ(command_sink.last_tick_index, context.tick_index);

    handler.on_peer_disconnected(kPeer, "test complete");
    EXPECT_EQ(authenticator.disconnected_player_id, authenticator.identity.account_id);
    EXPECT_EQ(authenticator.disconnect_reason, "test complete");
}

TEST(GameReplicationHandlerTest, RejectsUnauthenticatedCommandsAndClosedAdmission) {
    RecordingAuthenticator authenticator;
    RecordingCommandSink command_sink;
    snt::game::replication::GameServerReplicationHandler handler(authenticator, &command_sink);
    const snt::network::ReplicationTickContext context{.tick_index = 4, .delta_seconds = 0.05f};
    ASSERT_TRUE(handler.on_peer_connected(23, context));

    auto command = snt::game::replication::make_game_client_command({
        .client_sequence = 1,
        .command_type = 1,
        .payload = {},
    });
    ASSERT_TRUE(command) << command.error().format();
    const auto unauthenticated = handler.on_frame(23, make_frame(*command), context);
    ASSERT_FALSE(unauthenticated);
    EXPECT_EQ(unauthenticated.error().code(), snt::core::ErrorCode::kProtocolError);
    EXPECT_EQ(command_sink.call_count, 0);

    snt::game::replication::ClosedGamePeerAuthenticator closed_authenticator;
    snt::game::replication::GameServerReplicationHandler closed_handler(closed_authenticator);
    ASSERT_TRUE(closed_handler.on_peer_connected(24, context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
        .credential = {},
    });
    ASSERT_TRUE(login) << login.error().format();
    const auto rejected = closed_handler.on_frame(24, make_frame(*login), context);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), snt::core::ErrorCode::kProtocolError);
}

TEST(GameAccountPeerAuthenticatorTest, MapsLocalNamesAndUsesOnlyVerifiedSteamIds) {
    snt::game::replication::GameAccountPeerAuthenticator local_only;
    const snt::network::ReplicationTickContext context{.tick_index = 7, .delta_seconds = 0.05f};

    auto local = local_only.authenticate(
        41, {.identity_provider = PlayerIdentityProvider::kLocalName, .display_name = "Alex"}, context);
    ASSERT_TRUE(local) << local.error().format();
    EXPECT_EQ(local->identity.account_id, "local-name:Alex");

    auto missing_steam = local_only.authenticate(
        42, {.identity_provider = PlayerIdentityProvider::kSteam,
             .display_name = "Client supplied name",
             .credential = bytes_from_text("ticket")}, context);
    ASSERT_FALSE(missing_steam);
    EXPECT_EQ(missing_steam.error().code(), snt::core::ErrorCode::kNotImplemented);

    RecordingSteamTicketVerifier verifier;
    snt::game::replication::GameAccountPeerAuthenticator with_steam(&verifier);
    auto steam = with_steam.authenticate(
        43, {.identity_provider = PlayerIdentityProvider::kSteam,
             .display_name = "Untrusted client label",
             .credential = bytes_from_text("ticket")}, context);
    ASSERT_TRUE(steam) << steam.error().format();
    EXPECT_EQ(steam->identity.account_id, "steam:76561198000000001");
    EXPECT_EQ(steam->identity.display_name, verifier.display_name);
    EXPECT_EQ(verifier.last_peer, 43u);
    EXPECT_EQ(verifier.last_ticket, bytes_from_text("ticket"));
}

TEST(GameAccountPeerAuthenticatorTest, NewerDuplicateLocalNameLoginTakesOverTheAccountSession) {
    snt::game::replication::GameAccountPeerAuthenticator authenticator;
    snt::game::replication::GameServerReplicationHandler handler(authenticator);
    const snt::network::ReplicationTickContext context{.tick_index = 8, .delta_seconds = 0.05f};
    ASSERT_TRUE(handler.on_peer_connected(51, context));
    ASSERT_TRUE(handler.on_peer_connected(52, context));

    auto first_login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "SharedName",
    });
    ASSERT_TRUE(first_login) << first_login.error().format();
    ASSERT_TRUE(handler.on_frame(51, make_frame(*first_login), context));

    auto second_login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "SharedName",
    });
    ASSERT_TRUE(second_login) << second_login.error().format();
    ASSERT_TRUE(handler.on_frame(52, make_frame(*second_login), context));

    RecordingFrameSink sink;
    ASSERT_TRUE(handler.emit_outbound(context, sink));
    ASSERT_EQ(sink.disconnected.size(), 1u);
    EXPECT_EQ(sink.disconnected.front().first, 51u);
    EXPECT_EQ(sink.disconnected.front().second,
              "player account session was replaced by a newer login");
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent.front().first, 52u);

    auto old_peer_command = snt::game::replication::make_game_client_command({
        .client_sequence = 1,
        .command_type = 1,
        .payload = {},
    });
    ASSERT_TRUE(old_peer_command) << old_peer_command.error().format();
    const auto rejected_old_peer = handler.on_frame(51, make_frame(*old_peer_command), context);
    ASSERT_FALSE(rejected_old_peer);
    EXPECT_EQ(rejected_old_peer.error().code(), snt::core::ErrorCode::kProtocolError);
}

}  // namespace
