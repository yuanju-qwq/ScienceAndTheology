// Game-owned replication protocol and authoritative admission tests.

#include "game/network/game_replication_protocol.h"
#include "game/network/game_account_peer_authenticator.h"
#include "game/network/game_client_replication_session.h"
#include "game/network/game_server_replication_handler.h"
#include "game/server/game_server_command_sink.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_movement.h"
#include "game/world/save/player_state_persistence.h"
#include "network/tcp_udp_transport.h"
#include "ecs/world.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <variant>

namespace {

using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameClientCommand;
using snt::game::replication::GameClientCommandType;
using snt::game::replication::GameLoginRequest;
using snt::game::replication::GamePlayerMovementInput;
using snt::game::replication::GameDelta;
using snt::game::replication::GameReplicationMessage;
using snt::game::replication::GameReplicationMessageKind;
using snt::game::replication::GameSnapshot;
using snt::game::GameContentRegistry;
using snt::game::PlayerIdentity;
using snt::game::PlayerIdentityProvider;
using snt::game::QuestDefinition;
using snt::game::QuestItemReward;
using snt::game::QuestObjectiveDefinition;
using snt::game::QuestObjectiveKind;
using snt::game::QuestRewardKind;
using snt::game::QuestRegistry;
using snt::game::QuestState;

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

class RecordingQuestRewardSink final : public snt::game::IQuestRewardSink {
public:
    snt::core::Expected<void> grant_item_rewards(
        std::string_view player_id, std::string_view quest_id,
        std::span<const QuestItemReward> rewards) override {
        player_ids.emplace_back(player_id);
        quest_ids.emplace_back(quest_id);
        grants.emplace_back(rewards.begin(), rewards.end());
        return {};
    }

    std::vector<std::string> player_ids;
    std::vector<std::string> quest_ids;
    std::vector<std::vector<QuestItemReward>> grants;
};

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
        last_peer = peer.peer;
        last_player_id = peer.identity.account_id;
        last_command = std::move(command);
        last_tick_index = context.tick_index;
        return {};
    }

    snt::core::Expected<void> enqueue_player_movement_input(
        const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
        const snt::network::ReplicationTickContext& context) override {
        ++movement_call_count;
        last_movement_peer = peer.peer;
        last_movement_input = input;
        last_movement_tick_index = context.tick_index;
        return {};
    }

    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override {
        ++disconnected_call_count;
        disconnected_peer = peer.peer;
        disconnect_reason = reason;
    }

    int call_count = 0;
    int movement_call_count = 0;
    int disconnected_call_count = 0;
    snt::network::PeerId last_peer = snt::network::kInvalidPeerId;
    snt::network::PeerId disconnected_peer = snt::network::kInvalidPeerId;
    std::string last_player_id;
    GameClientCommand last_command;
    GamePlayerMovementInput last_movement_input;
    snt::network::PeerId last_movement_peer = snt::network::kInvalidPeerId;
    uint64_t last_tick_index = 0;
    uint64_t last_movement_tick_index = 0;
    std::string disconnect_reason;
};

class RecordingMovementInputSink final
    : public snt::game::replication::IGameServerPlayerMovementInputSink {
public:
    snt::core::Expected<void> enqueue_player_movement_input(
        const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
        const snt::network::ReplicationTickContext& context) override {
        ++call_count;
        last_peer = peer;
        last_input = input;
        last_tick_index = context.tick_index;
        return {};
    }

    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override {
        ++disconnected_call_count;
        disconnected_peer = peer;
        disconnect_reason = reason;
    }

    int call_count = 0;
    int disconnected_call_count = 0;
    GameAuthenticatedPeer last_peer;
    GameAuthenticatedPeer disconnected_peer;
    GamePlayerMovementInput last_input;
    uint64_t last_tick_index = 0;
    std::string disconnect_reason;
};

class RecordingPlayerLifecycle final
    : public snt::game::replication::IGamePlayerSessionLifecycle {
public:
    snt::core::Expected<void> on_peer_authenticated(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) override {
        ++authenticated_call_count;
        authenticated_peer = peer;
        authenticated_tick = context.tick_index;
        return {};
    }

    snt::core::Expected<void> on_peer_replaced(
        const GameAuthenticatedPeer& previous_peer, const GameAuthenticatedPeer& replacement_peer,
        const snt::network::ReplicationTickContext& context) override {
        ++replaced_call_count;
        previous = previous_peer;
        replacement = replacement_peer;
        replacement_tick = context.tick_index;
        return {};
    }

    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override {
        ++disconnected_call_count;
        disconnected_peer = peer;
        disconnect_reason = reason;
    }

    int authenticated_call_count = 0;
    int replaced_call_count = 0;
    int disconnected_call_count = 0;
    GameAuthenticatedPeer authenticated_peer;
    GameAuthenticatedPeer previous;
    GameAuthenticatedPeer replacement;
    GameAuthenticatedPeer disconnected_peer;
    uint64_t authenticated_tick = 0;
    uint64_t replacement_tick = 0;
    std::string disconnect_reason;
};

std::filesystem::path make_player_lifecycle_save_dir() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("snt_game_server_player_lifecycle_" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    return root;
}

std::filesystem::path find_player_progress_file(const std::filesystem::path& save_dir) {
    const auto players_dir = save_dir / "players";
    std::error_code error;
    if (!std::filesystem::is_directory(players_dir, error) || error) return {};
    for (const auto& entry : std::filesystem::directory_iterator(players_dir, error)) {
        if (error) return {};
        if (entry.is_regular_file() && entry.path().extension() == ".quest") {
            return entry.path();
        }
    }
    return {};
}

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
        if (reject_sends) {
            return snt::core::Error{snt::core::ErrorCode::kNetworkIoFailed,
                                    "test frame sink rejected send"};
        }
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
    bool reject_sends = false;
};

class RecordingInterestProvider final
    : public snt::game::replication::IGameReplicationInterestProvider {
public:
    snt::core::Expected<snt::game::replication::GameReplicationInterest> compute_interest(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) override {
        ++call_count;
        last_peer = peer;
        last_tick = context.tick_index;
        return interest;
    }

    snt::game::replication::GameReplicationInterest interest;
    int call_count = 0;
    GameAuthenticatedPeer last_peer;
    uint64_t last_tick = 0;
};

class RecordingSnapshotSource final
    : public snt::game::replication::IGameReplicationSnapshotSource {
public:
    snt::core::Expected<std::vector<GameReplicationMessage>> build_initial_snapshot(
        const GameAuthenticatedPeer& peer,
        const snt::game::replication::GameReplicationInterest& interest,
        const snt::game::replication::GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override {
        ++initial_call_count;
        last_peer = peer;
        last_interest = interest;
        last_budget = budget;
        last_tick = context.tick_index;
        return initial_messages;
    }

    snt::core::Expected<std::vector<GameReplicationMessage>> build_deltas(
        const GameAuthenticatedPeer& peer,
        const snt::game::replication::GameReplicationInterest& interest,
        const snt::game::replication::GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override {
        ++delta_call_count;
        last_peer = peer;
        last_interest = interest;
        last_budget = budget;
        last_tick = context.tick_index;
        return delta_messages;
    }

    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override {
        ++disconnected_call_count;
        disconnected_peer = peer;
        disconnect_reason = reason;
    }

    std::vector<GameReplicationMessage> initial_messages;
    std::vector<GameReplicationMessage> delta_messages;
    int initial_call_count = 0;
    int delta_call_count = 0;
    GameAuthenticatedPeer last_peer;
    snt::game::replication::GameReplicationInterest last_interest;
    snt::game::replication::GameReplicationBudget last_budget;
    uint64_t last_tick = 0;
    int disconnected_call_count = 0;
    GameAuthenticatedPeer disconnected_peer;
    std::string disconnect_reason;
};

// Deterministic transport for client-handler state transitions that should
// not depend on an OS socket schedule. The production localhost test below
// separately covers the TCP+UDP handshake and queued reliable delivery.
class ControlledReplicationTransport final : public snt::network::IReplicationTransport {
public:
    snt::core::Expected<void> send(snt::network::PeerId peer,
                                   const snt::network::ReplicationFrame& frame) override {
        sent.emplace_back(peer, frame);
        return {};
    }

    snt::core::Expected<void> disconnect(snt::network::PeerId peer,
                                         std::string_view reason) override {
        disconnected.emplace_back(peer, std::string(reason));
        return {};
    }

    snt::core::Expected<std::vector<snt::network::ReplicationEvent>> poll() override {
        auto result = std::move(events);
        events.clear();
        return result;
    }

    std::vector<snt::network::PeerId> connected_peers() const override { return peers; }
    void shutdown() noexcept override { shutdown_called = true; }

    std::vector<snt::network::ReplicationEvent> events;
    std::vector<snt::network::PeerId> peers;
    std::vector<std::pair<snt::network::PeerId, snt::network::ReplicationFrame>> sent;
    std::vector<std::pair<snt::network::PeerId, std::string>> disconnected;
    bool shutdown_called = false;
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

GameSnapshot make_test_snapshot(uint64_t snapshot_id) {
    GameSnapshot snapshot;
    snapshot.snapshot_id = snapshot_id;
    snt::game::replication::GameChunkSnapshot chunk;
    chunk.chunk = snt::voxel::ChunkKey{"overworld", -2, 3, 7};
    chunk.payload = bytes_from_text("chunk snapshot");
    snapshot.chunks.push_back(std::move(chunk));
    snt::game::replication::GameEntitySnapshot entity;
    entity.entity_guid = {.value = 101};
    entity.payload = bytes_from_text("entity snapshot");
    snapshot.entities.push_back(std::move(entity));
    snapshot.values.push_back({
        .kind = snt::game::replication::GameReplicationValueKind::kQuestBook,
        .operation = snt::game::replication::GameReplicationValueOperation::kUpsert,
        .payload = bytes_from_text("task-book snapshot"),
    });
    return snapshot;
}

GameDelta make_test_delta(uint64_t snapshot_id, uint64_t sequence) {
    GameDelta delta;
    delta.base_snapshot_id = snapshot_id;
    delta.sequence = sequence;
    snt::game::replication::GameChunkDelta chunk;
    chunk.chunk = snt::voxel::ChunkKey{"overworld", -2, 3, 7};
    chunk.blocks.push_back({.local_index = 37, .material = 9, .flags = 0x5u});
    delta.chunks.push_back(std::move(chunk));
    snt::game::replication::GameEntitySnapshot entity;
    entity.entity_guid = {.value = 102};
    entity.payload = bytes_from_text("entity delta");
    delta.entities.push_back(std::move(entity));
    delta.values.push_back({
        .kind = snt::game::replication::GameReplicationValueKind::kQuestBook,
        .operation = snt::game::replication::GameReplicationValueOperation::kRemove,
    });
    return delta;
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

    auto quest_claim_reward = snt::game::replication::make_game_quest_claim_reward_command(
        93, {.quest_id = "network.quest.claim"});
    ASSERT_TRUE(quest_claim_reward) << quest_claim_reward.error().format();
    EXPECT_EQ(quest_claim_reward->command_type,
              static_cast<uint16_t>(GameClientCommandType::kQuestClaimReward));
    auto parsed_quest_claim_reward =
        snt::game::replication::parse_game_quest_claim_reward_command(*quest_claim_reward);
    ASSERT_TRUE(parsed_quest_claim_reward) << parsed_quest_claim_reward.error().format();
    EXPECT_EQ(parsed_quest_claim_reward->quest_id, "network.quest.claim");

    const GamePlayerMovementInput movement{
        .client_sequence = 94,
        .forward_axis = 1,
        .strafe_axis = -1,
        .flags = snt::game::replication::kGamePlayerMovementFlagSprint,
        .yaw_centidegrees = -9000,
        .pitch_centidegrees = -2500,
    };
    auto movement_message = snt::game::replication::make_game_player_movement_input(movement);
    ASSERT_TRUE(movement_message) << movement_message.error().format();
    EXPECT_EQ(movement_message->kind, GameReplicationMessageKind::kClientMovementInput);
    auto parsed_movement = snt::game::replication::parse_game_player_movement_input(*movement_message);
    ASSERT_TRUE(parsed_movement) << parsed_movement.error().format();
    EXPECT_EQ(parsed_movement->client_sequence, movement.client_sequence);
    EXPECT_EQ(parsed_movement->forward_axis, movement.forward_axis);
    EXPECT_EQ(parsed_movement->strafe_axis, movement.strafe_axis);
    EXPECT_EQ(parsed_movement->flags, movement.flags);
    EXPECT_EQ(parsed_movement->yaw_centidegrees, movement.yaw_centidegrees);
    EXPECT_EQ(parsed_movement->pitch_centidegrees, movement.pitch_centidegrees);
}

TEST(GameReplicationProtocolTest, RoundTripsBoundedSnapshotAndDeltaValues) {
    EXPECT_EQ(snt::game::replication::kCurrentGameReplicationProtocolVersion, 9u);

    const GameSnapshot snapshot = make_test_snapshot(73);
    auto snapshot_message = snt::game::replication::make_game_snapshot(snapshot);
    ASSERT_TRUE(snapshot_message) << snapshot_message.error().format();
    auto snapshot_wire = snt::game::replication::encode_game_replication_message(*snapshot_message);
    ASSERT_TRUE(snapshot_wire) << snapshot_wire.error().format();
    auto snapshot_envelope = snt::game::replication::decode_game_replication_message(*snapshot_wire);
    ASSERT_TRUE(snapshot_envelope) << snapshot_envelope.error().format();
    auto parsed_snapshot = snt::game::replication::parse_game_snapshot(*snapshot_envelope);
    ASSERT_TRUE(parsed_snapshot) << parsed_snapshot.error().format();
    EXPECT_EQ(parsed_snapshot->snapshot_id, snapshot.snapshot_id);
    ASSERT_EQ(parsed_snapshot->chunks.size(), 1u);
    EXPECT_EQ(parsed_snapshot->chunks.front().chunk.dimension_id, "overworld");
    EXPECT_EQ(parsed_snapshot->chunks.front().chunk.chunk_x, -2);
    EXPECT_EQ(parsed_snapshot->chunks.front().payload, bytes_from_text("chunk snapshot"));
    ASSERT_EQ(parsed_snapshot->entities.size(), 1u);
    EXPECT_EQ(parsed_snapshot->entities.front().entity_guid.value, 101u);
    ASSERT_EQ(parsed_snapshot->values.size(), 1u);
    EXPECT_EQ(parsed_snapshot->values.front().kind,
              snt::game::replication::GameReplicationValueKind::kQuestBook);
    EXPECT_EQ(parsed_snapshot->values.front().payload, bytes_from_text("task-book snapshot"));

    const GameDelta delta = make_test_delta(snapshot.snapshot_id, 1);
    auto delta_message = snt::game::replication::make_game_delta(delta);
    ASSERT_TRUE(delta_message) << delta_message.error().format();
    auto delta_wire = snt::game::replication::encode_game_replication_message(*delta_message);
    ASSERT_TRUE(delta_wire) << delta_wire.error().format();
    auto delta_envelope = snt::game::replication::decode_game_replication_message(*delta_wire);
    ASSERT_TRUE(delta_envelope) << delta_envelope.error().format();
    auto parsed_delta = snt::game::replication::parse_game_delta(*delta_envelope);
    ASSERT_TRUE(parsed_delta) << parsed_delta.error().format();
    EXPECT_EQ(parsed_delta->base_snapshot_id, snapshot.snapshot_id);
    EXPECT_EQ(parsed_delta->sequence, 1u);
    ASSERT_EQ(parsed_delta->chunks.size(), 1u);
    ASSERT_EQ(parsed_delta->chunks.front().blocks.size(), 1u);
    EXPECT_EQ(parsed_delta->chunks.front().blocks.front().local_index, 37u);
    EXPECT_EQ(parsed_delta->chunks.front().blocks.front().material, 9u);
    EXPECT_EQ(parsed_delta->chunks.front().blocks.front().flags, 0x5u);
    ASSERT_EQ(parsed_delta->values.size(), 1u);
    EXPECT_EQ(parsed_delta->values.front().operation,
              snt::game::replication::GameReplicationValueOperation::kRemove);
    EXPECT_TRUE(parsed_delta->values.front().payload.empty());

    GameSnapshot duplicate_chunk = snapshot;
    duplicate_chunk.chunks.push_back(duplicate_chunk.chunks.front());
    EXPECT_FALSE(snt::game::replication::make_game_snapshot(duplicate_chunk));

    GameDelta duplicate_block = delta;
    duplicate_block.chunks.front().blocks.push_back(duplicate_block.chunks.front().blocks.front());
    EXPECT_FALSE(snt::game::replication::make_game_delta(duplicate_block));

    GameSnapshot duplicate_value = snapshot;
    duplicate_value.values.push_back(duplicate_value.values.front());
    EXPECT_FALSE(snt::game::replication::make_game_snapshot(duplicate_value));
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

    EXPECT_TRUE(snt::game::replication::validate_game_replication_channel(
        GameReplicationMessageKind::kClientMovementInput,
        snt::network::ReplicationChannel::Unreliable));
    EXPECT_FALSE(snt::game::replication::validate_game_replication_channel(
        GameReplicationMessageKind::kClientMovementInput,
        snt::network::ReplicationChannel::Reliable));
}

TEST(GameReplicationHandlerTest, RoutesOnlyUnreliablePlayerMovementIntentAfterLogin) {
    RecordingAuthenticator authenticator;
    RecordingCommandSink command_sink;
    snt::game::replication::GameServerReplicationHandler handler(authenticator, &command_sink);
    const snt::network::ReplicationTickContext context{.tick_index = 71, .delta_seconds = 0.05f};
    constexpr snt::network::PeerId kPeer = 177;
    ASSERT_TRUE(handler.on_peer_connected(kPeer, context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
        .credential = {},
    });
    ASSERT_TRUE(login) << login.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*login), context));

    auto movement = snt::game::replication::make_game_player_movement_input({
        .client_sequence = 1,
        .forward_axis = 1,
        .yaw_centidegrees = -9000,
    });
    ASSERT_TRUE(movement) << movement.error().format();
    ASSERT_TRUE(handler.on_frame(
        kPeer, make_frame(*movement, snt::network::ReplicationChannel::Unreliable), context));
    EXPECT_EQ(command_sink.movement_call_count, 1);
    EXPECT_EQ(command_sink.last_movement_peer, kPeer);
    EXPECT_EQ(command_sink.last_movement_input.forward_axis, 1);
    EXPECT_EQ(command_sink.last_movement_tick_index, context.tick_index);

    EXPECT_FALSE(handler.on_frame(kPeer, make_frame(*movement), context));
}

TEST(GameReplicationHandlerTest, AuthenticatesThenQueuesCommandsAndAcknowledgesLogin) {
    RecordingAuthenticator authenticator;
    RecordingCommandSink command_sink;
    RecordingPlayerLifecycle player_lifecycle;
    snt::game::replication::GameServerReplicationHandler handler(
        authenticator, &command_sink, &player_lifecycle);
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
    EXPECT_EQ(player_lifecycle.authenticated_call_count, 1);
    EXPECT_EQ(player_lifecycle.authenticated_peer.peer, kPeer);
    EXPECT_EQ(player_lifecycle.authenticated_peer.identity.account_id,
              authenticator.identity.account_id);
    EXPECT_EQ(player_lifecycle.authenticated_tick, context.tick_index);

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
    EXPECT_EQ(command_sink.last_peer, kPeer);
    EXPECT_EQ(command_sink.last_player_id, authenticator.identity.account_id);
    EXPECT_EQ(command_sink.last_command.client_sequence, 19u);
    EXPECT_EQ(command_sink.last_command.command_type, 12u);
    EXPECT_EQ(command_sink.last_tick_index, context.tick_index);

    handler.on_peer_disconnected(kPeer, "test complete");
    EXPECT_EQ(authenticator.disconnected_player_id, authenticator.identity.account_id);
    EXPECT_EQ(authenticator.disconnect_reason, "test complete");
    EXPECT_EQ(command_sink.disconnected_call_count, 1);
    EXPECT_EQ(command_sink.disconnected_peer, kPeer);
    EXPECT_EQ(command_sink.disconnect_reason, "test complete");
    EXPECT_EQ(player_lifecycle.disconnected_call_count, 1);
    EXPECT_EQ(player_lifecycle.disconnected_peer.peer, kPeer);
    EXPECT_EQ(player_lifecycle.disconnect_reason, "test complete");
}

TEST(GameReplicationHandlerTest, EmitsBoundedSnapshotThenDeltasFromGameProviders) {
    RecordingAuthenticator authenticator;
    RecordingInterestProvider interest_provider;
    interest_provider.interest.chunks.emplace_back("overworld", -2, 3, 7);
    RecordingSnapshotSource snapshot_source;
    auto snapshot = snt::game::replication::make_game_snapshot(make_test_snapshot(91));
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    snapshot_source.initial_messages.push_back(std::move(*snapshot));
    auto delta = snt::game::replication::make_game_delta(make_test_delta(91, 1));
    ASSERT_TRUE(delta) << delta.error().format();
    snapshot_source.delta_messages.push_back(std::move(*delta));

    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_chunk_snapshots_per_tick = 1,
        .max_entity_snapshots_per_tick = 2,
        .max_block_deltas_per_tick = 1,
    };
    snt::game::replication::GameServerReplicationHandler handler(
        authenticator, nullptr, nullptr, &interest_provider, &snapshot_source, budget);
    const snt::network::ReplicationTickContext login_context{
        .tick_index = 41,
        .delta_seconds = 0.05f,
    };
    constexpr snt::network::PeerId kPeer = 118;
    ASSERT_TRUE(handler.on_peer_connected(kPeer, login_context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
    });
    ASSERT_TRUE(login) << login.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*login), login_context));

    RecordingFrameSink initial_sink;
    ASSERT_TRUE(handler.emit_outbound(login_context, initial_sink));
    ASSERT_EQ(initial_sink.sent.size(), 2u);
    EXPECT_EQ(interest_provider.call_count, 1);
    EXPECT_EQ(snapshot_source.initial_call_count, 1);
    EXPECT_EQ(snapshot_source.delta_call_count, 0);
    EXPECT_EQ(snapshot_source.last_peer.peer, kPeer);
    EXPECT_EQ(snapshot_source.last_budget.max_chunk_snapshots_per_tick, 1u);
    auto snapshot_envelope = snt::game::replication::decode_game_replication_message(
        initial_sink.sent[1].second.payload);
    ASSERT_TRUE(snapshot_envelope) << snapshot_envelope.error().format();
    auto parsed_snapshot = snt::game::replication::parse_game_snapshot(*snapshot_envelope);
    ASSERT_TRUE(parsed_snapshot) << parsed_snapshot.error().format();
    EXPECT_EQ(parsed_snapshot->snapshot_id, 91u);

    const snt::network::ReplicationTickContext delta_context{
        .tick_index = 42,
        .delta_seconds = 0.05f,
    };
    RecordingFrameSink delta_sink;
    ASSERT_TRUE(handler.emit_outbound(delta_context, delta_sink));
    ASSERT_EQ(delta_sink.sent.size(), 1u);
    EXPECT_EQ(interest_provider.call_count, 2);
    EXPECT_EQ(snapshot_source.initial_call_count, 1);
    EXPECT_EQ(snapshot_source.delta_call_count, 1);
    auto delta_envelope = snt::game::replication::decode_game_replication_message(
        delta_sink.sent.front().second.payload);
    ASSERT_TRUE(delta_envelope) << delta_envelope.error().format();
    auto parsed_delta = snt::game::replication::parse_game_delta(*delta_envelope);
    ASSERT_TRUE(parsed_delta) << parsed_delta.error().format();
    EXPECT_EQ(parsed_delta->base_snapshot_id, 91u);
    EXPECT_EQ(parsed_delta->sequence, 1u);
}

TEST(GameReplicationHandlerTest, RejectsOverBudgetProviderOutputWithoutPartialFrames) {
    RecordingAuthenticator authenticator;
    RecordingInterestProvider interest_provider;
    RecordingSnapshotSource snapshot_source;
    auto snapshot = snt::game::replication::make_game_snapshot(make_test_snapshot(101));
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    snapshot_source.initial_messages.push_back(std::move(*snapshot));
    auto first_delta = snt::game::replication::make_game_delta(make_test_delta(101, 1));
    ASSERT_TRUE(first_delta) << first_delta.error().format();
    auto second_delta = snt::game::replication::make_game_delta(make_test_delta(101, 2));
    ASSERT_TRUE(second_delta) << second_delta.error().format();
    snapshot_source.delta_messages = {std::move(*first_delta), std::move(*second_delta)};

    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_chunk_snapshots_per_tick = 1,
        .max_entity_snapshots_per_tick = 4,
        .max_block_deltas_per_tick = 1,
    };
    snt::game::replication::GameServerReplicationHandler handler(
        authenticator, nullptr, nullptr, &interest_provider, &snapshot_source, budget);
    const snt::network::ReplicationTickContext login_context{
        .tick_index = 51,
        .delta_seconds = 0.05f,
    };
    constexpr snt::network::PeerId kPeer = 119;
    ASSERT_TRUE(handler.on_peer_connected(kPeer, login_context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
    });
    ASSERT_TRUE(login) << login.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*login), login_context));

    RecordingFrameSink initial_sink;
    ASSERT_TRUE(handler.emit_outbound(login_context, initial_sink));
    ASSERT_EQ(initial_sink.sent.size(), 2u);

    const snt::network::ReplicationTickContext delta_context{
        .tick_index = 52,
        .delta_seconds = 0.05f,
    };
    RecordingFrameSink delta_sink;
    const auto rejected = handler.emit_outbound(delta_context, delta_sink);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), snt::core::ErrorCode::kProtocolError);
    EXPECT_TRUE(delta_sink.sent.empty());
    EXPECT_EQ(snapshot_source.delta_call_count, 1);
}

TEST(GameReplicationHandlerTest, WaitsForQueuedSnapshotBeforeProducingMoreReplication) {
    RecordingAuthenticator authenticator;
    RecordingInterestProvider interest_provider;
    RecordingSnapshotSource snapshot_source;
    auto snapshot = snt::game::replication::make_game_snapshot(make_test_snapshot(102));
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    snapshot_source.initial_messages.push_back(std::move(*snapshot));
    auto delta = snt::game::replication::make_game_delta(make_test_delta(102, 1));
    ASSERT_TRUE(delta) << delta.error().format();
    snapshot_source.delta_messages.push_back(std::move(*delta));

    snt::game::replication::GameServerReplicationHandler handler(
        authenticator, nullptr, nullptr, &interest_provider, &snapshot_source);
    const snt::network::ReplicationTickContext login_context{
        .tick_index = 53,
        .delta_seconds = 0.05f,
    };
    constexpr snt::network::PeerId kPeer = 120;
    ASSERT_TRUE(handler.on_peer_connected(kPeer, login_context));
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
    });
    ASSERT_TRUE(login) << login.error().format();
    ASSERT_TRUE(handler.on_frame(kPeer, make_frame(*login), login_context));

    RecordingFrameSink blocked_sink;
    blocked_sink.reject_sends = true;
    const auto blocked = handler.emit_outbound(login_context, blocked_sink);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(snapshot_source.initial_call_count, 1);
    EXPECT_EQ(snapshot_source.delta_call_count, 0);
    EXPECT_TRUE(blocked_sink.sent.empty());

    const snt::network::ReplicationTickContext retry_context{
        .tick_index = 54,
        .delta_seconds = 0.05f,
    };
    RecordingFrameSink retry_sink;
    ASSERT_TRUE(handler.emit_outbound(retry_context, retry_sink));
    ASSERT_EQ(retry_sink.sent.size(), 2u);
    EXPECT_EQ(snapshot_source.initial_call_count, 1);
    EXPECT_EQ(snapshot_source.delta_call_count, 0);

    const snt::network::ReplicationTickContext delta_context{
        .tick_index = 55,
        .delta_seconds = 0.05f,
    };
    RecordingFrameSink delta_sink;
    ASSERT_TRUE(handler.emit_outbound(delta_context, delta_sink));
    ASSERT_EQ(delta_sink.sent.size(), 1u);
    EXPECT_EQ(snapshot_source.delta_call_count, 1);
}

TEST(GameReplicationHandlerTest, ReleasesSnapshotSourcePeerStateOnDisconnectAndTakeover) {
    const snt::network::ReplicationTickContext context{
        .tick_index = 56,
        .delta_seconds = 0.05f,
    };
    auto login = snt::game::replication::make_game_login_request({
        .identity_provider = PlayerIdentityProvider::kLocalName,
        .display_name = "Alice",
    });
    ASSERT_TRUE(login) << login.error().format();

    RecordingAuthenticator disconnect_authenticator;
    RecordingInterestProvider disconnect_interest;
    RecordingSnapshotSource disconnect_source;
    snt::game::replication::GameServerReplicationHandler disconnect_handler(
        disconnect_authenticator, nullptr, nullptr, &disconnect_interest, &disconnect_source);
    ASSERT_TRUE(disconnect_handler.on_peer_connected(121, context));
    ASSERT_TRUE(disconnect_handler.on_frame(121, make_frame(*login), context));
    disconnect_handler.on_peer_disconnected(121, "test disconnect");
    EXPECT_EQ(disconnect_source.disconnected_call_count, 1);
    EXPECT_EQ(disconnect_source.disconnected_peer.peer, 121u);
    EXPECT_EQ(disconnect_source.disconnect_reason, "test disconnect");

    RecordingAuthenticator takeover_authenticator;
    RecordingInterestProvider takeover_interest;
    RecordingSnapshotSource takeover_source;
    snt::game::replication::GameServerReplicationHandler takeover_handler(
        takeover_authenticator, nullptr, nullptr, &takeover_interest, &takeover_source);
    ASSERT_TRUE(takeover_handler.on_peer_connected(122, context));
    ASSERT_TRUE(takeover_handler.on_peer_connected(123, context));
    ASSERT_TRUE(takeover_handler.on_frame(122, make_frame(*login), context));
    ASSERT_TRUE(takeover_handler.on_frame(123, make_frame(*login), context));
    EXPECT_EQ(takeover_source.disconnected_call_count, 1);
    EXPECT_EQ(takeover_source.disconnected_peer.peer, 122u);
    EXPECT_EQ(takeover_source.disconnect_reason,
              "player account session was replaced by a newer login");
}

TEST(GameServerCommandSinkTest, ClaimsAutomaticallyProgressedRewardsAndCancelsDisconnectedPeerCommands) {
    GameContentRegistry content;
    QuestDefinition quest;
    quest.id = "network.quest.reward";
    quest.title = "Network Quest";
    quest.description = "Progressed through authoritative gameplay events";
    quest.objectives.push_back({
        .id = "craft",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = 1,
    });
    quest.rewards.push_back({
        .kind = QuestRewardKind::kItem,
        .target_id = "wrought_iron",
        .count = 2,
    });
    ASSERT_TRUE(content.register_builtin_quest(std::move(quest)));

    QuestDefinition canceled_quest;
    canceled_quest.id = "network.quest.canceled";
    canceled_quest.title = "Canceled Network Quest";
    canceled_quest.description = "Must not apply after disconnect";
    canceled_quest.objectives.push_back({
        .id = "craft",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "copper_ingot",
        .required_count = 1,
    });
    ASSERT_TRUE(content.register_builtin_quest(std::move(canceled_quest)));

    QuestRegistry quests(content);
    RecordingQuestRewardSink reward_sink;
    quests.set_reward_sink(&reward_sink);
    snt::game::replication::GameServerCommandSink sink(quests);
    const snt::network::ReplicationTickContext context{.tick_index = 13, .delta_seconds = 0.05f};

    GameAuthenticatedPeer reward_peer{
        .peer = 70,
        .identity = make_local_identity("AuthoritativePlayer"),
    };
    ASSERT_TRUE(quests.record_progress(
        reward_peer.identity.account_id,
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1},
        context.tick_index));
    const auto* progress = quests.find_progress(
        reward_peer.identity.account_id, "network.quest.reward");
    ASSERT_NE(progress, nullptr);
    ASSERT_EQ(progress->state, QuestState::kCompleted);

    auto claim = snt::game::replication::make_game_quest_claim_reward_command(
        4, {.quest_id = "network.quest.reward"});
    ASSERT_TRUE(claim) << claim.error().format();
    ASSERT_TRUE(sink.enqueue_client_command(reward_peer, std::move(*claim), context));

    auto duplicate = snt::game::replication::make_game_quest_claim_reward_command(
        4, {.quest_id = "network.quest.reward"});
    ASSERT_TRUE(duplicate) << duplicate.error().format();
    const auto duplicate_result = sink.enqueue_client_command(
        reward_peer, std::move(*duplicate), context);
    ASSERT_FALSE(duplicate_result);
    EXPECT_EQ(duplicate_result.error().code(), snt::core::ErrorCode::kProtocolError);

    GameAuthenticatedPeer disconnected_peer{
        .peer = 71,
        .identity = make_local_identity("DisconnectedPlayer"),
    };
    auto canceled = snt::game::replication::make_game_quest_claim_reward_command(
        1, {.quest_id = "network.quest.canceled"});
    ASSERT_TRUE(canceled) << canceled.error().format();
    ASSERT_TRUE(sink.enqueue_client_command(disconnected_peer, std::move(*canceled), context));
    sink.on_peer_disconnected(disconnected_peer, "test disconnected before tick");

    ASSERT_EQ(sink.pending_command_count(), 1u);
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index));
    progress = quests.find_progress(reward_peer.identity.account_id, "network.quest.reward");
    ASSERT_NE(progress, nullptr);
    EXPECT_TRUE(progress->reward_claimed);
    EXPECT_EQ(quests.find_progress(disconnected_peer.identity.account_id,
                                   "network.quest.canceled"), nullptr);
    ASSERT_EQ(reward_sink.player_ids.size(), 1u);
    EXPECT_EQ(reward_sink.player_ids.front(), reward_peer.identity.account_id);
    ASSERT_EQ(reward_sink.quest_ids.size(), 1u);
    EXPECT_EQ(reward_sink.quest_ids.front(), "network.quest.reward");
    ASSERT_EQ(reward_sink.grants.size(), 1u);
    ASSERT_EQ(reward_sink.grants.front().size(), 1u);
    EXPECT_EQ(reward_sink.grants.front().front().item_id, "wrought_iron");
    EXPECT_EQ(reward_sink.grants.front().front().count, 2);

    const uint64_t claimed_revision = quests.progress_revision(reward_peer.identity.account_id);
    auto repeated_claim = snt::game::replication::make_game_quest_claim_reward_command(
        5, {.quest_id = "network.quest.reward"});
    ASSERT_TRUE(repeated_claim) << repeated_claim.error().format();
    ASSERT_TRUE(sink.enqueue_client_command(reward_peer, std::move(*repeated_claim), context));
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index + 1));
    EXPECT_EQ(quests.progress_revision(reward_peer.identity.account_id), claimed_revision);
    EXPECT_EQ(reward_sink.grants.size(), 1u);
}

TEST(GameServerCommandSinkTest, CoalescesLatestMovementIntentPerPeer) {
    GameContentRegistry content;
    QuestRegistry quests(content);
    RecordingMovementInputSink movement;
    snt::game::replication::GameServerCommandSink sink(quests, &movement);
    const snt::network::ReplicationTickContext context{.tick_index = 23, .delta_seconds = 0.05f};
    const GameAuthenticatedPeer peer{
        .peer = 72,
        .identity = make_local_identity("MovementCommandPlayer"),
    };

    ASSERT_TRUE(sink.enqueue_player_movement_input(
        peer, {.client_sequence = 1, .forward_axis = 1, .yaw_centidegrees = -9000}, context));
    ASSERT_TRUE(sink.enqueue_player_movement_input(
        peer, {.client_sequence = 2, .strafe_axis = 1, .yaw_centidegrees = -9000}, context));
    EXPECT_EQ(sink.pending_command_count(), 1u);
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index));
    EXPECT_EQ(movement.call_count, 1);
    EXPECT_EQ(movement.last_peer.peer, peer.peer);
    EXPECT_EQ(movement.last_input.client_sequence, 2u);
    EXPECT_EQ(movement.last_input.forward_axis, 0);
    EXPECT_EQ(movement.last_input.strafe_axis, 1);
    EXPECT_EQ(movement.last_tick_index, context.tick_index);

    ASSERT_TRUE(sink.enqueue_player_movement_input(
        peer, {.client_sequence = 3, .forward_axis = -1, .yaw_centidegrees = -9000}, context));
    sink.on_peer_disconnected(peer, "test input disconnect");
    EXPECT_EQ(sink.pending_command_count(), 0u);
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index + 1));
    EXPECT_EQ(movement.call_count, 1);
    EXPECT_EQ(movement.disconnected_call_count, 1);
}

TEST(GameServerPlayerLifecycleTest, TransfersTakeoverStateAndPersistsItAcrossRestart) {
    const auto save_dir = make_player_lifecycle_save_dir();
    GameContentRegistry content;
    QuestDefinition quest;
    quest.id = "network.quest.lifecycle";
    quest.title = "Lifecycle Quest";
    quest.description = "Persisted through authoritative player lifecycle";
    quest.objectives.push_back({
        .id = "craft",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = 3,
    });
    ASSERT_TRUE(content.register_builtin_quest(std::move(quest)));

    const auto identity = make_local_identity("LifecyclePlayer");
    const snt::network::ReplicationTickContext context{.tick_index = 31, .delta_seconds = 0.05f};
    GameAuthenticatedPeer original{.peer = 81, .identity = identity};
    GameAuthenticatedPeer replacement{.peer = 82, .identity = identity};

    QuestRegistry live_quests(content);
    snt::ecs::World live_world;
    auto live_player_state = snt::game::replication::GameServerPlayerState::create(live_world);
    ASSERT_TRUE(live_player_state) << live_player_state.error().format();
    snt::game::replication::GameServerPlayerLifecycle live_lifecycle(
        live_quests, **live_player_state, save_dir.string());
    ASSERT_TRUE(live_lifecycle.on_peer_authenticated(original, context));
    ASSERT_TRUE((*live_player_state)->set_authoritative_position(
        original, {.dimension_id = "overworld", .position = {.x = 23, .y = 71, .z = -6}}));
    ASSERT_TRUE((*live_player_state)->apply_inventory_transaction(
        original, {.additions = {{.item_id = "iron_ingot", .count = 4}}}));
    ASSERT_TRUE(live_quests.record_progress(
        identity.account_id,
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 2},
        context.tick_index + 1));

    ASSERT_TRUE(live_lifecycle.on_peer_replaced(original, replacement, context));
    EXPECT_EQ(live_lifecycle.active_player_count(), 1u);
    const auto* live_progress = live_quests.find_progress(
        identity.account_id, "network.quest.lifecycle");
    ASSERT_NE(live_progress, nullptr);
    EXPECT_EQ(live_progress->objective_counts.at("craft"), 2);

    live_lifecycle.on_peer_disconnected(replacement, "test disconnect");
    EXPECT_EQ(live_lifecycle.active_player_count(), 0u);
    live_lifecycle.shutdown();
    (*live_player_state)->shutdown();

    QuestRegistry restarted_quests(content);
    snt::ecs::World restarted_world;
    auto restarted_player_state = snt::game::replication::GameServerPlayerState::create(
        restarted_world);
    ASSERT_TRUE(restarted_player_state) << restarted_player_state.error().format();
    snt::game::replication::GameServerPlayerLifecycle restarted_lifecycle(
        restarted_quests, **restarted_player_state, save_dir.string());
    GameAuthenticatedPeer reconnect{.peer = 83, .identity = identity};
    ASSERT_TRUE(restarted_lifecycle.on_peer_authenticated(reconnect, context));
    const auto* restored_progress = restarted_quests.find_progress(
        identity.account_id, "network.quest.lifecycle");
    ASSERT_NE(restored_progress, nullptr);
    EXPECT_EQ(restored_progress->state, QuestState::kInProgress);
    EXPECT_EQ(restored_progress->objective_counts.at("craft"), 2);
    auto restored_player = (*restarted_player_state)->snapshot_for_peer(reconnect);
    ASSERT_TRUE(restored_player) << restored_player.error().format();
    EXPECT_EQ(restored_player->position.position.x, 23);
    auto restored_inventory = (*restarted_player_state)->inventory_for_peer(reconnect);
    ASSERT_TRUE(restored_inventory) << restored_inventory.error().format();
    ASSERT_EQ(restored_inventory->slots.size(), 36u);
    EXPECT_EQ(restored_inventory->slots[0].item_id, "iron_ingot");
    EXPECT_EQ(restored_inventory->slots[0].count, 4);
    restarted_lifecycle.shutdown();
    (*restarted_player_state)->shutdown();

    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameServerPlayerLifecycleTest, AutosavesOnlyChangedProgressAtConfiguredInterval) {
    const auto save_dir = make_player_lifecycle_save_dir();
    GameContentRegistry content;
    QuestDefinition quest;
    quest.id = "network.quest.autosave";
    quest.title = "Autosave Quest";
    quest.description = "Progress is persisted only after a value change";
    quest.objectives.push_back({
        .id = "craft",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = 2,
    });
    ASSERT_TRUE(content.register_builtin_quest(std::move(quest)));

    const auto identity = make_local_identity("AutosavePlayer");
    const snt::network::ReplicationTickContext context{.tick_index = 0, .delta_seconds = 0.05f};
    GameAuthenticatedPeer peer{.peer = 84, .identity = identity};

    QuestRegistry quests(content);
    snt::ecs::World world;
    auto player_state = snt::game::replication::GameServerPlayerState::create(world);
    ASSERT_TRUE(player_state) << player_state.error().format();
    snt::game::replication::GameServerPlayerLifecycle lifecycle(
        quests, **player_state, save_dir.string(), 5);
    ASSERT_TRUE(lifecycle.on_peer_authenticated(peer, context));
    ASSERT_TRUE(lifecycle.flush_due(0));
    ASSERT_TRUE(quests.tick(1));
    ASSERT_TRUE(lifecycle.flush_due(4));
    EXPECT_TRUE(find_player_progress_file(save_dir).empty());

    ASSERT_TRUE(lifecycle.flush_due(5));
    const auto progress_file = find_player_progress_file(save_dir);
    ASSERT_FALSE(progress_file.empty());
    const auto first_write = std::filesystem::last_write_time(progress_file);

    // NTFS timestamps distinguish the following due checkpoint from the
    // first write. A clean revision must leave the primary file untouched.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(lifecycle.flush_due(10));
    EXPECT_EQ(std::filesystem::last_write_time(progress_file), first_write);

    ASSERT_TRUE(quests.record_progress(
        identity.account_id,
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1}, 11));
    ASSERT_TRUE(lifecycle.flush_due(15));

    snt::game::GameSaveQuestProgressPersistence persistence(save_dir.string());
    auto restored = persistence.load_player_progress(identity.account_id);
    ASSERT_TRUE(restored) << restored.error().format();
    ASSERT_EQ(restored->size(), 1u);
    EXPECT_EQ(restored->front().objective_counts.at("craft"), 1);

    lifecycle.shutdown();
    (*player_state)->shutdown();
    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(GameServerPlayerLifecycleTest, AutosavesMarkedAuthoritativePlayerStateAtConfiguredInterval) {
    const auto save_dir = make_player_lifecycle_save_dir();
    GameContentRegistry content;
    QuestRegistry quests(content);
    snt::ecs::World world;
    auto player_state = snt::game::replication::GameServerPlayerState::create(world);
    ASSERT_TRUE(player_state) << player_state.error().format();

    const auto identity = make_local_identity("StateAutosavePlayer");
    GameAuthenticatedPeer peer{.peer = 85, .identity = identity};
    const snt::network::ReplicationTickContext context{.tick_index = 0, .delta_seconds = 0.05f};
    snt::game::replication::GameServerPlayerLifecycle lifecycle(
        quests, **player_state, save_dir.string(), 5);
    ASSERT_TRUE(lifecycle.on_peer_authenticated(peer, context));
    ASSERT_TRUE((*player_state)->set_authoritative_position(
        peer, {.dimension_id = "overworld", .position = {.x = 17, .y = 71, .z = -9}}));
    ASSERT_TRUE(lifecycle.mark_player_state_dirty(peer));
    ASSERT_TRUE(lifecycle.flush_due(0));

    snt::game::GameSavePlayerStatePersistence persistence(save_dir.string());
    auto first = persistence.load_player_state(identity.account_id);
    ASSERT_TRUE(first) << first.error().format();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((**first).position.position.x, 17);

    ASSERT_TRUE((*player_state)->set_authoritative_position(
        peer, {.dimension_id = "overworld", .position = {.x = 21, .y = 71, .z = -9}}));
    ASSERT_TRUE(lifecycle.flush_due(5));
    auto unmarked = persistence.load_player_state(identity.account_id);
    ASSERT_TRUE(unmarked) << unmarked.error().format();
    ASSERT_TRUE(unmarked->has_value());
    EXPECT_EQ((**unmarked).position.position.x, 17);

    ASSERT_TRUE(lifecycle.mark_player_state_dirty(peer));
    ASSERT_TRUE(lifecycle.flush_due(10));
    auto updated = persistence.load_player_state(identity.account_id);
    ASSERT_TRUE(updated) << updated.error().format();
    ASSERT_TRUE(updated->has_value());
    EXPECT_EQ((**updated).position.position.x, 21);

    lifecycle.shutdown();
    (*player_state)->shutdown();
    std::error_code error;
    std::filesystem::remove_all(save_dir, error);
    EXPECT_FALSE(error) << error.message();
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
    RecordingPlayerLifecycle player_lifecycle;
    snt::game::replication::GameServerReplicationHandler handler(
        authenticator, nullptr, &player_lifecycle);
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
    EXPECT_EQ(player_lifecycle.authenticated_call_count, 1);
    EXPECT_EQ(player_lifecycle.replaced_call_count, 1);
    EXPECT_EQ(player_lifecycle.previous.peer, 51u);
    EXPECT_EQ(player_lifecycle.replacement.peer, 52u);
    EXPECT_EQ(player_lifecycle.replacement_tick, context.tick_index);
    EXPECT_EQ(player_lifecycle.disconnected_call_count, 0);

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

TEST(GameClientReplicationSessionTest, RejectsLoginAcceptedForADifferentLocalAccount) {
    auto transport = std::make_unique<ControlledReplicationTransport>();
    ControlledReplicationTransport* const transport_view = transport.get();
    transport_view->peers = {snt::network::kServerPeerId};
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::PeerConnected,
        .peer = snt::network::kServerPeerId,
    });

    auto session = snt::game::replication::GameClientReplicationSession::create(
        std::move(transport),
        {.local_identity = make_local_identity("ExpectedAccount")});
    ASSERT_TRUE(session) << session.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 17, .delta_seconds = 0.05f};
    ASSERT_TRUE((*session)->poll_inbound(context));
    ASSERT_TRUE((*session)->emit_outbound(context));
    ASSERT_EQ(transport_view->sent.size(), 1u);

    auto unexpected_identity = snt::game::make_local_name_player_identity("UnexpectedAccount");
    ASSERT_TRUE(unexpected_identity) << unexpected_identity.error().format();
    auto accepted = snt::game::replication::make_game_login_accepted({
        .identity = std::move(*unexpected_identity),
    });
    ASSERT_TRUE(accepted) << accepted.error().format();
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*accepted),
    });

    // ReplicationService converts a game protocol rejection into a local
    // disconnect, so poll itself succeeds while the value status records it.
    ASSERT_TRUE((*session)->poll_inbound(context));
    EXPECT_EQ((*session)->status().state,
              snt::game::replication::GameClientConnectionState::kDisconnected);
    ASSERT_EQ(transport_view->disconnected.size(), 1u);
    EXPECT_EQ(transport_view->disconnected.front().first, snt::network::kServerPeerId);

    (*session)->shutdown();
    EXPECT_TRUE(transport_view->shutdown_called);
}

TEST(GameClientReplicationSessionTest, DrainsAuthenticatedSnapshotAndDeltaValueUpdates) {
    auto transport = std::make_unique<ControlledReplicationTransport>();
    ControlledReplicationTransport* const transport_view = transport.get();
    transport_view->peers = {snt::network::kServerPeerId};
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::PeerConnected,
        .peer = snt::network::kServerPeerId,
    });

    auto session = snt::game::replication::GameClientReplicationSession::create(
        std::move(transport),
        {.local_identity = make_local_identity("SnapshotClient")});
    ASSERT_TRUE(session) << session.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 61, .delta_seconds = 0.05f};
    ASSERT_TRUE((*session)->poll_inbound(context));
    ASSERT_TRUE((*session)->emit_outbound(context));

    auto accepted = snt::game::replication::make_game_login_accepted({
        .identity = make_local_identity("SnapshotClient"),
    });
    ASSERT_TRUE(accepted) << accepted.error().format();
    auto snapshot = snt::game::replication::make_game_snapshot(make_test_snapshot(111));
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    auto delta = snt::game::replication::make_game_delta(make_test_delta(111, 1));
    ASSERT_TRUE(delta) << delta.error().format();
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*accepted),
    });
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*snapshot),
    });
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*delta),
    });

    ASSERT_TRUE((*session)->poll_inbound(context));
    EXPECT_EQ((*session)->status().state,
              snt::game::replication::GameClientConnectionState::kAuthenticated);
    auto updates = (*session)->drain_replication_updates();
    ASSERT_EQ(updates.size(), 2u);
    const auto* drained_snapshot = std::get_if<GameSnapshot>(&updates[0]);
    ASSERT_NE(drained_snapshot, nullptr);
    EXPECT_EQ(drained_snapshot->snapshot_id, 111u);
    const auto* drained_delta = std::get_if<GameDelta>(&updates[1]);
    ASSERT_NE(drained_delta, nullptr);
    EXPECT_EQ(drained_delta->base_snapshot_id, 111u);
    EXPECT_EQ(drained_delta->sequence, 1u);
    EXPECT_TRUE((*session)->drain_replication_updates().empty());
}

TEST(GameClientReplicationSessionTest, QueuesLatestMovementIntentOnTheUnreliableChannel) {
    auto transport = std::make_unique<ControlledReplicationTransport>();
    ControlledReplicationTransport* const transport_view = transport.get();
    transport_view->peers = {snt::network::kServerPeerId};
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::PeerConnected,
        .peer = snt::network::kServerPeerId,
    });

    auto session = snt::game::replication::GameClientReplicationSession::create(
        std::move(transport), {.local_identity = make_local_identity("MovementClient")});
    ASSERT_TRUE(session) << session.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 64, .delta_seconds = 0.05f};
    ASSERT_TRUE((*session)->poll_inbound(context));
    ASSERT_TRUE((*session)->emit_outbound(context));
    transport_view->sent.clear();

    auto accepted = snt::game::replication::make_game_login_accepted({
        .identity = make_local_identity("MovementClient"),
    });
    ASSERT_TRUE(accepted) << accepted.error().format();
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*accepted),
    });
    ASSERT_TRUE((*session)->poll_inbound(context));
    ASSERT_EQ((*session)->status().state,
              snt::game::replication::GameClientConnectionState::kAuthenticated);

    ASSERT_TRUE((*session)->enqueue_player_movement_input({
        .client_sequence = 1,
        .forward_axis = 1,
        .flags = snt::game::replication::kGamePlayerMovementFlagSprint,
        .yaw_centidegrees = -9000,
    }));
    ASSERT_TRUE((*session)->emit_outbound(context));
    ASSERT_EQ(transport_view->sent.size(), 1u);
    EXPECT_EQ(transport_view->sent.front().second.channel,
              snt::network::ReplicationChannel::Unreliable);
    auto message = snt::game::replication::decode_game_replication_message(
        transport_view->sent.front().second.payload);
    ASSERT_TRUE(message) << message.error().format();
    auto movement = snt::game::replication::parse_game_player_movement_input(*message);
    ASSERT_TRUE(movement) << movement.error().format();
    EXPECT_EQ(movement->client_sequence, 1u);
    EXPECT_EQ(movement->forward_axis, 1);
}

TEST(GameClientReplicationSessionTest, RejectsDeltaThatDoesNotMatchAnAcceptedSnapshot) {
    auto transport = std::make_unique<ControlledReplicationTransport>();
    ControlledReplicationTransport* const transport_view = transport.get();
    transport_view->peers = {snt::network::kServerPeerId};
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::PeerConnected,
        .peer = snt::network::kServerPeerId,
    });

    auto session = snt::game::replication::GameClientReplicationSession::create(
        std::move(transport),
        {.local_identity = make_local_identity("DeltaClient")});
    ASSERT_TRUE(session) << session.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 62, .delta_seconds = 0.05f};
    ASSERT_TRUE((*session)->poll_inbound(context));
    ASSERT_TRUE((*session)->emit_outbound(context));

    auto accepted = snt::game::replication::make_game_login_accepted({
        .identity = make_local_identity("DeltaClient"),
    });
    ASSERT_TRUE(accepted) << accepted.error().format();
    auto mismatched_delta = snt::game::replication::make_game_delta(make_test_delta(112, 1));
    ASSERT_TRUE(mismatched_delta) << mismatched_delta.error().format();
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*accepted),
    });
    transport_view->events.push_back({
        .kind = snt::network::ReplicationEventKind::FrameReceived,
        .peer = snt::network::kServerPeerId,
        .frame = make_frame(*mismatched_delta),
    });

    ASSERT_TRUE((*session)->poll_inbound(context));
    EXPECT_EQ((*session)->status().state,
              snt::game::replication::GameClientConnectionState::kDisconnected);
    EXPECT_TRUE((*session)->drain_replication_updates().empty());
    ASSERT_EQ(transport_view->disconnected.size(), 1u);
    EXPECT_EQ(transport_view->disconnected.front().first, snt::network::kServerPeerId);
}

TEST(GameClientReplicationSessionTest, CompletesLocalLoginAndClaimsAutomaticallyProgressedRewardsOverTcpUdp) {
    auto server_transport = snt::network::TcpUdpReplicationTransport::listen({
        .bind_address = "127.0.0.1",
        .tcp_port = 0,
        .udp_port = 0,
    });
    ASSERT_TRUE(server_transport) << server_transport.error().format();

    GameContentRegistry content;
    QuestDefinition quest;
    quest.id = "network.quest.automatic";
    quest.title = "Network Quest";
    quest.description = "Automatically progressed over the localhost replication path";
    quest.objectives.push_back({
        .id = "craft",
        .kind = QuestObjectiveKind::kCraftItem,
        .target_id = "iron_ingot",
        .required_count = 2,
    });
    ASSERT_TRUE(content.register_builtin_quest(std::move(quest)));
    QuestRegistry quests(content);
    ASSERT_TRUE(quests.refresh_definitions());

    snt::game::replication::GameAccountPeerAuthenticator authenticator;
    snt::game::replication::GameServerCommandSink command_sink(quests);
    snt::game::replication::GameServerReplicationHandler server_handler(
        authenticator, &command_sink);
    snt::network::ReplicationService server_service(*(*server_transport), server_handler);

    auto local_identity = snt::game::make_local_name_player_identity("NetworkClient");
    ASSERT_TRUE(local_identity) << local_identity.error().format();
    auto client_session = snt::game::replication::GameClientReplicationSession::connect_tcp_udp(
        {
            .host = "127.0.0.1",
            .tcp_port = (*server_transport)->tcp_port(),
            .udp_port = (*server_transport)->udp_port(),
        },
        {.local_identity = std::move(*local_identity)});
    ASSERT_TRUE(client_session) << client_session.error().format();

    bool authenticated = false;
    for (uint64_t tick = 1; tick <= 500; ++tick) {
        const snt::network::ReplicationTickContext context{
            .tick_index = tick,
            .delta_seconds = 0.05f,
        };
        ASSERT_TRUE(server_service.poll_inbound(context));
        ASSERT_TRUE(command_sink.apply_pending_commands(context.tick_index));
        ASSERT_TRUE((*client_session)->poll_inbound(context));
        ASSERT_TRUE((*client_session)->emit_outbound(context));
        ASSERT_TRUE(server_service.emit_outbound(context));

        const auto status = (*client_session)->status();
        if (status.state == snt::game::replication::GameClientConnectionState::kAuthenticated) {
            ASSERT_TRUE(status.authenticated_identity.has_value());
            EXPECT_EQ(status.authenticated_identity->account_id, "local-name:NetworkClient");
            authenticated = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(authenticated) << "Timed out waiting for client login acceptance";

    constexpr std::string_view kQuestId = "network.quest.automatic";
    ASSERT_TRUE(quests.record_progress(
        "local-name:NetworkClient",
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1},
        501));
    const auto* progress = quests.find_progress("local-name:NetworkClient", kQuestId);
    ASSERT_NE(progress, nullptr);
    ASSERT_EQ(progress->state, QuestState::kInProgress);

    ASSERT_TRUE(quests.record_progress(
        "local-name:NetworkClient",
        {.kind = QuestObjectiveKind::kCraftItem, .target_id = "iron_ingot", .amount = 1},
        502));
    const auto* completed_progress = quests.find_progress("local-name:NetworkClient", kQuestId);
    ASSERT_NE(completed_progress, nullptr);
    ASSERT_EQ(completed_progress->state, QuestState::kCompleted);

    const snt::game::replication::GameQuestClaimRewardCommand claim_command{
        .quest_id = std::string(kQuestId),
    };
    ASSERT_TRUE((*client_session)->enqueue_quest_claim_reward(11, claim_command));
    const auto duplicate_sequence = (*client_session)->enqueue_quest_claim_reward(11, claim_command);
    ASSERT_FALSE(duplicate_sequence);
    EXPECT_EQ(duplicate_sequence.error().code(), snt::core::ErrorCode::kInvalidArgument);

    bool reward_claimed = false;
    for (uint64_t tick = 503; tick <= 1000; ++tick) {
        const snt::network::ReplicationTickContext context{
            .tick_index = tick,
            .delta_seconds = 0.05f,
        };
        ASSERT_TRUE((*client_session)->poll_inbound(context));
        ASSERT_TRUE((*client_session)->emit_outbound(context));
        ASSERT_TRUE(server_service.poll_inbound(context));
        ASSERT_TRUE(command_sink.apply_pending_commands(context.tick_index));
        ASSERT_TRUE(server_service.emit_outbound(context));
        progress = quests.find_progress("local-name:NetworkClient", kQuestId);
        if (progress != nullptr && progress->reward_claimed) {
            reward_claimed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(reward_claimed) << "Timed out waiting for authoritative quest reward claim";

    (*client_session)->shutdown();
    server_service.shutdown();
}

}  // namespace
