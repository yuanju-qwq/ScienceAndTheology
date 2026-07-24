// Chunk-owned ground-loot presentation replication implementation.

#include "game/network/game_ground_loot_replication.h"

#include "core/error.h"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
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

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

void append_i64(std::vector<std::byte>& bytes, int64_t value) {
    append_u64(bytes, std::bit_cast<uint64_t>(value));
}

void append_float(std::vector<std::byte>& bytes, float value) {
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

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] int64_t read_i64(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int64_t>(read_u64(bytes, offset));
}

[[nodiscard]] float read_float(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

[[nodiscard]] snt::core::Expected<void> append_text(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum_length,
    std::string_view field_name, bool required) {
    if ((required && value.empty()) || value.size() > maximum_length ||
        value.size() > std::numeric_limits<uint16_t>::max() || has_embedded_nul(value)) {
        return protocol_error("Ground loot presentation " + std::string(field_name) +
                              " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_text(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum_length,
    std::string_view field_name, bool required) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Ground loot presentation " + std::string(field_name) +
                              " is truncated");
    }
    const size_t length = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((required && length == 0) || length > maximum_length ||
        bytes.size() - offset < length) {
        return protocol_error("Ground loot presentation " + std::string(field_name) +
                              " is invalid");
    }
    std::string value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += length;
    if (has_embedded_nul(value)) {
        return protocol_error("Ground loot presentation " + std::string(field_name) +
                              " contains a NUL byte");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_state(
    const GameGroundLootPresentationState& state) {
    if (state.loot_id == 0 || state.chunk.dimension_id.empty() ||
        state.chunk.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(state.chunk.dimension_id) || !state.resource.is_valid() ||
        !state.resource.is_item() ||
        state.resource.key.type.size() > kMaxGameResourceTypeBytes ||
        state.resource.key.id.size() > kMaxGameResourceIdBytes ||
        state.resource.key.variant.size() > kMaxGameResourceVariantBytes ||
        has_embedded_nul(state.resource.key.type) || has_embedded_nul(state.resource.key.id) ||
        has_embedded_nul(state.resource.key.variant) || !std::isfinite(state.position_x) ||
        !std::isfinite(state.position_y) || !std::isfinite(state.position_z)) {
        return protocol_error("Ground loot presentation state is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_snapshot(
    const GameGroundLootPresentationSnapshot& snapshot) {
    if (snapshot.loot.size() > kMaxGameGroundLootPresentationStates) {
        return protocol_error("Ground loot presentation snapshot has too many states");
    }
    uint64_t previous_id = 0;
    for (const GameGroundLootPresentationState& state : snapshot.loot) {
        if (auto result = validate_state(state); !result) return result.error();
        if (state.loot_id <= previous_id) {
            return protocol_error("Ground loot presentation states are not uniquely ordered");
        }
        previous_id = state.loot_id;
    }
    return {};
}

[[nodiscard]] const GameReplicationValue* find_ground_loot_value(
    std::span<const GameReplicationValue> values, bool is_snapshot) {
    const GameReplicationValue* result = nullptr;
    for (const GameReplicationValue& value : values) {
        if (value.kind != GameReplicationValueKind::kGroundLootPresentation) continue;
        if (result != nullptr) return nullptr;
        if (is_snapshot && value.operation != GameReplicationValueOperation::kUpsert) return nullptr;
        result = &value;
    }
    return result;
}

[[nodiscard]] bool has_duplicate_ground_loot_value(
    std::span<const GameReplicationValue> values) noexcept {
    bool found = false;
    for (const GameReplicationValue& value : values) {
        if (value.kind != GameReplicationValueKind::kGroundLootPresentation) continue;
        if (found) return true;
        found = true;
    }
    return false;
}

}  // namespace

snt::core::Expected<std::vector<std::byte>> encode_game_ground_loot_presentation_snapshot(
    const GameGroundLootPresentationSnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();

    std::vector<std::byte> payload;
    payload.reserve(16 + snapshot.loot.size() * 64);
    payload.push_back(static_cast<std::byte>(kGameGroundLootPresentationReplicationVersion));
    append_u64(payload, snapshot.source_tick);
    append_u16(payload, static_cast<uint16_t>(snapshot.loot.size()));
    for (const GameGroundLootPresentationState& state : snapshot.loot) {
        append_u64(payload, state.loot_id);
        if (auto result = append_text(payload, state.chunk.dimension_id,
                                      kMaxGameDimensionIdBytes, "dimension id", true);
            !result) {
            return result.error();
        }
        append_i32(payload, state.chunk.chunk_x);
        append_i32(payload, state.chunk.chunk_y);
        append_i32(payload, state.chunk.chunk_z);
        if (auto result = append_text(payload, state.resource.key.type,
                                      kMaxGameResourceTypeBytes, "resource type", true);
            !result) {
            return result.error();
        }
        if (auto result = append_text(payload, state.resource.key.id,
                                      kMaxGameResourceIdBytes, "resource id", true);
            !result) {
            return result.error();
        }
        if (auto result = append_text(payload, state.resource.key.variant,
                                      kMaxGameResourceVariantBytes, "resource variant", false);
            !result) {
            return result.error();
        }
        append_i64(payload, state.resource.amount);
        append_float(payload, state.position_x);
        append_float(payload, state.position_y);
        append_float(payload, state.position_z);
        append_u64(payload, state.spawned_tick);
    }
    if (payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Ground loot presentation snapshot exceeds the replication payload limit");
    }
    return payload;
}

snt::core::Expected<GameGroundLootPresentationSnapshot>
decode_game_ground_loot_presentation_snapshot(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint16_t) ||
        std::to_integer<uint8_t>(payload.front()) !=
            kGameGroundLootPresentationReplicationVersion) {
        return protocol_error("Ground loot presentation snapshot version is invalid");
    }
    if (payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Ground loot presentation snapshot exceeds the replication payload limit");
    }

    size_t offset = sizeof(uint8_t);
    GameGroundLootPresentationSnapshot snapshot;
    snapshot.source_tick = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    const size_t state_count = read_u16(payload, offset);
    offset += sizeof(uint16_t);
    if (state_count > kMaxGameGroundLootPresentationStates) {
        return protocol_error("Ground loot presentation snapshot has too many states");
    }
    snapshot.loot.reserve(state_count);
    for (size_t index = 0; index < state_count; ++index) {
        if (payload.size() - offset < sizeof(uint64_t)) {
            return protocol_error("Ground loot presentation state id is truncated");
        }
        GameGroundLootPresentationState state;
        state.loot_id = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        auto dimension_id = read_text(payload, offset, kMaxGameDimensionIdBytes,
                                      "dimension id", true);
        if (!dimension_id) return dimension_id.error();
        if (payload.size() - offset < sizeof(int32_t) * 3) {
            return protocol_error("Ground loot presentation chunk is truncated");
        }
        state.chunk.dimension_id = std::move(*dimension_id);
        state.chunk.chunk_x = read_i32(payload, offset);
        offset += sizeof(int32_t);
        state.chunk.chunk_y = read_i32(payload, offset);
        offset += sizeof(int32_t);
        state.chunk.chunk_z = read_i32(payload, offset);
        offset += sizeof(int32_t);
        auto resource_type = read_text(payload, offset, kMaxGameResourceTypeBytes,
                                       "resource type", true);
        if (!resource_type) return resource_type.error();
        auto resource_id = read_text(payload, offset, kMaxGameResourceIdBytes,
                                     "resource id", true);
        if (!resource_id) return resource_id.error();
        auto resource_variant = read_text(payload, offset, kMaxGameResourceVariantBytes,
                                          "resource variant", false);
        if (!resource_variant) return resource_variant.error();
        constexpr size_t kStateTailBytes = sizeof(int64_t) + sizeof(float) * 3 + sizeof(uint64_t);
        if (payload.size() - offset < kStateTailBytes) {
            return protocol_error("Ground loot presentation state is truncated");
        }
        state.resource.key.type = std::move(*resource_type);
        state.resource.key.id = std::move(*resource_id);
        state.resource.key.variant = std::move(*resource_variant);
        state.resource.amount = read_i64(payload, offset);
        offset += sizeof(int64_t);
        state.position_x = read_float(payload, offset);
        offset += sizeof(float);
        state.position_y = read_float(payload, offset);
        offset += sizeof(float);
        state.position_z = read_float(payload, offset);
        offset += sizeof(float);
        state.spawned_tick = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        snapshot.loot.push_back(std::move(state));
    }
    if (offset != payload.size()) {
        return protocol_error("Ground loot presentation snapshot has trailing bytes");
    }
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    return snapshot;
}

snt::core::Expected<void> GameRemoteGroundLootWorld::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Client ground loot world received an invalid snapshot id");
    }
    if (has_duplicate_ground_loot_value(snapshot.values)) {
        return protocol_error("Client ground loot snapshot has duplicate value records");
    }
    const GameReplicationValue* value = find_ground_loot_value(snapshot.values, true);
    if (value == nullptr) {
        bool has_ground_loot_value = false;
        for (const GameReplicationValue& candidate : snapshot.values) {
            has_ground_loot_value = has_ground_loot_value ||
                candidate.kind == GameReplicationValueKind::kGroundLootPresentation;
        }
        if (has_ground_loot_value) {
            return protocol_error("Client ground loot snapshot has an invalid value record");
        }
        loot_.clear();
        latest_source_tick_ = 0;
    } else {
        auto decoded = decode_game_ground_loot_presentation_snapshot(value->payload);
        if (!decoded) return decoded.error();
        latest_source_tick_ = 0;
        if (auto result = replace_current_set(*decoded); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemoteGroundLootWorld::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Client ground loot delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Client ground loot delta sequence is invalid");
    }
    if (has_duplicate_ground_loot_value(delta.values)) {
        return protocol_error("Client ground loot delta has duplicate value records");
    }
    const GameReplicationValue* value = find_ground_loot_value(delta.values, false);
    if (value != nullptr) {
        switch (value->operation) {
            case GameReplicationValueOperation::kUpsert: {
                auto decoded = decode_game_ground_loot_presentation_snapshot(value->payload);
                if (!decoded) return decoded.error();
                if (auto result = replace_current_set(*decoded); !result) return result.error();
                break;
            }
            case GameReplicationValueOperation::kRemove:
                if (!value->payload.empty()) {
                    return protocol_error("Client ground loot removal carries a payload");
                }
                loot_.clear();
                latest_source_tick_ = 0;
                break;
            default:
                return protocol_error("Client ground loot delta has an invalid value operation");
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::vector<GameGroundLootPresentationState> GameRemoteGroundLootWorld::loot() const {
    std::vector<GameGroundLootPresentationState> result;
    result.reserve(loot_.size());
    for (const auto& [loot_id, state] : loot_) {
        static_cast<void>(loot_id);
        result.push_back(state);
    }
    return result;
}

std::optional<GameGroundLootPresentationState> GameRemoteGroundLootWorld::find_loot(
    uint64_t loot_id) const {
    const auto found = loot_.find(loot_id);
    if (found == loot_.end()) return std::nullopt;
    return found->second;
}

void GameRemoteGroundLootWorld::clear() noexcept {
    loot_.clear();
    latest_source_tick_ = 0;
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemoteGroundLootWorld::replace_current_set(
    const GameGroundLootPresentationSnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    if (snapshot.source_tick < latest_source_tick_) {
        return protocol_error("Client ground loot state regressed its authoritative source tick");
    }
    std::map<uint64_t, GameGroundLootPresentationState> next;
    for (const GameGroundLootPresentationState& state : snapshot.loot) {
        next.emplace(state.loot_id, state);
    }
    loot_ = std::move(next);
    latest_source_tick_ = snapshot.source_tick;
    return {};
}

}  // namespace snt::game::replication
