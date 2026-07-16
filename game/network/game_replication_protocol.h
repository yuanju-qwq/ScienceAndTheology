// ScienceAndTheology game replication message format.
//
// Ownership: this module owns gameplay protocol versions and message meaning.
// The reusable snt_network transport carries these bytes but must never include
// this header or interpret a game message. The format is deliberately latest-
// version-only: an incompatible game protocol is rejected instead of adapted.

#pragma once

#include "core/expected.h"
#include "ecs/entity_guid.h"
#include "game/player/player_identity.h"
#include "network/replication.h"
#include "voxel/data/voxel_chunk.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snt::game::replication {

inline constexpr uint32_t kGameReplicationMagic = 0x534E5447u;  // "SNTG"
inline constexpr uint16_t kCurrentGameReplicationProtocolVersion = 5;
inline constexpr size_t kGameReplicationHeaderBytes = 12;
inline constexpr size_t kMaxGameReplicationPayloadBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaxGamePlayerNameBytes = kMaxPlayerDisplayNameBytes;
inline constexpr size_t kMaxGamePlayerIdBytes = kMaxPlayerAccountIdBytes;
inline constexpr size_t kMaxGameCredentialBytes = 1024;
inline constexpr size_t kMaxGameCommandPayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxGameQuestIdBytes = 512;
inline constexpr size_t kMaxGameDimensionIdBytes = 128;
inline constexpr size_t kMaxGameChunkSnapshotPayloadBytes = 512u * 1024u;
inline constexpr size_t kMaxGameEntitySnapshotPayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxGameSnapshotChunks = 128;
inline constexpr size_t kMaxGameSnapshotEntities = 4096;
inline constexpr size_t kMaxGameDeltaChunks = 128;
inline constexpr size_t kMaxGameBlockDeltasPerChunk =
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize) *
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize) *
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize);
inline constexpr size_t kMaxGameBlockDeltas = 16384;

// Client and server message ranges are intentionally distinct. New message
// kinds must be added here before a handler accepts them, so an unrecognized
// payload never gains gameplay meaning accidentally.
enum class GameReplicationMessageKind : uint8_t {
    kClientLoginRequest = 1,
    kClientCommand = 2,
    // Movement is a latest-state intent, so it deliberately has an explicit
    // unreliable lane instead of letting reliable interaction traffic backlog
    // client movement behind old packets.
    kClientMovementInput = 3,
    kClientInterestUpdate = 4,
    kClientSnapshotAcknowledgement = 5,

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

// Login, commands, and server replication use the reliable ordered channel.
// Player movement intent is the only current unreliable message and carries a
// sequence number so gameplay can discard stale intent without trusting a
// client position. New unreliable messages must receive their own kind and
// explicit validation here before the server accepts them.
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

// Latest-state movement intent. Axes are canonicalized to -1, 0, or 1; yaw
// and pitch are centidegrees so neither float NaNs nor client coordinates can
// cross the protocol boundary. The server owns integration, collision, and
// the resulting player position.
inline constexpr uint8_t kGamePlayerMovementFlagSprint = 1u << 0u;
inline constexpr uint8_t kGamePlayerMovementFlagJump = 1u << 1u;
inline constexpr uint8_t kGamePlayerMovementKnownFlags =
    kGamePlayerMovementFlagSprint | kGamePlayerMovementFlagJump;

struct GamePlayerMovementInput {
    uint64_t client_sequence = 0;
    int8_t forward_axis = 0;
    int8_t strafe_axis = 0;
    uint8_t flags = 0;
    int16_t yaw_centidegrees = 0;
    int16_t pitch_centidegrees = 0;
};

struct GameQuestAcceptCommand {
    std::string quest_id;
};

// Snapshot and delta payloads deliberately keep world/actor semantics opaque.
// Snapshot is snapshot_id plus chunk/entity blob lists; Delta is a snapshot
// base id, monotonic sequence, block changes, and entity blobs. The game
// provider owns each blob schema, while this codec guarantees a bounded,
// deterministic outer shape that can be budgeted before handing bytes to
// snt_network. A later Sector migration replaces ChunkKey in this latest-only
// protocol rather than adding a coordinate compatibility path.
struct GameChunkSnapshot {
    snt::voxel::ChunkKey chunk;
    std::vector<std::byte> payload;
};

struct GameEntitySnapshot {
    snt::ecs::EntityGuid entity_guid;
    std::vector<std::byte> payload;
};

struct GameSnapshot {
    uint64_t snapshot_id = 0;
    std::vector<GameChunkSnapshot> chunks;
    std::vector<GameEntitySnapshot> entities;
};

struct GameBlockDelta {
    uint16_t local_index = 0;
    snt::voxel::TerrainMaterialId material = 0;
    uint32_t flags = 0;
};

struct GameChunkDelta {
    snt::voxel::ChunkKey chunk;
    std::vector<GameBlockDelta> blocks;
};

struct GameDelta {
    uint64_t base_snapshot_id = 0;
    uint64_t sequence = 0;
    std::vector<GameChunkDelta> chunks;
    std::vector<GameEntitySnapshot> entities;
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

[[nodiscard]] snt::core::Expected<void> validate_game_player_movement_input(
    const GamePlayerMovementInput& input);
[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_player_movement_input(
    const GamePlayerMovementInput& input);
[[nodiscard]] snt::core::Expected<GamePlayerMovementInput> parse_game_player_movement_input(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_quest_accept_command(
    uint64_t client_sequence, const GameQuestAcceptCommand& command);
[[nodiscard]] snt::core::Expected<GameQuestAcceptCommand> parse_game_quest_accept_command(
    const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_snapshot(
    const GameSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameSnapshot> parse_game_snapshot(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_delta(
    const GameDelta& delta);
[[nodiscard]] snt::core::Expected<GameDelta> parse_game_delta(
    const GameReplicationMessage& message);

}  // namespace snt::game::replication
