// Source-law body persistence implementation.

#define SNT_LOG_CHANNEL "game.source_law.persistence"
#include "game/source_law/source_law_persistence_codec.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::source_law {
namespace {

constexpr uint32_t kPayloadMagic = 0x31424c53U;  // "SLB1" in little-endian bytes.
constexpr uint16_t kPayloadVersion = 1;
constexpr size_t kMaxPayloadBytes = 64U * 1024U;
constexpr size_t kMaxIdBytes = 192;
constexpr size_t kMaxTuningTags = 32;
constexpr size_t kMaxCoordinatingCircuits = 7;
constexpr size_t kMaxDiscoveredSystems = 256;
constexpr int32_t kMaxSourceValue = 1'000'000'000;
constexpr float kMaxCooldownSeconds = 86'400.0F;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_valid_id(const SourceLawId& id, bool allow_empty = false) noexcept {
    return (allow_empty && id.empty()) ||
           (!id.empty() && id.size() <= kMaxIdBytes && id.find('\0') == SourceLawId::npos);
}

[[nodiscard]] bool is_unique_valid_ids(const std::vector<SourceLawId>& values,
                                       size_t maximum,
                                       bool allow_empty = false) {
    if (values.size() > maximum) return false;
    std::set<SourceLawId> seen;
    for (const SourceLawId& value : values) {
        if (!is_valid_id(value, allow_empty) || !seen.insert(value).second) return false;
    }
    return true;
}

class PayloadWriter final {
public:
    void write_u8(uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    void write_u16(uint16_t value) {
        write_u8(static_cast<uint8_t>(value & 0xffU));
        write_u8(static_cast<uint8_t>((value >> 8U) & 0xffU));
    }

    void write_u32(uint32_t value) {
        for (uint8_t shift = 0; shift < 32; shift += 8) {
            write_u8(static_cast<uint8_t>((value >> shift) & 0xffU));
        }
    }

    void write_i32(int32_t value) { write_u32(std::bit_cast<uint32_t>(value)); }
    void write_f32(float value) { write_u32(std::bit_cast<uint32_t>(value)); }

    [[nodiscard]] bool write_id(const SourceLawId& id) {
        if (id.size() > std::numeric_limits<uint16_t>::max()) return false;
        write_u16(static_cast<uint16_t>(id.size()));
        for (const char character : id) write_u8(static_cast<uint8_t>(character));
        return true;
    }

    [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

class PayloadReader final {
public:
    explicit PayloadReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool read_u8(uint8_t& value) {
        if (cursor_ >= bytes_.size()) return false;
        value = std::to_integer<uint8_t>(bytes_[cursor_++]);
        return true;
    }

    [[nodiscard]] bool read_u16(uint16_t& value) {
        uint8_t low = 0;
        uint8_t high = 0;
        if (!read_u8(low) || !read_u8(high)) return false;
        value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8U);
        return true;
    }

    [[nodiscard]] bool read_u32(uint32_t& value) {
        value = 0;
        for (uint8_t shift = 0; shift < 32; shift += 8) {
            uint8_t part = 0;
            if (!read_u8(part)) return false;
            value |= static_cast<uint32_t>(part) << shift;
        }
        return true;
    }

    [[nodiscard]] bool read_i32(int32_t& value) {
        uint32_t raw = 0;
        if (!read_u32(raw)) return false;
        value = std::bit_cast<int32_t>(raw);
        return true;
    }

    [[nodiscard]] bool read_f32(float& value) {
        uint32_t raw = 0;
        if (!read_u32(raw)) return false;
        value = std::bit_cast<float>(raw);
        return true;
    }

    [[nodiscard]] bool read_id(SourceLawId& id, size_t maximum_bytes = kMaxIdBytes) {
        uint16_t size = 0;
        if (!read_u16(size) || size > maximum_bytes || cursor_ + size > bytes_.size()) {
            return false;
        }
        id.clear();
        id.reserve(size);
        for (uint16_t index = 0; index < size; ++index) {
            id.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes_[cursor_++])));
        }
        return true;
    }

    [[nodiscard]] bool at_end() const noexcept { return cursor_ == bytes_.size(); }

private:
    std::span<const std::byte> bytes_;
    size_t cursor_ = 0;
};

[[nodiscard]] snt::core::Expected<void> validate_player_state(
    const PlayerSourceLawState& state) {
    const SourceLawBodyState& body = state.body;
    if (!is_valid_id(body.active_path_id, true) || !is_valid_source_body_stage(body.stage) ||
        body.source_reserve_current < 0 || body.source_reserve_max < 0 ||
        body.source_reserve_current > body.source_reserve_max ||
        body.source_reserve_max > kMaxSourceValue ||
        !std::isfinite(body.source_throughput) || body.source_throughput < 0.0F ||
        !std::isfinite(body.stability) || body.stability < 0.0F || body.stability > 100.0F ||
        !std::isfinite(body.mutation) || body.mutation < 0.0F || body.mutation > 100.0F ||
        body.mana_current < 0 || body.mana_max < 0 || body.mana_current > body.mana_max ||
        body.mana_max > kMaxSourceValue || body.mental_load < 0 ||
        body.mental_load > kMaxSourceValue ||
        !std::isfinite(body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds) ||
        body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds < 0.0F ||
        body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds >
            kMaxCooldownSeconds ||
        (body.circuit_schedule.current_primary_circuit_system_id &&
         !is_valid_id(*body.circuit_schedule.current_primary_circuit_system_id)) ||
        !is_unique_valid_ids(body.circuit_schedule.coordinating_circuit_system_ids,
                             kMaxCoordinatingCircuits) ||
        !is_unique_valid_ids(state.discovered_system_ids, kMaxDiscoveredSystems)) {
        return invalid_argument("Source-law player state is invalid for persistence");
    }
    for (const std::optional<OrganInstance>& organ : body.organs) {
        if (!organ) continue;
        if (!is_valid_id(organ->definition_id) || !is_valid_id(organ->quality_id, true) ||
            !std::isfinite(organ->contamination) || organ->contamination < 0.0F ||
            organ->contamination > 1.0F || !std::isfinite(organ->integrity) ||
            organ->integrity < 0.0F || organ->integrity > 1.0F ||
            !is_unique_valid_ids(organ->tuning_tags, kMaxTuningTags)) {
            return invalid_argument("Source-law organ instance is invalid for persistence");
        }
    }
    return {};
}

[[nodiscard]] bool write_body(PayloadWriter& writer, const PlayerSourceLawState& state) {
    const SourceLawBodyState& body = state.body;
    writer.write_u32(kPayloadMagic);
    writer.write_u16(kPayloadVersion);
    if (!writer.write_id(body.active_path_id)) return false;
    writer.write_u8(static_cast<uint8_t>(body.stage));
    writer.write_u16(body.source_level);
    writer.write_i32(body.source_reserve_current);
    writer.write_i32(body.source_reserve_max);
    writer.write_f32(body.source_throughput);
    writer.write_i32(body.mana_current);
    writer.write_i32(body.mana_max);
    writer.write_f32(body.stability);
    writer.write_f32(body.mutation);
    writer.write_i32(body.mental_load);
    for (const std::optional<OrganInstance>& organ : body.organs) {
        writer.write_u8(organ ? 1U : 0U);
        if (!organ) continue;
        if (!writer.write_id(organ->definition_id) ||
            organ->tuning_tags.size() > std::numeric_limits<uint8_t>::max()) {
            return false;
        }
        writer.write_u16(organ->growth_level);
        if (!writer.write_id(organ->quality_id)) return false;
        writer.write_f32(organ->contamination);
        writer.write_f32(organ->integrity);
        writer.write_u8(static_cast<uint8_t>(organ->tuning_tags.size()));
        for (const SourceLawId& tag : organ->tuning_tags) {
            if (!writer.write_id(tag)) return false;
        }
    }
    writer.write_u8(body.circuit_schedule.current_primary_circuit_system_id ? 1U : 0U);
    if (body.circuit_schedule.current_primary_circuit_system_id &&
        !writer.write_id(*body.circuit_schedule.current_primary_circuit_system_id)) {
        return false;
    }
    if (body.circuit_schedule.coordinating_circuit_system_ids.size() >
        std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    writer.write_u8(static_cast<uint8_t>(body.circuit_schedule.coordinating_circuit_system_ids.size()));
    for (const SourceLawId& id : body.circuit_schedule.coordinating_circuit_system_ids) {
        if (!writer.write_id(id)) return false;
    }
    writer.write_f32(body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds);
    if (state.discovered_system_ids.size() > std::numeric_limits<uint16_t>::max()) return false;
    writer.write_u16(static_cast<uint16_t>(state.discovered_system_ids.size()));
    for (const SourceLawId& id : state.discovered_system_ids) {
        if (!writer.write_id(id)) return false;
    }
    return true;
}

[[nodiscard]] bool read_body(PayloadReader& reader, PlayerSourceLawState& state) {
    uint32_t magic = 0;
    uint16_t version = 0;
    if (!reader.read_u32(magic) || !reader.read_u16(version) || magic != kPayloadMagic ||
        version != kPayloadVersion) {
        return false;
    }
    SourceLawBodyState& body = state.body;
    uint8_t stage = 0;
    if (!reader.read_id(body.active_path_id) || !reader.read_u8(stage) ||
        !reader.read_u16(body.source_level) || !reader.read_i32(body.source_reserve_current) ||
        !reader.read_i32(body.source_reserve_max) || !reader.read_f32(body.source_throughput) ||
        !reader.read_i32(body.mana_current) || !reader.read_i32(body.mana_max) ||
        !reader.read_f32(body.stability) || !reader.read_f32(body.mutation) ||
        !reader.read_i32(body.mental_load)) {
        return false;
    }
    body.stage = static_cast<SourceBodyStage>(stage);
    for (std::optional<OrganInstance>& organ : body.organs) {
        uint8_t exists = 0;
        if (!reader.read_u8(exists) || exists > 1U) return false;
        if (exists == 0U) {
            organ.reset();
            continue;
        }
        OrganInstance decoded;
        uint8_t tag_count = 0;
        if (!reader.read_id(decoded.definition_id) || !reader.read_u16(decoded.growth_level) ||
            !reader.read_id(decoded.quality_id) || !reader.read_f32(decoded.contamination) ||
            !reader.read_f32(decoded.integrity) || !reader.read_u8(tag_count) ||
            tag_count > kMaxTuningTags) {
            return false;
        }
        decoded.tuning_tags.reserve(tag_count);
        for (uint8_t index = 0; index < tag_count; ++index) {
            SourceLawId tag;
            if (!reader.read_id(tag)) return false;
            decoded.tuning_tags.push_back(std::move(tag));
        }
        organ = std::move(decoded);
    }
    uint8_t has_primary = 0;
    if (!reader.read_u8(has_primary) || has_primary > 1U) return false;
    if (has_primary != 0U) {
        SourceLawId primary;
        if (!reader.read_id(primary)) return false;
        body.circuit_schedule.current_primary_circuit_system_id = std::move(primary);
    } else {
        body.circuit_schedule.current_primary_circuit_system_id.reset();
    }
    uint8_t coordinating_count = 0;
    if (!reader.read_u8(coordinating_count) || coordinating_count > kMaxCoordinatingCircuits) {
        return false;
    }
    body.circuit_schedule.coordinating_circuit_system_ids.clear();
    body.circuit_schedule.coordinating_circuit_system_ids.reserve(coordinating_count);
    for (uint8_t index = 0; index < coordinating_count; ++index) {
        SourceLawId system_id;
        if (!reader.read_id(system_id)) return false;
        body.circuit_schedule.coordinating_circuit_system_ids.push_back(std::move(system_id));
    }
    if (!reader.read_f32(body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds)) {
        return false;
    }
    uint16_t discovered_count = 0;
    if (!reader.read_u16(discovered_count) || discovered_count > kMaxDiscoveredSystems) return false;
    state.discovered_system_ids.clear();
    state.discovered_system_ids.reserve(discovered_count);
    for (uint16_t index = 0; index < discovered_count; ++index) {
        SourceLawId system_id;
        if (!reader.read_id(system_id)) return false;
        state.discovered_system_ids.push_back(std::move(system_id));
    }
    return reader.at_end();
}

}  // namespace

snt::core::Expected<GamePlayerOrganState> SourceLawPersistenceCodec::encode(
    const PlayerSourceLawState& state) const {
    if (auto result = validate_player_state(state); !result) return result.error();
    PayloadWriter writer;
    if (!write_body(writer, state)) {
        return invalid_state("Source-law player state could not be encoded");
    }
    std::vector<std::byte> payload = std::move(writer).finish();
    if (payload.size() > kMaxPayloadBytes) {
        return invalid_state("Source-law player payload exceeds its current-format size limit");
    }
    return GamePlayerOrganState{
        .schema_id = std::string{kSourceLawPlayerOrganSchemaId},
        .schema_version = kSourceLawPlayerOrganSchemaVersion,
        .payload = std::move(payload),
    };
}

snt::core::Expected<PlayerSourceLawState> SourceLawPersistenceCodec::decode(
    const GamePlayerOrganState& state) const {
    if (state.schema_id.empty()) {
        if (state.schema_version != 0 || !state.payload.empty()) {
            return invalid_argument("Source-law player state has an incomplete absent schema declaration");
        }
        return PlayerSourceLawState{};
    }
    if (state.schema_id != kSourceLawPlayerOrganSchemaId ||
        state.schema_version != kSourceLawPlayerOrganSchemaVersion ||
        state.payload.empty() || state.payload.size() > kMaxPayloadBytes) {
        return invalid_argument("Source-law player state uses an unsupported current-format schema");
    }
    PlayerSourceLawState decoded;
    PayloadReader reader{state.payload};
    if (!read_body(reader, decoded)) {
        return invalid_argument("Source-law player payload is malformed or truncated");
    }
    if (auto result = validate_player_state(decoded); !result) return result.error();
    return decoded;
}

snt::core::Expected<void> SourceLawPersistenceCodec::validate_organ_state(
    const GamePlayerOrganState& state) const {
    const auto decoded = decode(state);
    if (!decoded) return decoded.error();
    return {};
}

}  // namespace snt::game::source_law
