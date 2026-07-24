// Native creature presentation replication implementation.

#include "game/network/game_creature_replication.h"

#include "core/error.h"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr uint8_t kCreatureFlagInteractive = 1u << 0u;
constexpr uint8_t kCreatureFlagCaptive = 1u << 1u;
constexpr uint8_t kCreatureFlagTamed = 1u << 2u;
constexpr uint8_t kKnownCreatureFlags =
    kCreatureFlagInteractive | kCreatureFlagCaptive | kCreatureFlagTamed;

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

[[nodiscard]] float read_float(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

[[nodiscard]] bool valid_role(CreatureRole role) noexcept {
    switch (role) {
        case CreatureRole::HERBIVORE:
        case CreatureRole::PREDATOR:
            return true;
        case CreatureRole::COUNT:
            return false;
    }
    return false;
}

[[nodiscard]] bool valid_age_stage(CreatureAgeStage age_stage) noexcept {
    switch (age_stage) {
        case CreatureAgeStage::BABY:
        case CreatureAgeStage::ADULT:
            return true;
    }
    return false;
}

[[nodiscard]] snt::core::Expected<void> append_dimension_id(
    std::vector<std::byte>& bytes, std::string_view dimension_id) {
    if (dimension_id.empty() || dimension_id.size() > kMaxGameDimensionIdBytes ||
        dimension_id.size() > std::numeric_limits<uint16_t>::max() ||
        has_embedded_nul(dimension_id)) {
        return protocol_error("Creature presentation dimension id is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(dimension_id.size()));
    for (const char value : dimension_id) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_dimension_id(
    std::span<const std::byte> bytes, size_t& offset) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error("Creature presentation dimension id is truncated");
    }
    const size_t length = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (length == 0 || length > kMaxGameDimensionIdBytes || bytes.size() - offset < length) {
        return protocol_error("Creature presentation dimension id is invalid");
    }
    std::string value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += length;
    if (has_embedded_nul(value)) {
        return protocol_error("Creature presentation dimension id contains a NUL byte");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_state(
    const GameCreaturePresentationState& creature) {
    if (creature.entity_id == 0 || creature.chunk.dimension_id.empty() ||
        creature.chunk.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(creature.chunk.dimension_id) || creature.species_id == 0 ||
        !valid_role(creature.role) || !valid_age_stage(creature.age_stage) ||
        !std::isfinite(creature.position_x) ||
        !std::isfinite(creature.position_y) || !std::isfinite(creature.position_z) ||
        !std::isfinite(creature.health) || creature.health < 0.0f ||
        (creature.is_interactive && creature.is_captive) ||
        (creature.is_tamed && !creature.is_captive)) {
        return protocol_error("Creature presentation state is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_snapshot(
    const GameCreaturePresentationSnapshot& snapshot) {
    if (snapshot.creatures.size() > kMaxGameCreaturePresentationStates) {
        return protocol_error("Creature presentation snapshot has too many states");
    }
    uint64_t previous_id = 0;
    for (const GameCreaturePresentationState& creature : snapshot.creatures) {
        if (auto result = validate_state(creature); !result) return result.error();
        if (creature.entity_id <= previous_id) {
            return protocol_error("Creature presentation snapshot states are not uniquely ordered");
        }
        previous_id = creature.entity_id;
    }
    return {};
}

[[nodiscard]] const GameReplicationValue* find_creature_value(
    std::span<const GameReplicationValue> values, bool is_snapshot) {
    const GameReplicationValue* result = nullptr;
    for (const GameReplicationValue& value : values) {
        if (value.kind != GameReplicationValueKind::kCreaturePresentation) continue;
        if (result != nullptr) return nullptr;
        if (is_snapshot && value.operation != GameReplicationValueOperation::kUpsert) return nullptr;
        result = &value;
    }
    return result;
}

[[nodiscard]] bool has_duplicate_creature_value(
    std::span<const GameReplicationValue> values) noexcept {
    bool found = false;
    for (const GameReplicationValue& value : values) {
        if (value.kind != GameReplicationValueKind::kCreaturePresentation) continue;
        if (found) return true;
        found = true;
    }
    return false;
}

}  // namespace

snt::core::Expected<std::vector<std::byte>> encode_game_creature_presentation_snapshot(
    const GameCreaturePresentationSnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();

    std::vector<std::byte> payload;
    payload.reserve(16 + snapshot.creatures.size() * 48);
    payload.push_back(static_cast<std::byte>(kGameCreaturePresentationReplicationVersion));
    append_u64(payload, snapshot.source_tick);
    append_u16(payload, static_cast<uint16_t>(snapshot.creatures.size()));
    for (const GameCreaturePresentationState& creature : snapshot.creatures) {
        append_u64(payload, creature.entity_id);
        if (auto result = append_dimension_id(payload, creature.chunk.dimension_id); !result) {
            return result.error();
        }
        append_i32(payload, creature.chunk.chunk_x);
        append_i32(payload, creature.chunk.chunk_y);
        append_i32(payload, creature.chunk.chunk_z);
        append_u16(payload, creature.species_id);
        payload.push_back(static_cast<std::byte>(creature.role));
        payload.push_back(static_cast<std::byte>(creature.age_stage));
        append_float(payload, creature.position_x);
        append_float(payload, creature.position_y);
        append_float(payload, creature.position_z);
        append_float(payload, creature.health);
        uint8_t flags = 0;
        if (creature.is_interactive) flags |= kCreatureFlagInteractive;
        if (creature.is_captive) flags |= kCreatureFlagCaptive;
        if (creature.is_tamed) flags |= kCreatureFlagTamed;
        payload.push_back(static_cast<std::byte>(flags));
    }
    if (payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Creature presentation snapshot exceeds the replication payload limit");
    }
    return payload;
}

snt::core::Expected<GameCreaturePresentationSnapshot>
decode_game_creature_presentation_snapshot(std::span<const std::byte> payload) {
    if (payload.size() < sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint16_t) ||
        std::to_integer<uint8_t>(payload.front()) !=
            kGameCreaturePresentationReplicationVersion) {
        return protocol_error("Creature presentation snapshot version is invalid");
    }
    if (payload.size() > kMaxGameReplicationValuePayloadBytes) {
        return protocol_error("Creature presentation snapshot exceeds the replication payload limit");
    }

    size_t offset = sizeof(uint8_t);
    GameCreaturePresentationSnapshot snapshot;
    snapshot.source_tick = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    const size_t creature_count = read_u16(payload, offset);
    offset += sizeof(uint16_t);
    if (creature_count > kMaxGameCreaturePresentationStates) {
        return protocol_error("Creature presentation snapshot has too many states");
    }
    snapshot.creatures.reserve(creature_count);
    constexpr size_t kStateBodyBytes = sizeof(uint64_t) + sizeof(int32_t) * 3 +
        sizeof(uint16_t) + sizeof(uint8_t) * 2 + sizeof(float) * 4 + sizeof(uint8_t);
    for (size_t index = 0; index < creature_count; ++index) {
        if (payload.size() - offset < sizeof(uint64_t)) {
            return protocol_error("Creature presentation state id is truncated");
        }
        GameCreaturePresentationState creature;
        creature.entity_id = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        auto dimension_id = read_dimension_id(payload, offset);
        if (!dimension_id) return dimension_id.error();
        if (payload.size() - offset < kStateBodyBytes - sizeof(uint64_t)) {
            return protocol_error("Creature presentation state is truncated");
        }
        creature.chunk.dimension_id = std::move(*dimension_id);
        creature.chunk.chunk_x = read_i32(payload, offset);
        offset += sizeof(int32_t);
        creature.chunk.chunk_y = read_i32(payload, offset);
        offset += sizeof(int32_t);
        creature.chunk.chunk_z = read_i32(payload, offset);
        offset += sizeof(int32_t);
        creature.species_id = read_u16(payload, offset);
        offset += sizeof(uint16_t);
        creature.role = static_cast<CreatureRole>(std::to_integer<uint8_t>(payload[offset++]));
        creature.age_stage = static_cast<CreatureAgeStage>(
            std::to_integer<uint8_t>(payload[offset++]));
        creature.position_x = read_float(payload, offset);
        offset += sizeof(float);
        creature.position_y = read_float(payload, offset);
        offset += sizeof(float);
        creature.position_z = read_float(payload, offset);
        offset += sizeof(float);
        creature.health = read_float(payload, offset);
        offset += sizeof(float);
        const uint8_t flags = std::to_integer<uint8_t>(payload[offset++]);
        if ((flags & ~kKnownCreatureFlags) != 0) {
            return protocol_error("Creature presentation state flags are invalid");
        }
        creature.is_interactive = (flags & kCreatureFlagInteractive) != 0;
        creature.is_captive = (flags & kCreatureFlagCaptive) != 0;
        creature.is_tamed = (flags & kCreatureFlagTamed) != 0;
        snapshot.creatures.push_back(std::move(creature));
    }
    if (offset != payload.size()) {
        return protocol_error("Creature presentation snapshot has trailing bytes");
    }
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    return snapshot;
}

snt::core::Expected<void> GameRemoteCreatureWorld::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Client creature world received an invalid snapshot id");
    }
    if (has_duplicate_creature_value(snapshot.values)) {
        return protocol_error("Client creature snapshot has duplicate value records");
    }
    const GameReplicationValue* value = find_creature_value(snapshot.values, true);
    if (value == nullptr) {
        bool has_creature_value = false;
        for (const GameReplicationValue& candidate : snapshot.values) {
            has_creature_value = has_creature_value ||
                candidate.kind == GameReplicationValueKind::kCreaturePresentation;
        }
        if (has_creature_value) {
            return protocol_error("Client creature snapshot has an invalid value record");
        }
        creatures_.clear();
        latest_source_tick_ = 0;
    } else {
        auto decoded = decode_game_creature_presentation_snapshot(value->payload);
        if (!decoded) return decoded.error();
        // A full outer snapshot replaces the entire baseline, including after
        // a server-side resynchronization whose simulation tick restarted.
        latest_source_tick_ = 0;
        if (auto result = replace_current_set(*decoded); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemoteCreatureWorld::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Client creature delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Client creature delta sequence is invalid");
    }
    if (has_duplicate_creature_value(delta.values)) {
        return protocol_error("Client creature delta has duplicate value records");
    }
    const GameReplicationValue* value = find_creature_value(delta.values, false);
    if (value != nullptr) {
        switch (value->operation) {
            case GameReplicationValueOperation::kUpsert: {
                auto decoded = decode_game_creature_presentation_snapshot(value->payload);
                if (!decoded) return decoded.error();
                if (auto result = replace_current_set(*decoded); !result) return result.error();
                break;
            }
            case GameReplicationValueOperation::kRemove:
                if (!value->payload.empty()) {
                    return protocol_error("Client creature removal carries a payload");
                }
                creatures_.clear();
                latest_source_tick_ = 0;
                break;
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::vector<GameCreaturePresentationState> GameRemoteCreatureWorld::creatures() const {
    std::vector<GameCreaturePresentationState> result;
    result.reserve(creatures_.size());
    for (const auto& [entity_id, creature] : creatures_) {
        static_cast<void>(entity_id);
        result.push_back(creature);
    }
    return result;
}

std::optional<GameCreaturePresentationState> GameRemoteCreatureWorld::find_creature(
    uint64_t entity_id) const {
    const auto found = creatures_.find(entity_id);
    if (found == creatures_.end()) return std::nullopt;
    return found->second;
}

void GameRemoteCreatureWorld::clear() noexcept {
    creatures_.clear();
    latest_source_tick_ = 0;
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemoteCreatureWorld::replace_current_set(
    const GameCreaturePresentationSnapshot& snapshot) {
    if (auto result = validate_snapshot(snapshot); !result) return result.error();
    if (snapshot.source_tick < latest_source_tick_) {
        return protocol_error("Client creature state regressed its authoritative source tick");
    }
    std::map<uint64_t, GameCreaturePresentationState> next;
    for (const GameCreaturePresentationState& creature : snapshot.creatures) {
        next.emplace(creature.entity_id, creature);
    }
    creatures_ = std::move(next);
    latest_source_tick_ = snapshot.source_tick;
    return {};
}

}  // namespace snt::game::replication
