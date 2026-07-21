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
#include "game/player/player_state.h"
#include "network/replication.h"
#include "voxel/data/voxel_chunk.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace snt::game::replication {

inline constexpr uint32_t kGameReplicationMagic = 0x534E5447u;  // "SNTG"
inline constexpr uint16_t kCurrentGameReplicationProtocolVersion = 17;
inline constexpr size_t kGameReplicationHeaderBytes = 12;
inline constexpr size_t kMaxGameReplicationPayloadBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaxGamePlayerNameBytes = kMaxPlayerDisplayNameBytes;
inline constexpr size_t kMaxGamePlayerIdBytes = kMaxPlayerAccountIdBytes;
inline constexpr size_t kMaxGameCredentialBytes = 1024;
inline constexpr size_t kMaxGameServerPasswordBytes = 256;
inline constexpr size_t kMaxGameCommandPayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxGameQuestIdBytes = 512;
inline constexpr size_t kMaxGameDimensionIdBytes = 128;
inline constexpr size_t kMaxGameItemIdBytes = 256;
inline constexpr size_t kMaxGameResourceTypeBytes = 64;
inline constexpr size_t kMaxGameResourceIdBytes = 256;
inline constexpr size_t kMaxGameResourceVariantBytes = 1024;
inline constexpr size_t kMaxGameInventorySlots = 256;
inline constexpr size_t kMaxGameMachineInputSlots = 64;
inline constexpr int32_t kMaxGameInventoryStackSize = 65536;
// Inventory replication must fit one bounded SNTG value even at maximum slot
// count. Larger custom item data needs a future item-state blob protocol.
inline constexpr size_t kMaxGameInventoryItemInstanceBytes = 1024;
inline constexpr size_t kMaxGameChunkSnapshotPayloadBytes = 512u * 1024u;
inline constexpr size_t kMaxGameEntitySnapshotPayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxGameSnapshotChunks = 128;
inline constexpr size_t kMaxGameSnapshotEntities = 4096;
inline constexpr size_t kMaxGameSnapshotValues = 64;
inline constexpr size_t kMaxGameDeltaChunks = 128;
inline constexpr size_t kMaxGameBlockDeltasPerChunk =
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize) *
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize) *
    static_cast<size_t>(snt::voxel::VoxelChunk::kChunkSize);
inline constexpr size_t kMaxGameBlockDeltas = 16384;
inline constexpr size_t kMaxGameReplicationValuePayloadBytes = 512u * 1024u;

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

// Credentials are opaque Steam authentication evidence and must never be
// logged. server_password is a separate optional server access gate; it is
// never interpreted as identity evidence and must not be logged either. The
// identity provider tells the server whether it should validate a Steam ticket
// or map a deliberate local-name account. A client-supplied Steam id is never
// encoded.
struct GameLoginRequest {
    PlayerIdentityProvider identity_provider = PlayerIdentityProvider::kLocalName;
    std::string display_name;
    std::vector<std::byte> credential;
    std::string server_password;
};

struct GameLoginAccepted {
    PlayerIdentity identity;
};

// Command ids are game-owned and latest-version-only. The host owns final
// shared-world mutation, while block interaction carries client-computed
// presentation/gameplay hints so a co-op session does not spend server time
// replaying raycasts or other anti-cheat checks.
enum class GameClientCommandType : uint16_t {
    kBlockInteraction = 1,
    kQuestClaimReward = 2,
    kInventorySlotTransfer = 3,
    kMachineInputSlotTransfer = 4,
    kCreatureAttack = 5,
    kCreatureCapture = 6,
    kCaptiveCreatureFeed = 7,
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

// A claim contains only the completed quest identifier. The authenticated
// server peer determines the player, and QuestRegistry owns reward validation
// plus any authoritative inventory transaction.
struct GameQuestClaimRewardCommand {
    std::string quest_id;
};

// The client chooses the requested action and target. The host treats
// cover/ignition/structure/tool hints as trusted co-op input, but still owns
// catalog resolution, inventory arithmetic, sidecar integrity, and the final
// terrain mutation. Values not represented here must not acquire gameplay
// meaning from opaque command bytes.
enum class GameBlockInteractionAction : uint8_t {
    kMine = 1,
    kPlace = 2,
    kUse = 3,
    kActivateMachine = 4,
    kCollectMachineOutput = 5,
    // Farming remains a typed block interaction instead of a generic
    // command payload. The authenticated host resolves all item, crop, and
    // sidecar semantics from its current content snapshot.
    kTillFarmland = 6,
    kPlantCrop = 7,
    kFertilizeCrop = 8,
    kHarvestCrop = 9,
};

inline constexpr uint8_t kGameBlockInteractionHintCover = 1u << 0u;
inline constexpr uint8_t kGameBlockInteractionHintIgnition = 1u << 1u;
inline constexpr uint8_t kGameBlockInteractionHintStructure = 1u << 2u;
inline constexpr uint8_t kGameBlockInteractionKnownHints =
    kGameBlockInteractionHintCover |
    kGameBlockInteractionHintIgnition |
    kGameBlockInteractionHintStructure;

// Terrain material ids are uint8_t. The out-of-band value lets a client omit
// an optimistic stale-material guard without overloading air material 0.
inline constexpr uint16_t kGameNoExpectedTerrainMaterial = 256;

struct GameBlockInteractionCommand {
    GameBlockInteractionAction action = GameBlockInteractionAction::kMine;
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    uint16_t expected_material = kGameNoExpectedTerrainMaterial;
    std::string selected_item_id;
    uint8_t client_hints = 0;
};

// A fixed-slot player inventory transfer is conditional on the exact source
// and target values observed by the client. The server never trusts a client
// inventory mutation; it resolves this command against its authenticated
// player state and sends an incremental inventory result back through the
// player-only replication value.
struct GameInventorySlotTransferCommand {
    uint64_t request_id = 0;
    uint64_t expected_inventory_revision = 0;
    uint32_t source_slot = 0;
    uint32_t target_slot = 0;
    int32_t count = 0;
    GamePlayerItemStack expected_source;
    GamePlayerItemStack expected_target;
};

// A production machine keeps its own input storage, so it must not be
// represented as another player's inventory. This command moves one observed
// player slot to or from one observed machine input position. The host checks
// both snapshots and commits both containers on its simulation main thread.
enum class GameMachineInputSlotTransferDirection : uint8_t {
    kPlayerToMachineInput = 1,
    kMachineInputToPlayer = 2,
};

struct GameMachineInputSlotTransferCommand {
    uint64_t request_id = 0;
    uint64_t expected_inventory_revision = 0;
    GameMachineInputSlotTransferDirection direction =
        GameMachineInputSlotTransferDirection::kPlayerToMachineInput;
    std::string dimension_id;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    uint16_t expected_material = kGameNoExpectedTerrainMaterial;
    uint16_t player_slot = 0;
    uint16_t machine_input_slot = 0;
    int32_t count = 0;
    GamePlayerItemStack expected_player_slot;
    GamePlayerItemStack expected_machine_input_slot;
};

// Creature commands never carry client-computed damage, coordinates, pen
// bounds, or inventory mutations. The server resolves all of those from the
// authenticated player, current wildlife state, world terrain, and content.
struct GameCreatureAttackCommand {
    uint64_t creature_entity_id = 0;
};

struct GameCreatureCaptureCommand {
    uint64_t creature_entity_id = 0;
};

struct GameCaptiveCreatureFeedCommand {
    uint64_t creature_entity_id = 0;
    std::string feed_item_id;
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

// Values are authenticated-player presentation state which is neither terrain
// nor an ECS entity. A value kind appears at most once in one snapshot or
// delta, so each consumer has one deterministic latest value boundary.
enum class GameReplicationValueKind : uint8_t {
    kQuestBook = 1,
    kPlayerInventory = 2,
    // A complete observer-visible set of native creature presentation
    // values. It intentionally remains separate from persistent ECS actors:
    // far representatives are render-only, while nearby wild and captive
    // creatures expose the same stable ids for authoritative commands.
    kCreaturePresentation = 3,
};

enum class GameReplicationValueOperation : uint8_t {
    kUpsert = 1,
    kRemove = 2,
};

struct GameReplicationValue {
    GameReplicationValueKind kind = GameReplicationValueKind::kQuestBook;
    GameReplicationValueOperation operation = GameReplicationValueOperation::kUpsert;
    std::vector<std::byte> payload;
};

[[nodiscard]] bool is_known_game_replication_value_kind(
    GameReplicationValueKind kind) noexcept;

struct GameSnapshot {
    uint64_t snapshot_id = 0;
    std::vector<GameChunkSnapshot> chunks;
    std::vector<GameEntitySnapshot> entities;
    std::vector<GameReplicationValue> values;
};

struct GameBlockDelta {
    uint16_t local_index = 0;
    snt::voxel::TerrainMaterialId material = 0;
    uint32_t flags = 0;
    snt::voxel::CellFluidId fluid_type = snt::voxel::kInvalidCellFluidId;
    int16_t fluid_mass = 0;
    int16_t fluid_temperature = 300;
    bool fluid_is_gas = false;
};

struct GameChunkDelta {
    snt::voxel::ChunkKey chunk;
    std::vector<GameBlockDelta> blocks;
};

struct GameDelta {
    uint64_t base_snapshot_id = 0;
    uint64_t sequence = 0;
    // Newly interested chunks use full authoritative payloads. Chunk removal
    // explicitly releases the prior snapshot baseline before a later AOI
    // re-entry receives another full payload; block deltas only target chunks
    // already present in that baseline.
    std::vector<GameChunkSnapshot> chunk_snapshots;
    std::vector<snt::voxel::ChunkKey> removed_chunks;
    std::vector<GameChunkDelta> chunks;
    std::vector<GameEntitySnapshot> entities;
    std::vector<GameReplicationValue> values;
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

[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_quest_claim_reward_command(
    uint64_t client_sequence, const GameQuestClaimRewardCommand& command);
[[nodiscard]] snt::core::Expected<GameQuestClaimRewardCommand>
parse_game_quest_claim_reward_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_block_interaction_command(
    const GameBlockInteractionCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_block_interaction_command(
    uint64_t client_sequence, const GameBlockInteractionCommand& command);
[[nodiscard]] snt::core::Expected<GameBlockInteractionCommand>
parse_game_block_interaction_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_inventory_slot_transfer_command(
    const GameInventorySlotTransferCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_inventory_slot_transfer_command(
    uint64_t client_sequence, const GameInventorySlotTransferCommand& command);
[[nodiscard]] snt::core::Expected<GameInventorySlotTransferCommand>
parse_game_inventory_slot_transfer_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_machine_input_slot_transfer_command(
    const GameMachineInputSlotTransferCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand>
make_game_machine_input_slot_transfer_command(
    uint64_t client_sequence, const GameMachineInputSlotTransferCommand& command);
[[nodiscard]] snt::core::Expected<GameMachineInputSlotTransferCommand>
parse_game_machine_input_slot_transfer_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_creature_attack_command(
    const GameCreatureAttackCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_creature_attack_command(
    uint64_t client_sequence, const GameCreatureAttackCommand& command);
[[nodiscard]] snt::core::Expected<GameCreatureAttackCommand>
parse_game_creature_attack_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_creature_capture_command(
    const GameCreatureCaptureCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_creature_capture_command(
    uint64_t client_sequence, const GameCreatureCaptureCommand& command);
[[nodiscard]] snt::core::Expected<GameCreatureCaptureCommand>
parse_game_creature_capture_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<void> validate_game_captive_creature_feed_command(
    const GameCaptiveCreatureFeedCommand& command);
[[nodiscard]] snt::core::Expected<GameClientCommand> make_game_captive_creature_feed_command(
    uint64_t client_sequence, const GameCaptiveCreatureFeedCommand& command);
[[nodiscard]] snt::core::Expected<GameCaptiveCreatureFeedCommand>
parse_game_captive_creature_feed_command(const GameClientCommand& command);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_snapshot(
    const GameSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameSnapshot> parse_game_snapshot(
    const GameReplicationMessage& message);

[[nodiscard]] snt::core::Expected<GameReplicationMessage> make_game_delta(
    const GameDelta& delta);
[[nodiscard]] snt::core::Expected<GameDelta> parse_game_delta(
    const GameReplicationMessage& message);

}  // namespace snt::game::replication
