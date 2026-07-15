// ScienceAndTheology game replication message format.
//
// Ownership: this module owns gameplay protocol versions and message meaning.
// The reusable snt_network transport carries these bytes but must never include
// this header or interpret a game message. The format is deliberately latest-
// version-only: an incompatible game protocol is rejected instead of adapted.

#pragma once

#include "core/expected.h"
#include "game/player/player_identity.h"
#include "network/replication.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snt::game::replication {

inline constexpr uint32_t kGameReplicationMagic = 0x534E5447u;  // "SNTG"
inline constexpr uint16_t kCurrentGameReplicationProtocolVersion = 3;
inline constexpr size_t kGameReplicationHeaderBytes = 12;
inline constexpr size_t kMaxGameReplicationPayloadBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaxGamePlayerNameBytes = kMaxPlayerDisplayNameBytes;
inline constexpr size_t kMaxGamePlayerIdBytes = kMaxPlayerAccountIdBytes;
inline constexpr size_t kMaxGameCredentialBytes = 1024;
inline constexpr size_t kMaxGameCommandPayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxGameQuestIdBytes = 512;

// Client and server message ranges are intentionally distinct. New message
// kinds must be added here before a handler accepts them, so an unrecognized
// payload never gains gameplay meaning accidentally.
enum class GameReplicationMessageKind : uint8_t {
    kClientLoginRequest = 1,
    kClientCommand = 2,
    kClientInterestUpdate = 3,
    kClientSnapshotAcknowledgement = 4,

    kServerLoginAccepted = 64,
    kServerSnapshot = 65,
    kServerDelta = 66,
    kServerNotice = 67,
};

// The outer game envelope is network byte order:
// magic:u32, protocol_version:u16, kind:u8, reserved:u8, payload_length:u32.
// The reserved byte must be zero in the only supported protocol version.
struct GameReplicationMessage {
    uint16_t protocol_version = kCurrentGameReplicationProtocolVersion;
    GameReplicationMessageKind kind = GameReplicationMessageKind::kClientLoginRequest;
    std::vector<std::byte> payload;
};

[[nodiscard]] bool is_known_game_replication_message_kind(
    GameReplicationMessageKind kind) noexcept;
[[nodiscard]] bool is_client_game_replication_message(
    GameReplicationMessageKind kind) noexcept;
[[nodiscard]] bool is_server_game_replication_message(
    GameReplicationMessageKind kind) noexcept;

// All currently declared game messages use the reliable ordered channel.
// A future unreliable message must receive a new kind and explicit validation
// here before it is accepted by the server.
[[nodiscard]] snt::core::Expected<void> validate_game_replication_channel(
    GameReplicationMessageKind kind, snt::network::ReplicationChannel channel);

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_game_replication_message(
    const GameReplicationMessage& message);
[[nodiscard]] snt::core::Expected<GameReplicationMessage> decode_game_replication_message(
    std::span<const std::byte> bytes);

// Credentials are opaque to the codec and must never be logged. The identity
// provider tells the server whether it should validate a Steam ticket or map a
// deliberate local-name account. A client-supplied Steam id is never encoded.
struct GameLoginRequest {
    PlayerIdentityProvider identity_provider = PlayerIdentityProvider::kLocalName;
    std::string display_name;
    std::vector<std::byte> credential;
};

struct GameLoginAccepted {
    PlayerIdentity identity;
};

// Command ids are game-owned and latest-version-only. The first authoritative
// command is task acceptance; player movement and block editing remain absent
// until their server-owned player/AOI data model is defined.
enum class GameClientCommandType : uint16_t {
    kQuestAccept = 1,
};

// The network boundary preserves order and sequence information. Concrete
// command codecs below prevent server gameplay code from parsing opaque bytes.
struct GameClientCommand {
    uint64_t client_sequence = 0;
    uint16_t command_type = 0;
    std::vector<std::byte> payload;
};

struct GameQuestAcceptCommand {
    std::string quest_id;
};

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_login_request(
    const GameLoginRequest& request);
[[nodiscard]] snt::core::Expected<GameLoginRequest> parse_game_login_request(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_login_accepted(
    const GameLoginAccepted& accepted);
[[nodiscard]] snt::core::Expected<GameLoginAccepted> parse_game_login_accepted(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_client_command(
    const GameClientCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> parse_game_client_command(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_quest_accept_command(
    uint64_t client_sequence, const GameQuestAcceptCommand& command);
[[nodiscard]] snt::core::Expected<GameQuestAcceptCommand> parse_game_quest_accept_command(
    const GameClientCommand& command);

}  // namespace snt::game::replication
