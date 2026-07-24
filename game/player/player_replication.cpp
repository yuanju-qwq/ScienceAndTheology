// Game-owned player entity replication values implementation.

#define SNT_LOG_CHANNEL "game.player_replication"
#include "game/player/player_replication.h"

#include "core/error.h"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kPlayerEntityHeaderBytes = 4;
constexpr size_t kMaxEquipmentItemIdBytes = 256;

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

void append_f32(std::vector<std::byte>& bytes, float value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
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
    for (size_t index = 0; index < sizeof(uint32_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(uint64_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] int16_t read_i16(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int16_t>(read_u16(bytes, offset));
}

[[nodiscard]] float read_f32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if ((require_non_empty && value.empty()) || value.size() > maximum ||
        value.size() > std::numeric_limits<uint16_t>::max() || has_embedded_nul(value)) {
        return protocol_error(std::string("Player replication ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Player replication ") + field_name + " is truncated");
    }
    const size_t size = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && size == 0) || size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Player replication ") + field_name + " is invalid");
    }

    std::string value;
    value.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += size;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("Player replication ") + field_name + " is invalid");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_player_state(
    const GameReplicatedPlayerState& state) {
    if (auto result = validate_player_identity(state.identity); !result) {
        return protocol_error("Player replication identity is invalid");
    }
    if (state.position.dimension_id.empty() ||
        state.position.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(state.position.dimension_id)) {
        return protocol_error("Player replication dimension id is invalid");
    }
    constexpr float kMaximumCoordinateMagnitude = 16'000'000.0f;
    const auto valid_coordinate = [](float value) {
        return std::isfinite(value) && std::fabs(value) <= kMaximumCoordinateMagnitude;
    };
    if (!valid_coordinate(state.motion.feet_x) || !valid_coordinate(state.motion.feet_y) ||
        !valid_coordinate(state.motion.feet_z) || !std::isfinite(state.motion.velocity_x) ||
        !std::isfinite(state.motion.velocity_y) || !std::isfinite(state.motion.velocity_z) ||
        state.motion.yaw_centidegrees < -18000 || state.motion.yaw_centidegrees >= 18000 ||
        state.motion.pitch_centidegrees < -8900 || state.motion.pitch_centidegrees > 8900) {
        return protocol_error("Player replication motion state is invalid");
    }
    for (const std::string& item_id : state.equipment_item_ids) {
        if (item_id.size() > kMaxEquipmentItemIdBytes || has_embedded_nul(item_id)) {
            return protocol_error("Player replication equipment item id is invalid");
        }
    }
    return {};
}

[[nodiscard]] bool is_valid_operation(GamePlayerReplicationOperation operation) noexcept {
    switch (operation) {
        case GamePlayerReplicationOperation::kUpsert:
        case GamePlayerReplicationOperation::kRemove:
            return true;
    }
    return false;
}

}  // namespace

bool is_game_player_replication_entity_payload(std::span<const std::byte> payload) noexcept {
    return !payload.empty() &&
           std::to_integer<uint8_t>(payload.front()) == kGamePlayerReplicationEntityKind;
}

snt::core::Expected<void> validate_game_replicated_player_state(
    const GameReplicatedPlayerState& state) {
    return validate_player_state(state);
}

snt::core::Expected<std::vector<std::byte>> encode_game_player_replication_entity(
    const GamePlayerReplicationEntity& entity) {
    if (!is_valid_operation(entity.operation)) {
        return protocol_error("Player replication operation is invalid");
    }
    if (entity.operation == GamePlayerReplicationOperation::kRemove) {
        if (entity.player.has_value()) {
            return protocol_error("Player replication remove must not carry player state");
        }
        return std::vector<std::byte>{
            static_cast<std::byte>(kGamePlayerReplicationEntityKind),
            static_cast<std::byte>(kGamePlayerReplicationEntityVersion),
            static_cast<std::byte>(entity.operation),
            std::byte{0},
        };
    }
    if (!entity.player.has_value()) {
        return protocol_error("Player replication upsert has no player state");
    }
    if (auto result = validate_game_replicated_player_state(*entity.player); !result) {
        return result.error();
    }

    std::vector<std::byte> bytes;
    bytes.reserve(256);
    bytes.push_back(static_cast<std::byte>(kGamePlayerReplicationEntityKind));
    bytes.push_back(static_cast<std::byte>(kGamePlayerReplicationEntityVersion));
    bytes.push_back(static_cast<std::byte>(entity.operation));
    bytes.push_back(std::byte{0});
    bytes.push_back(static_cast<std::byte>(entity.player->identity.provider));
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    if (auto result = append_short_string(bytes, entity.player->identity.account_id,
                                          kMaxPlayerAccountIdBytes, "account id", true);
        !result) {
        return result.error();
    }
    if (auto result = append_short_string(bytes, entity.player->identity.display_name,
                                          kMaxPlayerDisplayNameBytes, "display name", true);
        !result) {
        return result.error();
    }
    if (auto result = append_short_string(bytes, entity.player->position.dimension_id,
                                          kMaxGameDimensionIdBytes, "dimension id", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, entity.player->position.position.x);
    append_i32(bytes, entity.player->position.position.y);
    append_i32(bytes, entity.player->position.position.z);
    append_u64(bytes, entity.player->motion.source_tick);
    append_u64(bytes, entity.player->motion.last_processed_input_sequence);
    append_f32(bytes, entity.player->motion.feet_x);
    append_f32(bytes, entity.player->motion.feet_y);
    append_f32(bytes, entity.player->motion.feet_z);
    append_f32(bytes, entity.player->motion.velocity_x);
    append_f32(bytes, entity.player->motion.velocity_y);
    append_f32(bytes, entity.player->motion.velocity_z);
    append_i16(bytes, entity.player->motion.yaw_centidegrees);
    append_i16(bytes, entity.player->motion.pitch_centidegrees);
    bytes.push_back(static_cast<std::byte>(entity.player->motion.grounded ? 1 : 0));
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    for (const std::string& item_id : entity.player->equipment_item_ids) {
        if (auto result = append_short_string(bytes, item_id, kMaxEquipmentItemIdBytes,
                                              "equipment item id", false);
            !result) {
            return result.error();
        }
    }
    return bytes;
}

snt::core::Expected<GamePlayerReplicationEntity>
decode_game_player_replication_entity(std::span<const std::byte> payload) {
    if (payload.size() < kPlayerEntityHeaderBytes ||
        !is_game_player_replication_entity_payload(payload) ||
        std::to_integer<uint8_t>(payload[1]) != kGamePlayerReplicationEntityVersion ||
        std::to_integer<uint8_t>(payload[3]) != 0) {
        return protocol_error("Player replication entity header is invalid");
    }

    const auto operation = static_cast<GamePlayerReplicationOperation>(
        std::to_integer<uint8_t>(payload[2]));
    if (!is_valid_operation(operation)) {
        return protocol_error("Player replication operation is invalid");
    }
    if (operation == GamePlayerReplicationOperation::kRemove) {
        if (payload.size() != kPlayerEntityHeaderBytes) {
            return protocol_error("Player replication remove has trailing bytes");
        }
        return GamePlayerReplicationEntity{.operation = operation, .player = std::nullopt};
    }

    constexpr size_t kUpsertPrefixBytes = 4;
    constexpr size_t kPositionBytes = sizeof(uint32_t) * 3;
    constexpr size_t kMotionBytes = sizeof(uint64_t) * 2 + sizeof(uint32_t) * 6 +
                                    sizeof(uint16_t) * 2 + 4;
    size_t offset = kPlayerEntityHeaderBytes;
    if (payload.size() - offset < kUpsertPrefixBytes) {
        return protocol_error("Player replication upsert is truncated");
    }
    const auto provider = static_cast<PlayerIdentityProvider>(
        std::to_integer<uint8_t>(payload[offset]));
    if (!is_valid_player_identity_provider(provider) ||
        std::to_integer<uint8_t>(payload[offset + 1]) != 0 ||
        std::to_integer<uint8_t>(payload[offset + 2]) != 0 ||
        std::to_integer<uint8_t>(payload[offset + 3]) != 0) {
        return protocol_error("Player replication identity provider is invalid");
    }
    offset += kUpsertPrefixBytes;

    auto account_id = read_short_string(payload, offset, kMaxPlayerAccountIdBytes,
                                        "account id", true);
    if (!account_id) return account_id.error();
    auto display_name = read_short_string(payload, offset, kMaxPlayerDisplayNameBytes,
                                          "display name", true);
    if (!display_name) return display_name.error();
    auto dimension_id = read_short_string(payload, offset, kMaxGameDimensionIdBytes,
                                          "dimension id", true);
    if (!dimension_id) return dimension_id.error();
    if (payload.size() - offset < kPositionBytes + kMotionBytes) {
        return protocol_error("Player replication position is truncated");
    }
    GameReplicatedPlayerState state{
        .identity = {
            .provider = provider,
            .account_id = std::move(*account_id),
            .display_name = std::move(*display_name),
        },
        .position = {
            .dimension_id = std::move(*dimension_id),
            .position = {
                .x = read_i32(payload, offset),
                .y = read_i32(payload, offset + sizeof(uint32_t)),
                .z = read_i32(payload, offset + sizeof(uint32_t) * 2),
            },
        },
    };
    offset += kPositionBytes;
    const uint64_t source_tick = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    const uint64_t last_processed_input_sequence = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    state.motion.feet_x = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.feet_y = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.feet_z = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.velocity_x = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.velocity_y = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.velocity_z = read_f32(payload, offset);
    offset += sizeof(uint32_t);
    state.motion.yaw_centidegrees = read_i16(payload, offset);
    offset += sizeof(uint16_t);
    state.motion.pitch_centidegrees = read_i16(payload, offset);
    offset += sizeof(uint16_t);
    const uint8_t grounded = std::to_integer<uint8_t>(payload[offset]);
    if (grounded > 1 || std::to_integer<uint8_t>(payload[offset + 1]) != 0 ||
        std::to_integer<uint8_t>(payload[offset + 2]) != 0 ||
        std::to_integer<uint8_t>(payload[offset + 3]) != 0) {
        return protocol_error("Player replication motion state is invalid");
    }
    state.motion.grounded = grounded != 0;
    state.motion.last_processed_input_sequence = last_processed_input_sequence;
    state.motion.source_tick = source_tick;
    offset += 4;
    for (std::string& item_id : state.equipment_item_ids) {
        auto parsed_item_id = read_short_string(payload, offset, kMaxEquipmentItemIdBytes,
                                                "equipment item id", false);
        if (!parsed_item_id) return parsed_item_id.error();
        item_id = std::move(*parsed_item_id);
    }
    if (offset != payload.size()) {
        return protocol_error("Player replication upsert has trailing bytes");
    }
    if (auto result = validate_game_replicated_player_state(state); !result) return result.error();
    return GamePlayerReplicationEntity{.operation = operation, .player = std::move(state)};
}

GameRemotePlayerWorld::GameRemotePlayerWorld(std::string local_account_id)
    : local_account_id_(std::move(local_account_id)) {}

snt::core::Expected<void> GameRemotePlayerWorld::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Remote player world received an invalid snapshot id");
    }

    std::map<uint64_t, GameRemotePlayerState> next_players;
    for (const GameEntitySnapshot& entity : snapshot.entities) {
        if (!is_game_player_replication_entity_payload(entity.payload)) continue;
        if (!entity.entity_guid.valid()) {
            return protocol_error("Remote player snapshot contains an invalid entity guid");
        }
        auto decoded = decode_game_player_replication_entity(entity.payload);
        if (!decoded) return decoded.error();
        if (decoded->operation != GamePlayerReplicationOperation::kUpsert ||
            !decoded->player.has_value()) {
            return protocol_error("Remote player snapshot contains a non-upsert player entity");
        }
        next_players.insert_or_assign(entity.entity_guid.value, GameRemotePlayerState{
            .entity_guid = entity.entity_guid,
            .player = std::move(*decoded->player),
        });
    }
    players_ = std::move(next_players);
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemotePlayerWorld::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Remote player delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Remote player delta sequence is invalid");
    }

    for (const GameEntitySnapshot& entity : delta.entities) {
        if (!is_game_player_replication_entity_payload(entity.payload)) continue;
        auto decoded = decode_game_player_replication_entity(entity.payload);
        if (!decoded) return decoded.error();
        if (auto result = apply_entity(entity.entity_guid, *decoded); !result) return result.error();
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::optional<GameRemotePlayerState> GameRemotePlayerWorld::authoritative_local_player() const {
    if (local_account_id_.empty()) return std::nullopt;
    for (const auto& [entity_guid, player] : players_) {
        static_cast<void>(entity_guid);
        if (player.player.identity.account_id == local_account_id_) return player;
    }
    return std::nullopt;
}

std::vector<GameRemotePlayerState> GameRemotePlayerWorld::remote_players() const {
    std::vector<GameRemotePlayerState> result;
    result.reserve(players_.size());
    for (const auto& [entity_guid, player] : players_) {
        static_cast<void>(entity_guid);
        if (!local_account_id_.empty() && player.player.identity.account_id == local_account_id_) {
            continue;
        }
        result.push_back(player);
    }
    return result;
}

void GameRemotePlayerWorld::clear() noexcept {
    players_.clear();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemotePlayerWorld::apply_entity(
    snt::ecs::EntityGuid entity_guid, const GamePlayerReplicationEntity& entity) {
    if (!entity_guid.valid()) {
        return protocol_error("Remote player delta contains an invalid entity guid");
    }
    switch (entity.operation) {
        case GamePlayerReplicationOperation::kUpsert:
            if (!entity.player.has_value()) {
                return protocol_error("Remote player upsert has no state");
            }
            players_.insert_or_assign(entity_guid.value, GameRemotePlayerState{
                .entity_guid = entity_guid,
                .player = *entity.player,
            });
            return {};
        case GamePlayerReplicationOperation::kRemove:
            if (entity.player.has_value()) {
                return protocol_error("Remote player remove carries unexpected state");
            }
            players_.erase(entity_guid.value);
            return {};
    }
    return protocol_error("Remote player delta operation is invalid");
}

}  // namespace snt::game::replication
