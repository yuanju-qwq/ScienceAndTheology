// Game-owned replication envelope and first typed message codecs.

#include "game/network/game_replication_protocol.h"

#include "core/error.h"

#include <bit>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::byte>& bytes, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_i16(std::vector<std::byte>& bytes, int16_t value) {
    append_u16(bytes, std::bit_cast<uint16_t>(value));
}

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] int16_t read_i16(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int16_t>(read_u16(bytes, offset));
}

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] bool valid_fluid_fields(snt::voxel::CellFluidId fluid_type,
                                      int16_t fluid_mass,
                                      bool fluid_is_gas) noexcept {
    if (fluid_mass < 0 || fluid_mass > snt::voxel::kCellFluidCapacity) return false;
    if (fluid_mass == 0) {
        return fluid_type == snt::voxel::kInvalidCellFluidId && !fluid_is_gas;
    }
    return fluid_type != snt::voxel::kInvalidCellFluidId;
}

[[nodiscard]] snt::core::Expected<void> validate_message_shape(
    const GameReplicationMessage& message) {
    if (message.protocol_version != kCurrentGameReplicationProtocolVersion) {
        return protocol_error("Game replication protocol version does not match");
    }
    if (!is_known_game_replication_message_kind(message.kind)) {
        return protocol_error("Unknown game replication message kind");
    }
    if (message.payload.size() > kMaxGameReplicationPayloadBytes) {
        return protocol_error("Game replication payload exceeds the configured limit");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_message_kind(
    const GameReplicationMessage& message, GameReplicationMessageKind expected) {
    if (auto result = validate_message_shape(message); !result) return result.error();
    if (message.kind != expected) {
        return protocol_error("Game replication message kind does not match the typed payload");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, const std::string& value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (require_non_empty && value.empty()) {
        return protocol_error(std::string(field_name) + " must not be empty");
    }
    if (value.size() > maximum || value.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error(std::string(field_name) + " exceeds the protocol limit");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char value_byte : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value_byte)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Game replication ") + field_name + " is truncated");
    }
    const size_t size = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && size == 0) || size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Game replication ") + field_name + " is invalid");
    }

    std::string value;
    value.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += size;
    return value;
}

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> read_byte_vector(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name) {
    if (bytes.size() - offset < sizeof(uint32_t)) {
        return protocol_error(std::string("Game replication ") + field_name + " is truncated");
    }
    const size_t size = read_u32(bytes, offset);
    offset += sizeof(uint32_t);
    if (size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Game replication ") + field_name + " exceeds the protocol limit");
    }

    std::vector<std::byte> value;
    value.insert(value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_chunk_key(
    const snt::voxel::ChunkKey& chunk) {
    if (chunk.dimension_id.empty() ||
        chunk.dimension_id.size() > kMaxGameDimensionIdBytes ||
        chunk.dimension_id.find('\0') != std::string::npos) {
        return protocol_error("Game replication chunk dimension id is invalid");
    }
    return {};
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] snt::core::Expected<void> validate_server_password(std::string_view password) {
    if (password.size() > kMaxGameServerPasswordBytes || has_embedded_nul(password)) {
        return protocol_error("Game login server password is invalid");
    }
    return {};
}

[[nodiscard]] bool is_known_block_interaction_action(
    GameBlockInteractionAction action) noexcept {
    switch (action) {
        case GameBlockInteractionAction::kMine:
        case GameBlockInteractionAction::kPlace:
        case GameBlockInteractionAction::kUse:
        case GameBlockInteractionAction::kActivateMachine:
        case GameBlockInteractionAction::kCollectMachineOutput:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_known_machine_input_slot_transfer_direction(
    GameMachineInputSlotTransferDirection direction) noexcept {
    switch (direction) {
        case GameMachineInputSlotTransferDirection::kPlayerToMachineInput:
        case GameMachineInputSlotTransferDirection::kMachineInputToPlayer:
            return true;
    }
    return false;
}

[[nodiscard]] bool is_empty_inventory_stack(const GamePlayerItemStack& stack) noexcept {
    return stack.item_id.empty() && stack.count == 0 && stack.instance_data.empty();
}

[[nodiscard]] snt::core::Expected<void> validate_inventory_stack(
    const GamePlayerItemStack& stack, bool allow_empty, const char* field_name) {
    if (is_empty_inventory_stack(stack)) {
        if (allow_empty) return {};
        return protocol_error(std::string(field_name) + " must not be empty");
    }
    if (stack.item_id.empty() || stack.item_id.size() > kMaxGameItemIdBytes ||
        stack.instance_data.size() > kMaxGameInventoryItemInstanceBytes ||
        has_embedded_nul(stack.item_id) || has_embedded_nul(stack.instance_data) ||
        stack.count <= 0 || stack.count > kMaxGameInventoryStackSize ||
        (!stack.instance_data.empty() && stack.count != 1)) {
        return protocol_error(std::string(field_name) + " is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_inventory_stack(
    std::vector<std::byte>& bytes, const GamePlayerItemStack& stack, const char* field_name) {
    if (auto result = validate_inventory_stack(stack, true, field_name); !result) {
        return result.error();
    }
    if (is_empty_inventory_stack(stack)) {
        bytes.push_back(std::byte{0});
        return {};
    }
    bytes.push_back(std::byte{1});
    if (auto result = append_short_string(bytes, stack.item_id, kMaxGameItemIdBytes,
                                          "inventory item id", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, stack.count);
    if (auto result = append_short_string(bytes, stack.instance_data,
                                          kMaxGameInventoryItemInstanceBytes,
                                          "inventory item instance data", false);
        !result) {
        return result.error();
    }
    return {};
}

[[nodiscard]] snt::core::Expected<GamePlayerItemStack> read_inventory_stack(
    std::span<const std::byte> bytes, size_t& offset, const char* field_name) {
    if (offset >= bytes.size()) {
        return protocol_error(std::string(field_name) + " is truncated");
    }
    const uint8_t present = std::to_integer<uint8_t>(bytes[offset++]);
    if (present == 0) return GamePlayerItemStack{};
    if (present != 1) return protocol_error(std::string(field_name) + " presence flag is invalid");

    auto item_id = read_short_string(bytes, offset, kMaxGameItemIdBytes,
                                     "inventory item id", true);
    if (!item_id) return item_id.error();
    if (bytes.size() - offset < sizeof(uint32_t)) {
        return protocol_error(std::string(field_name) + " count is truncated");
    }
    const int32_t count = read_i32(bytes, offset);
    offset += sizeof(uint32_t);
    auto instance_data = read_short_string(bytes, offset, kMaxGameInventoryItemInstanceBytes,
                                           "inventory item instance data", false);
    if (!instance_data) return instance_data.error();

    GamePlayerItemStack stack{
        .item_id = std::move(*item_id),
        .count = count,
        .instance_data = std::move(*instance_data),
    };
    if (auto result = validate_inventory_stack(stack, false, field_name); !result) {
        return result.error();
    }
    return stack;
}

[[nodiscard]] snt::core::Expected<void> append_chunk_key(
    std::vector<std::byte>& bytes, const snt::voxel::ChunkKey& chunk) {
    if (auto result = validate_chunk_key(chunk); !result) return result.error();
    if (auto result = append_short_string(bytes, chunk.dimension_id,
                                          kMaxGameDimensionIdBytes,
                                          "chunk dimension id", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, chunk.chunk_x);
    append_i32(bytes, chunk.chunk_y);
    append_i32(bytes, chunk.chunk_z);
    return {};
}

[[nodiscard]] snt::core::Expected<snt::voxel::ChunkKey> read_chunk_key(
    std::span<const std::byte> bytes, size_t& offset) {
    auto dimension = read_short_string(bytes, offset, kMaxGameDimensionIdBytes,
                                       "chunk dimension id", true);
    if (!dimension) return dimension.error();
    constexpr size_t kChunkCoordinateBytes = sizeof(uint32_t) * 3;
    if (bytes.size() - offset < kChunkCoordinateBytes) {
        return protocol_error("Game replication chunk coordinates are truncated");
    }
    snt::voxel::ChunkKey chunk{
        std::move(*dimension),
        read_i32(bytes, offset),
        read_i32(bytes, offset + sizeof(uint32_t)),
        read_i32(bytes, offset + sizeof(uint32_t) * 2),
    };
    offset += kChunkCoordinateBytes;
    if (auto result = validate_chunk_key(chunk); !result) return result.error();
    return chunk;
}

[[nodiscard]] snt::core::Expected<void> validate_entity_snapshot(
    const GameEntitySnapshot& entity) {
    if (!entity.entity_guid.valid() || entity.payload.empty() ||
        entity.payload.size() > kMaxGameEntitySnapshotPayloadBytes) {
        return protocol_error("Game replication entity snapshot is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_replication_value(
    const GameReplicationValue& value, bool is_snapshot) {
    if (!is_known_game_replication_value_kind(value.kind)) {
        return protocol_error("Game replication value kind is unknown");
    }
    if (value.operation != GameReplicationValueOperation::kUpsert &&
        value.operation != GameReplicationValueOperation::kRemove) {
        return protocol_error("Game replication value operation is invalid");
    }
    if (is_snapshot && value.operation != GameReplicationValueOperation::kUpsert) {
        return protocol_error("Game replication snapshot value must be an upsert");
    }
    if (value.operation == GameReplicationValueOperation::kRemove) {
        if (!value.payload.empty()) {
            return protocol_error("Game replication removed value carries a payload");
        }
        return {};
    }
    if (value.payload.empty() || value.payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Game replication value payload is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_replication_values(
    const std::vector<GameReplicationValue>& values, bool is_snapshot) {
    if (values.size() > kMaxGameSnapshotValues) {
        return protocol_error("Game replication has too many values");
    }
    std::unordered_set<uint8_t> kinds;
    kinds.reserve(values.size());
    for (const GameReplicationValue& value : values) {
        if (auto result = validate_replication_value(value, is_snapshot); !result) {
            return result.error();
        }
        if (!kinds.insert(static_cast<uint8_t>(value.kind)).second) {
            return protocol_error("Game replication contains duplicate value kinds");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_snapshot(
    const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0 ||
        snapshot.chunks.size() > kMaxGameSnapshotChunks ||
        snapshot.entities.size() > kMaxGameSnapshotEntities ||
        snapshot.values.size() > kMaxGameSnapshotValues) {
        return protocol_error("Game replication snapshot is invalid");
    }

    std::unordered_set<snt::voxel::ChunkKey> chunks;
    chunks.reserve(snapshot.chunks.size());
    for (const GameChunkSnapshot& chunk : snapshot.chunks) {
        if (auto result = validate_chunk_key(chunk.chunk); !result) return result.error();
        if (chunk.payload.empty() ||
            chunk.payload.size() > kMaxGameChunkSnapshotPayloadBytes) {
            return protocol_error("Game replication chunk snapshot payload is invalid");
        }
        if (!chunks.insert(chunk.chunk).second) {
            return protocol_error("Game replication snapshot contains a duplicate chunk");
        }
    }

    std::unordered_set<snt::ecs::EntityGuid> entities;
    entities.reserve(snapshot.entities.size());
    for (const GameEntitySnapshot& entity : snapshot.entities) {
        if (auto result = validate_entity_snapshot(entity); !result) return result.error();
        if (!entities.insert(entity.entity_guid).second) {
            return protocol_error("Game replication snapshot contains a duplicate entity");
        }
    }
    if (auto result = validate_replication_values(snapshot.values, true); !result) {
        return result.error();
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_delta(const GameDelta& delta) {
    if (delta.base_snapshot_id == 0 || delta.sequence == 0 ||
        delta.chunk_snapshots.size() > kMaxGameDeltaChunks ||
        delta.removed_chunks.size() > kMaxGameDeltaChunks ||
        delta.chunks.size() > kMaxGameDeltaChunks ||
        delta.entities.size() > kMaxGameSnapshotEntities ||
        delta.values.size() > kMaxGameSnapshotValues) {
        return protocol_error("Game replication delta is invalid");
    }

    size_t block_count = 0;
    std::unordered_set<snt::voxel::ChunkKey> chunks;
    chunks.reserve(delta.chunk_snapshots.size() + delta.removed_chunks.size() +
                   delta.chunks.size());
    for (const GameChunkSnapshot& chunk : delta.chunk_snapshots) {
        if (auto result = validate_chunk_key(chunk.chunk); !result) return result.error();
        if (chunk.payload.empty() ||
            chunk.payload.size() > kMaxGameChunkSnapshotPayloadBytes) {
            return protocol_error("Game replication delta chunk snapshot payload is invalid");
        }
        if (!chunks.insert(chunk.chunk).second) {
            return protocol_error("Game replication delta contains a duplicate chunk");
        }
    }
    for (const snt::voxel::ChunkKey& chunk : delta.removed_chunks) {
        if (auto result = validate_chunk_key(chunk); !result) return result.error();
        if (!chunks.insert(chunk).second) {
            return protocol_error("Game replication delta contains a duplicate chunk");
        }
    }
    for (const GameChunkDelta& chunk : delta.chunks) {
        if (auto result = validate_chunk_key(chunk.chunk); !result) return result.error();
        if (chunk.blocks.empty() ||
            chunk.blocks.size() > kMaxGameBlockDeltasPerChunk ||
            block_count > kMaxGameBlockDeltas - chunk.blocks.size()) {
            return protocol_error("Game replication chunk delta is invalid");
        }
        block_count += chunk.blocks.size();
        if (!chunks.insert(chunk.chunk).second) {
            return protocol_error("Game replication delta contains a duplicate chunk");
        }

        std::unordered_set<uint16_t> local_indices;
        local_indices.reserve(chunk.blocks.size());
        for (const GameBlockDelta& block : chunk.blocks) {
            if (block.local_index >= kMaxGameBlockDeltasPerChunk ||
                !local_indices.insert(block.local_index).second ||
                !valid_fluid_fields(block.fluid_type, block.fluid_mass,
                                    block.fluid_is_gas)) {
                return protocol_error("Game replication chunk delta has invalid local block indices");
            }
        }
    }

    std::unordered_set<snt::ecs::EntityGuid> entities;
    entities.reserve(delta.entities.size());
    for (const GameEntitySnapshot& entity : delta.entities) {
        if (auto result = validate_entity_snapshot(entity); !result) return result.error();
        if (!entities.insert(entity.entity_guid).second) {
            return protocol_error("Game replication delta contains a duplicate entity");
        }
    }
    if (auto result = validate_replication_values(delta.values, false); !result) {
        return result.error();
    }
    return {};
}

}  // namespace

bool is_known_game_replication_message_kind(GameReplicationMessageKind kind) noexcept {
    switch (kind) {
        case GameReplicationMessageKind::kClientLoginRequest:
        case GameReplicationMessageKind::kClientCommand:
        case GameReplicationMessageKind::kClientMovementInput:
        case GameReplicationMessageKind::kClientInterestUpdate:
        case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
        case GameReplicationMessageKind::kServerLoginAccepted:
        case GameReplicationMessageKind::kServerSnapshot:
        case GameReplicationMessageKind::kServerDelta:
        case GameReplicationMessageKind::kServerNotice:
            return true;
    }
    return false;
}

bool is_known_game_replication_value_kind(GameReplicationValueKind kind) noexcept {
    switch (kind) {
        case GameReplicationValueKind::kQuestBook:
        case GameReplicationValueKind::kPlayerInventory:
            return true;
    }
    return false;
}

bool is_client_game_replication_message(GameReplicationMessageKind kind) noexcept {
    switch (kind) {
        case GameReplicationMessageKind::kClientLoginRequest:
        case GameReplicationMessageKind::kClientCommand:
        case GameReplicationMessageKind::kClientMovementInput:
        case GameReplicationMessageKind::kClientInterestUpdate:
        case GameReplicationMessageKind::kClientSnapshotAcknowledgement:
            return true;
        case GameReplicationMessageKind::kServerLoginAccepted:
        case GameReplicationMessageKind::kServerSnapshot:
        case GameReplicationMessageKind::kServerDelta:
        case GameReplicationMessageKind::kServerNotice:
            return false;
    }
    return false;
}

bool is_server_game_replication_message(GameReplicationMessageKind kind) noexcept {
    return is_known_game_replication_message_kind(kind) &&
           !is_client_game_replication_message(kind);
}

snt::core::Expected<void> validate_game_replication_channel(
    GameReplicationMessageKind kind, snt::network::ReplicationChannel channel) {
    if (!is_known_game_replication_message_kind(kind)) {
        return protocol_error("Unknown game replication message kind");
    }
    const snt::network::ReplicationChannel expected_channel =
        kind == GameReplicationMessageKind::kClientMovementInput
            ? snt::network::ReplicationChannel::Unreliable
            : snt::network::ReplicationChannel::Reliable;
    if (channel != expected_channel) {
        return protocol_error("Game replication message was sent on the wrong channel");
    }
    return {};
}

snt::core::Expected<std::vector<std::byte>> encode_game_replication_message(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_shape(message); !result) return result.error();
    if (message.payload.size() > std::numeric_limits<uint32_t>::max()) {
        return protocol_error("Game replication payload cannot be represented in 32 bits");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(kGameReplicationHeaderBytes + message.payload.size());
    append_u32(bytes, kGameReplicationMagic);
    append_u16(bytes, message.protocol_version);
    bytes.push_back(static_cast<std::byte>(message.kind));
    bytes.push_back(std::byte{0});
    append_u32(bytes, static_cast<uint32_t>(message.payload.size()));
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

snt::core::Expected<GameReplicationMessage> decode_game_replication_message(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kGameReplicationHeaderBytes) {
        return protocol_error("Game replication message is incomplete");
    }
    if (read_u32(bytes, 0) != kGameReplicationMagic) {
        return protocol_error("Game replication magic does not match");
    }
    if (read_u16(bytes, 4) != kCurrentGameReplicationProtocolVersion) {
        return protocol_error("Game replication protocol version does not match");
    }
    if (std::to_integer<uint8_t>(bytes[7]) != 0) {
        return protocol_error("Game replication reserved header bits are non-zero");
    }

    const size_t payload_size = read_u32(bytes, 8);
    if (payload_size > kMaxGameReplicationPayloadBytes ||
        payload_size > std::numeric_limits<size_t>::max() - kGameReplicationHeaderBytes) {
        return protocol_error("Game replication payload exceeds the protocol limit");
    }
    if (kGameReplicationHeaderBytes + payload_size != bytes.size()) {
        return protocol_error("Game replication message has an invalid payload length");
    }

    GameReplicationMessage message;
    message.protocol_version = read_u16(bytes, 4);
    message.kind = static_cast<GameReplicationMessageKind>(std::to_integer<uint8_t>(bytes[6]));
    message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kGameReplicationHeaderBytes),
                           bytes.end());
    if (auto result = validate_message_shape(message); !result) return result.error();
    return message;
}

snt::core::Expected<GameReplicationMessage> make_game_login_request(
    const GameLoginRequest& request) {
    if (request.credential.size() > kMaxGameCredentialBytes) {
        return protocol_error("Game login credential exceeds the protocol limit");
    }
    if (!is_valid_player_identity_provider(request.identity_provider)) {
        return protocol_error("Game login identity provider is invalid");
    }
    if (auto result = validate_player_display_name(request.display_name); !result) {
        return protocol_error("Game login display name is invalid");
    }
    if (request.identity_provider == PlayerIdentityProvider::kLocalName &&
        !request.credential.empty()) {
        return protocol_error("Local-name game login must not send credentials");
    }
    if (request.identity_provider == PlayerIdentityProvider::kSteam && request.credential.empty()) {
        return protocol_error("Steam game login requires an authentication credential");
    }
    if (auto result = validate_server_password(request.server_password); !result) {
        return result.error();
    }

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kClientLoginRequest;
    message.payload.push_back(static_cast<std::byte>(request.identity_provider));
    if (auto result = append_short_string(message.payload, request.display_name,
                                          kMaxGamePlayerNameBytes, "login display name", true);
        !result) {
        return result.error();
    }
    append_u32(message.payload, static_cast<uint32_t>(request.credential.size()));
    message.payload.insert(message.payload.end(), request.credential.begin(), request.credential.end());
    if (auto result = append_short_string(message.payload, request.server_password,
                                          kMaxGameServerPasswordBytes, "server password", false);
        !result) {
        return result.error();
    }
    return message;
}

snt::core::Expected<GameLoginRequest> parse_game_login_request(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kClientLoginRequest);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    if (bytes.empty()) return protocol_error("Game login request is incomplete");
    size_t offset = 0;
    const auto identity_provider = static_cast<PlayerIdentityProvider>(
        std::to_integer<uint8_t>(bytes[offset++]));
    if (!is_valid_player_identity_provider(identity_provider)) {
        return protocol_error("Game login identity provider is invalid");
    }
    auto display_name = read_short_string(bytes, offset, kMaxGamePlayerNameBytes,
                                          "login display name", true);
    if (!display_name) return display_name.error();
    if (auto result = validate_player_display_name(*display_name); !result) {
        return protocol_error("Game login display name is invalid");
    }
    auto credential = read_byte_vector(bytes, offset, kMaxGameCredentialBytes, "login credential");
    if (!credential) return credential.error();
    if (identity_provider == PlayerIdentityProvider::kLocalName && !credential->empty()) {
        return protocol_error("Local-name game login must not send credentials");
    }
    if (identity_provider == PlayerIdentityProvider::kSteam && credential->empty()) {
        return protocol_error("Steam game login requires an authentication credential");
    }
    auto server_password = read_short_string(bytes, offset, kMaxGameServerPasswordBytes,
                                             "server password", false);
    if (!server_password) return server_password.error();
    if (auto result = validate_server_password(*server_password); !result) return result.error();
    if (offset != bytes.size()) return protocol_error("Game login request has trailing bytes");

    return GameLoginRequest{.identity_provider = identity_provider,
                            .display_name = std::move(*display_name),
                            .credential = std::move(*credential),
                            .server_password = std::move(*server_password)};
}

snt::core::Expected<GameReplicationMessage> make_game_login_accepted(
    const GameLoginAccepted& accepted) {
    if (auto result = validate_player_identity(accepted.identity); !result) {
        return protocol_error("Accepted game player identity is invalid");
    }
    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kServerLoginAccepted;
    message.payload.push_back(static_cast<std::byte>(accepted.identity.provider));
    if (auto result = append_short_string(message.payload, accepted.identity.account_id,
                                          kMaxGamePlayerIdBytes, "accepted player id", true);
        !result) {
        return result.error();
    }
    if (auto result = append_short_string(message.payload, accepted.identity.display_name,
                                          kMaxGamePlayerNameBytes, "accepted player display name", true);
        !result) {
        return result.error();
    }
    return message;
}

snt::core::Expected<GameLoginAccepted> parse_game_login_accepted(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kServerLoginAccepted);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    if (bytes.empty()) return protocol_error("Game login accepted payload is incomplete");
    size_t offset = 0;
    const auto identity_provider = static_cast<PlayerIdentityProvider>(
        std::to_integer<uint8_t>(bytes[offset++]));
    if (!is_valid_player_identity_provider(identity_provider)) {
        return protocol_error("Accepted game player identity provider is invalid");
    }
    auto account_id = read_short_string(bytes, offset, kMaxGamePlayerIdBytes,
                                        "accepted player id", true);
    if (!account_id) return account_id.error();
    auto display_name = read_short_string(bytes, offset, kMaxGamePlayerNameBytes,
                                          "accepted player display name", true);
    if (!display_name) return display_name.error();
    if (offset != bytes.size()) return protocol_error("Game login accepted has trailing bytes");

    PlayerIdentity identity{
        .provider = identity_provider,
        .account_id = std::move(*account_id),
        .display_name = std::move(*display_name),
    };
    if (auto result = validate_player_identity(identity); !result) {
        return protocol_error("Accepted game player identity is invalid");
    }
    return GameLoginAccepted{.identity = std::move(identity)};
}

snt::core::Expected<GameReplicationMessage> make_game_client_command(
    const GameClientCommand& command) {
    if (command.client_sequence == 0 || command.command_type == 0) {
        return protocol_error("Game client command sequence and type must be non-zero");
    }
    if (command.payload.size() > kMaxGameCommandPayloadBytes) {
        return protocol_error("Game client command payload exceeds the protocol limit");
    }

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kClientCommand;
    append_u64(message.payload, command.client_sequence);
    append_u16(message.payload, command.command_type);
    append_u32(message.payload, static_cast<uint32_t>(command.payload.size()));
    message.payload.insert(message.payload.end(), command.payload.begin(), command.payload.end());
    return message;
}

snt::core::Expected<GameClientCommand> parse_game_client_command(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kClientCommand);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    constexpr size_t kCommandPrefixBytes = sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint32_t);
    if (bytes.size() < kCommandPrefixBytes) {
        return protocol_error("Game client command is incomplete");
    }

    size_t offset = 0;
    GameClientCommand command;
    command.client_sequence = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    command.command_type = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (command.client_sequence == 0 || command.command_type == 0) {
        return protocol_error("Game client command sequence and type must be non-zero");
    }
    auto payload = read_byte_vector(bytes, offset, kMaxGameCommandPayloadBytes,
                                    "client command payload");
    if (!payload) return payload.error();
    if (offset != bytes.size()) return protocol_error("Game client command has trailing bytes");
    command.payload = std::move(*payload);
    return command;
}

snt::core::Expected<void> validate_game_player_movement_input(
    const GamePlayerMovementInput& input) {
    if (input.client_sequence == 0) {
        return protocol_error("Game player movement input sequence must be non-zero");
    }
    if (input.forward_axis < -1 || input.forward_axis > 1 ||
        input.strafe_axis < -1 || input.strafe_axis > 1) {
        return protocol_error("Game player movement input axis is invalid");
    }
    if ((input.flags & ~kGamePlayerMovementKnownFlags) != 0) {
        return protocol_error("Game player movement input flags are invalid");
    }
    if (input.yaw_centidegrees < -18000 || input.yaw_centidegrees >= 18000 ||
        input.pitch_centidegrees < -8900 || input.pitch_centidegrees > 8900) {
        return protocol_error("Game player movement input look angles are invalid");
    }
    return {};
}

snt::core::Expected<GameReplicationMessage> make_game_player_movement_input(
    const GamePlayerMovementInput& input) {
    if (auto result = validate_game_player_movement_input(input); !result) return result.error();

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kClientMovementInput;
    message.payload.reserve(16);
    append_u64(message.payload, input.client_sequence);
    message.payload.push_back(static_cast<std::byte>(std::bit_cast<uint8_t>(input.forward_axis)));
    message.payload.push_back(static_cast<std::byte>(std::bit_cast<uint8_t>(input.strafe_axis)));
    message.payload.push_back(static_cast<std::byte>(input.flags));
    message.payload.push_back(std::byte{0});
    append_i16(message.payload, input.yaw_centidegrees);
    append_i16(message.payload, input.pitch_centidegrees);
    return message;
}

snt::core::Expected<GamePlayerMovementInput> parse_game_player_movement_input(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kClientMovementInput);
        !result) {
        return result.error();
    }
    constexpr size_t kMovementInputBytes = 16;
    if (message.payload.size() != kMovementInputBytes ||
        std::to_integer<uint8_t>(message.payload[11]) != 0) {
        return protocol_error("Game player movement input payload is invalid");
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    GamePlayerMovementInput input{
        .client_sequence = read_u64(bytes, 0),
        .forward_axis = std::bit_cast<int8_t>(std::to_integer<uint8_t>(bytes[8])),
        .strafe_axis = std::bit_cast<int8_t>(std::to_integer<uint8_t>(bytes[9])),
        .flags = std::to_integer<uint8_t>(bytes[10]),
        .yaw_centidegrees = read_i16(bytes, 12),
        .pitch_centidegrees = read_i16(bytes, 14),
    };
    if (auto result = validate_game_player_movement_input(input); !result) return result.error();
    return input;
}

snt::core::Expected<GameClientCommand> make_game_quest_claim_reward_command(
    uint64_t client_sequence, const GameQuestClaimRewardCommand& command) {
    GameClientCommand encoded;
    encoded.client_sequence = client_sequence;
    encoded.command_type = static_cast<uint16_t>(GameClientCommandType::kQuestClaimReward);
    if (auto result = append_short_string(encoded.payload, command.quest_id,
                                          kMaxGameQuestIdBytes, "quest reward claim id", true);
        !result) {
        return result.error();
    }
    return encoded;
}

snt::core::Expected<GameQuestClaimRewardCommand> parse_game_quest_claim_reward_command(
    const GameClientCommand& command) {
    if (command.command_type != static_cast<uint16_t>(GameClientCommandType::kQuestClaimReward)) {
        return protocol_error("Game client command type is not QuestClaimReward");
    }

    const std::span<const std::byte> bytes(command.payload.data(), command.payload.size());
    size_t offset = 0;
    auto quest_id = read_short_string(bytes, offset, kMaxGameQuestIdBytes,
                                      "quest reward claim id", true);
    if (!quest_id) return quest_id.error();
    if (offset != bytes.size()) return protocol_error("Quest reward claim command has trailing bytes");
    return GameQuestClaimRewardCommand{.quest_id = std::move(*quest_id)};
}

snt::core::Expected<void> validate_game_block_interaction_command(
    const GameBlockInteractionCommand& command) {
    if (!is_known_block_interaction_action(command.action)) {
        return protocol_error("Game block interaction action is invalid");
    }
    if (command.dimension_id.empty() ||
        command.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(command.dimension_id)) {
        return protocol_error("Game block interaction dimension id is invalid");
    }
    if (command.expected_material > kGameNoExpectedTerrainMaterial) {
        return protocol_error("Game block interaction expected material is invalid");
    }
    if (command.selected_item_id.size() > kMaxGameItemIdBytes ||
        has_embedded_nul(command.selected_item_id)) {
        return protocol_error("Game block interaction selected item id is invalid");
    }
    if ((command.client_hints & ~kGameBlockInteractionKnownHints) != 0) {
        return protocol_error("Game block interaction hints are invalid");
    }
    if (command.action == GameBlockInteractionAction::kPlace &&
        command.selected_item_id.empty()) {
        return protocol_error("Game block placement requires a selected item id");
    }
    if (command.action != GameBlockInteractionAction::kActivateMachine &&
        command.client_hints != 0) {
        return protocol_error("Game block interaction hints are only valid for machine activation");
    }
    return {};
}

snt::core::Expected<GameClientCommand> make_game_block_interaction_command(
    uint64_t client_sequence, const GameBlockInteractionCommand& command) {
    if (client_sequence == 0) {
        return protocol_error("Game block interaction command sequence must be non-zero");
    }
    if (auto result = validate_game_block_interaction_command(command); !result) {
        return result.error();
    }

    GameClientCommand encoded;
    encoded.client_sequence = client_sequence;
    encoded.command_type = static_cast<uint16_t>(GameClientCommandType::kBlockInteraction);
    encoded.payload.reserve(2 + sizeof(uint16_t) + sizeof(uint32_t) * 3 +
                            command.dimension_id.size() + command.selected_item_id.size() + 4);
    encoded.payload.push_back(static_cast<std::byte>(command.action));
    encoded.payload.push_back(static_cast<std::byte>(command.client_hints));
    append_u16(encoded.payload, command.expected_material);
    if (auto result = append_short_string(encoded.payload, command.dimension_id,
                                          kMaxGameDimensionIdBytes,
                                          "block interaction dimension id", true);
        !result) {
        return result.error();
    }
    append_i32(encoded.payload, command.block_x);
    append_i32(encoded.payload, command.block_y);
    append_i32(encoded.payload, command.block_z);
    if (auto result = append_short_string(encoded.payload, command.selected_item_id,
                                          kMaxGameItemIdBytes,
                                          "block interaction selected item id", false);
        !result) {
        return result.error();
    }
    return encoded;
}

snt::core::Expected<GameBlockInteractionCommand> parse_game_block_interaction_command(
    const GameClientCommand& command) {
    if (command.command_type != static_cast<uint16_t>(GameClientCommandType::kBlockInteraction)) {
        return protocol_error("Game client command type is not BlockInteraction");
    }

    const std::span<const std::byte> bytes(command.payload.data(), command.payload.size());
    constexpr size_t kPrefixBytes = sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t);
    constexpr size_t kCoordinatesBytes = sizeof(uint32_t) * 3;
    if (bytes.size() < kPrefixBytes) {
        return protocol_error("Game block interaction command is incomplete");
    }
    size_t offset = 0;
    GameBlockInteractionCommand decoded;
    decoded.action = static_cast<GameBlockInteractionAction>(
        std::to_integer<uint8_t>(bytes[offset++]));
    decoded.client_hints = std::to_integer<uint8_t>(bytes[offset++]);
    decoded.expected_material = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    auto dimension = read_short_string(bytes, offset, kMaxGameDimensionIdBytes,
                                       "block interaction dimension id", true);
    if (!dimension) return dimension.error();
    if (bytes.size() - offset < kCoordinatesBytes) {
        return protocol_error("Game block interaction coordinates are truncated");
    }
    decoded.dimension_id = std::move(*dimension);
    decoded.block_x = read_i32(bytes, offset);
    decoded.block_y = read_i32(bytes, offset + sizeof(uint32_t));
    decoded.block_z = read_i32(bytes, offset + sizeof(uint32_t) * 2);
    offset += kCoordinatesBytes;
    auto selected_item = read_short_string(bytes, offset, kMaxGameItemIdBytes,
                                           "block interaction selected item id", false);
    if (!selected_item) return selected_item.error();
    decoded.selected_item_id = std::move(*selected_item);
    if (offset != bytes.size()) {
        return protocol_error("Game block interaction command has trailing bytes");
    }
    if (auto result = validate_game_block_interaction_command(decoded); !result) {
        return result.error();
    }
    return decoded;
}

snt::core::Expected<void> validate_game_inventory_slot_transfer_command(
    const GameInventorySlotTransferCommand& command) {
    if (command.request_id == 0 || command.expected_inventory_revision == 0 ||
        command.source_slot >= kMaxGameInventorySlots || command.target_slot >= kMaxGameInventorySlots ||
        command.source_slot == command.target_slot || command.count <= 0) {
        return protocol_error("Game inventory slot transfer header is invalid");
    }
    if (auto result = validate_inventory_stack(command.expected_source, false,
                                               "Game inventory slot transfer expected source");
        !result) {
        return result.error();
    }
    if (auto result = validate_inventory_stack(command.expected_target, true,
                                               "Game inventory slot transfer expected target");
        !result) {
        return result.error();
    }
    if (command.count > command.expected_source.count) {
        return protocol_error("Game inventory slot transfer count exceeds the expected source");
    }
    return {};
}

snt::core::Expected<GameClientCommand> make_game_inventory_slot_transfer_command(
    uint64_t client_sequence, const GameInventorySlotTransferCommand& command) {
    if (client_sequence == 0) {
        return protocol_error("Game inventory slot transfer command sequence must be non-zero");
    }
    if (auto result = validate_game_inventory_slot_transfer_command(command); !result) {
        return result.error();
    }

    GameClientCommand encoded;
    encoded.client_sequence = client_sequence;
    encoded.command_type = static_cast<uint16_t>(GameClientCommandType::kInventorySlotTransfer);
    encoded.payload.reserve(sizeof(uint64_t) * 2 + sizeof(uint16_t) * 2 + sizeof(uint32_t) +
                            command.expected_source.item_id.size() +
                            command.expected_source.instance_data.size() +
                            command.expected_target.item_id.size() +
                            command.expected_target.instance_data.size() + 16);
    append_u64(encoded.payload, command.request_id);
    append_u64(encoded.payload, command.expected_inventory_revision);
    append_u16(encoded.payload, static_cast<uint16_t>(command.source_slot));
    append_u16(encoded.payload, static_cast<uint16_t>(command.target_slot));
    append_i32(encoded.payload, command.count);
    if (auto result = append_inventory_stack(encoded.payload, command.expected_source,
                                             "Game inventory slot transfer expected source");
        !result) {
        return result.error();
    }
    if (auto result = append_inventory_stack(encoded.payload, command.expected_target,
                                             "Game inventory slot transfer expected target");
        !result) {
        return result.error();
    }
    return encoded;
}

snt::core::Expected<GameInventorySlotTransferCommand>
parse_game_inventory_slot_transfer_command(const GameClientCommand& command) {
    if (command.command_type != static_cast<uint16_t>(GameClientCommandType::kInventorySlotTransfer)) {
        return protocol_error("Game client command type is not InventorySlotTransfer");
    }

    const std::span<const std::byte> bytes(command.payload.data(), command.payload.size());
    constexpr size_t kHeaderBytes = sizeof(uint64_t) * 2 + sizeof(uint16_t) * 2 + sizeof(uint32_t);
    if (bytes.size() < kHeaderBytes) {
        return protocol_error("Game inventory slot transfer command is incomplete");
    }
    size_t offset = 0;
    GameInventorySlotTransferCommand decoded{
        .request_id = read_u64(bytes, offset),
        .expected_inventory_revision = read_u64(bytes, offset + sizeof(uint64_t)),
        .source_slot = read_u16(bytes, offset + sizeof(uint64_t) * 2),
        .target_slot = read_u16(bytes, offset + sizeof(uint64_t) * 2 + sizeof(uint16_t)),
        .count = read_i32(bytes, offset + sizeof(uint64_t) * 2 + sizeof(uint16_t) * 2),
    };
    offset += kHeaderBytes;
    auto expected_source = read_inventory_stack(
        bytes, offset, "Game inventory slot transfer expected source");
    if (!expected_source) return expected_source.error();
    auto expected_target = read_inventory_stack(
        bytes, offset, "Game inventory slot transfer expected target");
    if (!expected_target) return expected_target.error();
    if (offset != bytes.size()) {
        return protocol_error("Game inventory slot transfer command has trailing bytes");
    }
    decoded.expected_source = std::move(*expected_source);
    decoded.expected_target = std::move(*expected_target);
    if (auto result = validate_game_inventory_slot_transfer_command(decoded); !result) {
        return result.error();
    }
    return decoded;
}

snt::core::Expected<void> validate_game_machine_input_slot_transfer_command(
    const GameMachineInputSlotTransferCommand& command) {
    if (command.request_id == 0 || command.expected_inventory_revision == 0 ||
        !is_known_machine_input_slot_transfer_direction(command.direction) ||
        command.dimension_id.empty() || command.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(command.dimension_id) ||
        command.expected_material > kGameNoExpectedTerrainMaterial ||
        command.player_slot >= kMaxGameInventorySlots ||
        command.machine_input_slot >= kMaxGameMachineInputSlots || command.count <= 0) {
        return protocol_error("Game machine input slot transfer header is invalid");
    }
    if (auto result = validate_inventory_stack(command.expected_player_slot,
                                               command.direction ==
                                                   GameMachineInputSlotTransferDirection::
                                                       kMachineInputToPlayer,
                                               "Game machine input slot transfer expected player slot");
        !result) {
        return result.error();
    }
    if (auto result = validate_inventory_stack(
            command.expected_machine_input_slot,
            command.direction ==
                GameMachineInputSlotTransferDirection::kPlayerToMachineInput,
            "Game machine input slot transfer expected machine input slot");
        !result) {
        return result.error();
    }
    if (!command.expected_machine_input_slot.instance_data.empty()) {
        return protocol_error("Game machine input slot transfer cannot carry machine instance data");
    }
    const int32_t source_count = command.direction ==
            GameMachineInputSlotTransferDirection::kPlayerToMachineInput
        ? command.expected_player_slot.count
        : command.expected_machine_input_slot.count;
    if (command.count > source_count) {
        return protocol_error("Game machine input slot transfer count exceeds the expected source");
    }
    return {};
}

snt::core::Expected<GameClientCommand> make_game_machine_input_slot_transfer_command(
    uint64_t client_sequence, const GameMachineInputSlotTransferCommand& command) {
    if (client_sequence == 0) {
        return protocol_error("Game machine input slot transfer command sequence must be non-zero");
    }
    if (auto result = validate_game_machine_input_slot_transfer_command(command); !result) {
        return result.error();
    }

    GameClientCommand encoded;
    encoded.client_sequence = client_sequence;
    encoded.command_type = static_cast<uint16_t>(GameClientCommandType::kMachineInputSlotTransfer);
    encoded.payload.reserve(sizeof(uint64_t) * 2 + sizeof(uint8_t) +
                            command.dimension_id.size() + sizeof(uint16_t) * 4 +
                            sizeof(uint32_t) * 4 +
                            command.expected_player_slot.item_id.size() +
                            command.expected_player_slot.instance_data.size() +
                            command.expected_machine_input_slot.item_id.size() + 20);
    append_u64(encoded.payload, command.request_id);
    append_u64(encoded.payload, command.expected_inventory_revision);
    encoded.payload.push_back(static_cast<std::byte>(command.direction));
    if (auto result = append_short_string(encoded.payload, command.dimension_id,
                                          kMaxGameDimensionIdBytes,
                                          "machine input slot transfer dimension id", true);
        !result) {
        return result.error();
    }
    append_i32(encoded.payload, command.root_x);
    append_i32(encoded.payload, command.root_y);
    append_i32(encoded.payload, command.root_z);
    append_u16(encoded.payload, command.expected_material);
    append_u16(encoded.payload, command.player_slot);
    append_u16(encoded.payload, command.machine_input_slot);
    append_i32(encoded.payload, command.count);
    if (auto result = append_inventory_stack(
            encoded.payload, command.expected_player_slot,
            "Game machine input slot transfer expected player slot");
        !result) {
        return result.error();
    }
    if (auto result = append_inventory_stack(
            encoded.payload, command.expected_machine_input_slot,
            "Game machine input slot transfer expected machine input slot");
        !result) {
        return result.error();
    }
    return encoded;
}

snt::core::Expected<GameMachineInputSlotTransferCommand>
parse_game_machine_input_slot_transfer_command(const GameClientCommand& command) {
    if (command.command_type !=
        static_cast<uint16_t>(GameClientCommandType::kMachineInputSlotTransfer)) {
        return protocol_error("Game client command type is not MachineInputSlotTransfer");
    }

    const std::span<const std::byte> bytes(command.payload.data(), command.payload.size());
    constexpr size_t kPrefixBytes = sizeof(uint64_t) * 2 + sizeof(uint8_t);
    if (bytes.size() < kPrefixBytes) {
        return protocol_error("Game machine input slot transfer command is incomplete");
    }
    size_t offset = 0;
    GameMachineInputSlotTransferCommand decoded;
    decoded.request_id = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    decoded.expected_inventory_revision = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    decoded.direction = static_cast<GameMachineInputSlotTransferDirection>(
        std::to_integer<uint8_t>(bytes[offset++]));
    auto dimension_id = read_short_string(bytes, offset, kMaxGameDimensionIdBytes,
                                          "machine input slot transfer dimension id", true);
    if (!dimension_id) return dimension_id.error();
    constexpr size_t kBodyBytes = sizeof(uint32_t) * 4 + sizeof(uint16_t) * 3;
    if (bytes.size() - offset < kBodyBytes) {
        return protocol_error("Game machine input slot transfer coordinates are truncated");
    }
    decoded.dimension_id = std::move(*dimension_id);
    decoded.root_x = read_i32(bytes, offset);
    decoded.root_y = read_i32(bytes, offset + sizeof(uint32_t));
    decoded.root_z = read_i32(bytes, offset + sizeof(uint32_t) * 2);
    offset += sizeof(uint32_t) * 3;
    decoded.expected_material = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    decoded.player_slot = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    decoded.machine_input_slot = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    decoded.count = read_i32(bytes, offset);
    offset += sizeof(uint32_t);
    auto expected_player = read_inventory_stack(
        bytes, offset, "Game machine input slot transfer expected player slot");
    if (!expected_player) return expected_player.error();
    auto expected_machine = read_inventory_stack(
        bytes, offset, "Game machine input slot transfer expected machine input slot");
    if (!expected_machine) return expected_machine.error();
    if (offset != bytes.size()) {
        return protocol_error("Game machine input slot transfer command has trailing bytes");
    }
    decoded.expected_player_slot = std::move(*expected_player);
    decoded.expected_machine_input_slot = std::move(*expected_machine);
    if (auto result = validate_game_machine_input_slot_transfer_command(decoded); !result) {
        return result.error();
    }
    return decoded;
}

snt::core::Expected<GameReplicationMessage> make_game_snapshot(
    const GameSnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kServerSnapshot;
    append_u64(message.payload, snapshot.snapshot_id);
    append_u16(message.payload, static_cast<uint16_t>(snapshot.chunks.size()));
    for (const GameChunkSnapshot& chunk : snapshot.chunks) {
        if (auto result = append_chunk_key(message.payload, chunk.chunk); !result) return result.error();
        append_u32(message.payload, static_cast<uint32_t>(chunk.payload.size()));
        message.payload.insert(message.payload.end(), chunk.payload.begin(), chunk.payload.end());
    }
    append_u16(message.payload, static_cast<uint16_t>(snapshot.entities.size()));
    for (const GameEntitySnapshot& entity : snapshot.entities) {
        append_u64(message.payload, entity.entity_guid.value);
        append_u32(message.payload, static_cast<uint32_t>(entity.payload.size()));
        message.payload.insert(message.payload.end(), entity.payload.begin(), entity.payload.end());
    }
    append_u16(message.payload, static_cast<uint16_t>(snapshot.values.size()));
    for (const GameReplicationValue& value : snapshot.values) {
        message.payload.push_back(static_cast<std::byte>(value.kind));
        message.payload.push_back(static_cast<std::byte>(value.operation));
        append_u32(message.payload, static_cast<uint32_t>(value.payload.size()));
        message.payload.insert(message.payload.end(), value.payload.begin(), value.payload.end());
    }
    if (auto result = validate_message_shape(message); !result) return result.error();
    return message;
}

snt::core::Expected<GameSnapshot> parse_game_snapshot(
    const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kServerSnapshot);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    if (bytes.size() < sizeof(uint64_t) + sizeof(uint16_t)) {
        return protocol_error("Game replication snapshot is truncated");
    }

    size_t offset = 0;
    GameSnapshot snapshot;
    snapshot.snapshot_id = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    const size_t chunk_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (chunk_count > kMaxGameSnapshotChunks) {
        return protocol_error("Game replication snapshot has too many chunks");
    }
    snapshot.chunks.reserve(chunk_count);
    for (size_t index = 0; index < chunk_count; ++index) {
        auto chunk = read_chunk_key(bytes, offset);
        if (!chunk) return chunk.error();
        auto payload = read_byte_vector(bytes, offset, kMaxGameChunkSnapshotPayloadBytes,
                                        "chunk snapshot payload");
        if (!payload) return payload.error();
        snapshot.chunks.push_back({.chunk = std::move(*chunk), .payload = std::move(*payload)});
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication snapshot entity count is truncated");
    }
    const size_t entity_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (entity_count > kMaxGameSnapshotEntities) {
        return protocol_error("Game replication snapshot has too many entities");
    }
    snapshot.entities.reserve(entity_count);
    for (size_t index = 0; index < entity_count; ++index) {
        if (bytes.size() - offset < sizeof(uint64_t)) {
            return protocol_error("Game replication snapshot entity guid is truncated");
        }
        GameEntitySnapshot entity;
        entity.entity_guid = {read_u64(bytes, offset)};
        offset += sizeof(uint64_t);
        auto payload = read_byte_vector(bytes, offset, kMaxGameEntitySnapshotPayloadBytes,
                                        "entity snapshot payload");
        if (!payload) return payload.error();
        entity.payload = std::move(*payload);
        snapshot.entities.push_back(std::move(entity));
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication snapshot value count is truncated");
    }
    const size_t value_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (value_count > kMaxGameSnapshotValues) {
        return protocol_error("Game replication snapshot has too many values");
    }
    snapshot.values.reserve(value_count);
    for (size_t index = 0; index < value_count; ++index) {
        if (bytes.size() - offset < sizeof(uint8_t) * 2) {
            return protocol_error("Game replication snapshot value header is truncated");
        }
        GameReplicationValue value;
        value.kind = static_cast<GameReplicationValueKind>(
            std::to_integer<uint8_t>(bytes[offset++]));
        value.operation = static_cast<GameReplicationValueOperation>(
            std::to_integer<uint8_t>(bytes[offset++]));
        auto payload = read_byte_vector(bytes, offset, kMaxGameReplicationValuePayloadBytes,
                                        "snapshot value payload");
        if (!payload) return payload.error();
        value.payload = std::move(*payload);
        snapshot.values.push_back(std::move(value));
    }

    if (offset != bytes.size()) return protocol_error("Game replication snapshot has trailing bytes");
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    return snapshot;
}

snt::core::Expected<GameReplicationMessage> make_game_delta(const GameDelta& delta) {
    if (auto result = validate_delta(delta); !result) return result.error();

    GameReplicationMessage message;
    message.kind = GameReplicationMessageKind::kServerDelta;
    append_u64(message.payload, delta.base_snapshot_id);
    append_u64(message.payload, delta.sequence);
    append_u16(message.payload, static_cast<uint16_t>(delta.chunk_snapshots.size()));
    for (const GameChunkSnapshot& chunk : delta.chunk_snapshots) {
        if (auto result = append_chunk_key(message.payload, chunk.chunk); !result) return result.error();
        append_u32(message.payload, static_cast<uint32_t>(chunk.payload.size()));
        message.payload.insert(message.payload.end(), chunk.payload.begin(), chunk.payload.end());
    }
    append_u16(message.payload, static_cast<uint16_t>(delta.removed_chunks.size()));
    for (const snt::voxel::ChunkKey& chunk : delta.removed_chunks) {
        if (auto result = append_chunk_key(message.payload, chunk); !result) return result.error();
    }
    append_u16(message.payload, static_cast<uint16_t>(delta.chunks.size()));
    for (const GameChunkDelta& chunk : delta.chunks) {
        if (auto result = append_chunk_key(message.payload, chunk.chunk); !result) return result.error();
        append_u16(message.payload, static_cast<uint16_t>(chunk.blocks.size()));
        for (const GameBlockDelta& block : chunk.blocks) {
            append_u16(message.payload, block.local_index);
            message.payload.push_back(static_cast<std::byte>(block.material));
            append_u32(message.payload, block.flags);
            append_u16(message.payload, block.fluid_type);
            append_i16(message.payload, block.fluid_mass);
            append_i16(message.payload, block.fluid_temperature);
            message.payload.push_back(static_cast<std::byte>(block.fluid_is_gas ? 1 : 0));
        }
    }
    append_u16(message.payload, static_cast<uint16_t>(delta.entities.size()));
    for (const GameEntitySnapshot& entity : delta.entities) {
        append_u64(message.payload, entity.entity_guid.value);
        append_u32(message.payload, static_cast<uint32_t>(entity.payload.size()));
        message.payload.insert(message.payload.end(), entity.payload.begin(), entity.payload.end());
    }
    append_u16(message.payload, static_cast<uint16_t>(delta.values.size()));
    for (const GameReplicationValue& value : delta.values) {
        message.payload.push_back(static_cast<std::byte>(value.kind));
        message.payload.push_back(static_cast<std::byte>(value.operation));
        append_u32(message.payload, static_cast<uint32_t>(value.payload.size()));
        message.payload.insert(message.payload.end(), value.payload.begin(), value.payload.end());
    }
    if (auto result = validate_message_shape(message); !result) return result.error();
    return message;
}

snt::core::Expected<GameDelta> parse_game_delta(const GameReplicationMessage& message) {
    if (auto result = validate_message_kind(message, GameReplicationMessageKind::kServerDelta);
        !result) {
        return result.error();
    }

    const std::span<const std::byte> bytes(message.payload.data(), message.payload.size());
    constexpr size_t kDeltaPrefixBytes = sizeof(uint64_t) * 2 + sizeof(uint16_t);
    if (bytes.size() < kDeltaPrefixBytes) {
        return protocol_error("Game replication delta is truncated");
    }

    size_t offset = 0;
    GameDelta delta;
    delta.base_snapshot_id = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    delta.sequence = read_u64(bytes, offset);
    offset += sizeof(uint64_t);
    const size_t chunk_snapshot_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (chunk_snapshot_count > kMaxGameDeltaChunks) {
        return protocol_error("Game replication delta has too many chunk snapshots");
    }
    delta.chunk_snapshots.reserve(chunk_snapshot_count);
    for (size_t index = 0; index < chunk_snapshot_count; ++index) {
        auto chunk = read_chunk_key(bytes, offset);
        if (!chunk) return chunk.error();
        auto payload = read_byte_vector(bytes, offset, kMaxGameChunkSnapshotPayloadBytes,
                                        "delta chunk snapshot payload");
        if (!payload) return payload.error();
        delta.chunk_snapshots.push_back({.chunk = std::move(*chunk),
                                         .payload = std::move(*payload)});
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication delta removed chunk count is truncated");
    }
    const size_t removed_chunk_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (removed_chunk_count > kMaxGameDeltaChunks) {
        return protocol_error("Game replication delta has too many removed chunks");
    }
    delta.removed_chunks.reserve(removed_chunk_count);
    for (size_t index = 0; index < removed_chunk_count; ++index) {
        auto chunk = read_chunk_key(bytes, offset);
        if (!chunk) return chunk.error();
        delta.removed_chunks.push_back(std::move(*chunk));
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication delta block chunk count is truncated");
    }
    const size_t chunk_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (chunk_count > kMaxGameDeltaChunks) {
        return protocol_error("Game replication delta has too many block chunks");
    }
    delta.chunks.reserve(chunk_count);
    size_t total_block_count = 0;
    for (size_t index = 0; index < chunk_count; ++index) {
        auto chunk = read_chunk_key(bytes, offset);
        if (!chunk) return chunk.error();
        if (bytes.size() - offset < sizeof(uint16_t)) {
            return protocol_error("Game replication delta block count is truncated");
        }
        const size_t block_count = read_u16(bytes, offset);
        offset += sizeof(uint16_t);
        if (block_count == 0 || block_count > kMaxGameBlockDeltasPerChunk ||
            total_block_count > kMaxGameBlockDeltas - block_count) {
            return protocol_error("Game replication delta block count is invalid");
        }
        total_block_count += block_count;

        GameChunkDelta chunk_delta;
        chunk_delta.chunk = std::move(*chunk);
        chunk_delta.blocks.reserve(block_count);
        constexpr size_t kBlockDeltaBytes = sizeof(uint16_t) + sizeof(uint8_t) +
                                            sizeof(uint32_t) + sizeof(uint16_t) +
                                            sizeof(int16_t) + sizeof(int16_t) + sizeof(uint8_t);
        for (size_t block_index = 0; block_index < block_count; ++block_index) {
            if (bytes.size() - offset < kBlockDeltaBytes) {
                return protocol_error("Game replication block delta is truncated");
            }
            GameBlockDelta block;
            block.local_index = read_u16(bytes, offset);
            offset += sizeof(uint16_t);
            block.material = std::to_integer<snt::voxel::TerrainMaterialId>(bytes[offset++]);
            block.flags = read_u32(bytes, offset);
            offset += sizeof(uint32_t);
            block.fluid_type = read_u16(bytes, offset);
            offset += sizeof(uint16_t);
            block.fluid_mass = read_i16(bytes, offset);
            offset += sizeof(int16_t);
            block.fluid_temperature = read_i16(bytes, offset);
            offset += sizeof(int16_t);
            const uint8_t fluid_is_gas = std::to_integer<uint8_t>(bytes[offset++]);
            if (fluid_is_gas > 1) {
                return protocol_error("Game replication block delta has invalid gas flag");
            }
            block.fluid_is_gas = fluid_is_gas != 0;
            chunk_delta.blocks.push_back(block);
        }
        delta.chunks.push_back(std::move(chunk_delta));
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication delta entity count is truncated");
    }
    const size_t entity_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (entity_count > kMaxGameSnapshotEntities) {
        return protocol_error("Game replication delta has too many entities");
    }
    delta.entities.reserve(entity_count);
    for (size_t index = 0; index < entity_count; ++index) {
        if (bytes.size() - offset < sizeof(uint64_t)) {
            return protocol_error("Game replication delta entity guid is truncated");
        }
        GameEntitySnapshot entity;
        entity.entity_guid = {read_u64(bytes, offset)};
        offset += sizeof(uint64_t);
        auto payload = read_byte_vector(bytes, offset, kMaxGameEntitySnapshotPayloadBytes,
                                        "entity delta payload");
        if (!payload) return payload.error();
        entity.payload = std::move(*payload);
        delta.entities.push_back(std::move(entity));
    }

    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Game replication delta value count is truncated");
    }
    const size_t value_count = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (value_count > kMaxGameSnapshotValues) {
        return protocol_error("Game replication delta has too many values");
    }
    delta.values.reserve(value_count);
    for (size_t index = 0; index < value_count; ++index) {
        if (bytes.size() - offset < sizeof(uint8_t) * 2) {
            return protocol_error("Game replication delta value header is truncated");
        }
        GameReplicationValue value;
        value.kind = static_cast<GameReplicationValueKind>(
            std::to_integer<uint8_t>(bytes[offset++]));
        value.operation = static_cast<GameReplicationValueOperation>(
            std::to_integer<uint8_t>(bytes[offset++]));
        auto payload = read_byte_vector(bytes, offset, kMaxGameReplicationValuePayloadBytes,
                                        "delta value payload");
        if (!payload) return payload.error();
        value.payload = std::move(*payload);
        delta.values.push_back(std::move(value));
    }

    if (offset != bytes.size()) return protocol_error("Game replication delta has trailing bytes");
    if (auto result = validate_delta(delta); !result) return result.error();
    return delta;
}

}  // namespace snt::game::replication
