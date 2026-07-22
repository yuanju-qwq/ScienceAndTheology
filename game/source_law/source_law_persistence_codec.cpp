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
#include <tuple>
#include <utility>
#include <vector>

namespace snt::game::source_law {
namespace {

constexpr uint32_t kPayloadMagic = 0x32424c53U;  // "SLB2" in little-endian bytes.
constexpr uint16_t kPayloadVersion = 2;
constexpr size_t kMaxPayloadBytes = 64U * 1024U;
constexpr size_t kMaxIdBytes = 192;
constexpr size_t kMaxTuningTags = 32;
constexpr size_t kMaxCoordinatingCircuits = 7;
constexpr size_t kMaxDiscoveredSystems = 256;
constexpr size_t kMaxPersonalSpellPrograms = 64;
constexpr size_t kMaxSpellDisplayNameBytes = 96;
constexpr size_t kMaxSpellGraphNodes = 128;
constexpr size_t kMaxSpellGraphLinks = 512;
constexpr size_t kMaxSpellNodeParameters = 32;
constexpr size_t kMaxSpellGraphDeclaredSystems = 8;
constexpr uint16_t kMaxSpellControlSteps = 256;
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

    void write_u64(uint64_t value) {
        for (uint8_t shift = 0; shift < 64; shift += 8) {
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

    [[nodiscard]] bool read_u64(uint64_t& value) {
        value = 0;
        for (uint8_t shift = 0; shift < 64; shift += 8) {
            uint8_t part = 0;
            if (!read_u8(part)) return false;
            value |= static_cast<uint64_t>(part) << shift;
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

[[nodiscard]] bool validate_spell_graph_for_persistence(const SourceLawSpellGraph& graph) {
    if (!is_valid_source_law_spell_graph_kind(graph.kind) || graph.nodes.empty() ||
        graph.nodes.size() > kMaxSpellGraphNodes || graph.links.size() > kMaxSpellGraphLinks ||
        graph.declared_max_control_steps > kMaxSpellControlSteps ||
        graph.declared_primary_system_ids.size() > kMaxSpellGraphDeclaredSystems ||
        graph.declared_coordinating_system_ids.size() > kMaxSpellGraphDeclaredSystems ||
        !is_unique_valid_ids(graph.required_path_core_ids, kMaxSpellGraphNodes) ||
        !is_unique_valid_ids(graph.requested_hybrid_link_ids, kMaxSpellGraphNodes) ||
        !is_unique_valid_ids(graph.declared_primary_system_ids, kMaxSpellGraphDeclaredSystems) ||
        !is_unique_valid_ids(graph.declared_coordinating_system_ids,
                             kMaxSpellGraphDeclaredSystems)) {
        return false;
    }
    std::set<uint32_t> node_ids;
    for (const SourceLawSpellNode& node : graph.nodes) {
        if (node.stable_node_id == 0 || !node_ids.insert(node.stable_node_id).second ||
            !is_valid_source_law_spell_node_kind(node.kind) || !is_valid_id(node.definition_id) ||
            node.parameter_ids.size() > kMaxSpellNodeParameters ||
            !is_unique_valid_ids(node.parameter_ids, kMaxSpellNodeParameters)) {
            return false;
        }
    }
    std::set<std::tuple<uint32_t, SourceLawId, uint32_t, SourceLawId>> links;
    for (const SourceLawSpellLink& link : graph.links) {
        if (!node_ids.contains(link.from_node_id) || !node_ids.contains(link.to_node_id) ||
            !is_valid_id(link.from_port_id) || !is_valid_id(link.to_port_id) ||
            !links.emplace(link.from_node_id, link.from_port_id, link.to_node_id,
                           link.to_port_id).second) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_id_vector(PayloadWriter& writer,
                                   const std::vector<SourceLawId>& values,
                                   uint16_t maximum) {
    if (values.size() > maximum || values.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    writer.write_u16(static_cast<uint16_t>(values.size()));
    for (const SourceLawId& value : values) {
        if (!writer.write_id(value)) return false;
    }
    return true;
}

[[nodiscard]] bool read_id_vector(PayloadReader& reader,
                                  std::vector<SourceLawId>& values,
                                  uint16_t maximum) {
    uint16_t count = 0;
    if (!reader.read_u16(count) || count > maximum) return false;
    values.clear();
    values.reserve(count);
    for (uint16_t index = 0; index < count; ++index) {
        SourceLawId value;
        if (!reader.read_id(value)) return false;
        values.push_back(std::move(value));
    }
    return true;
}

[[nodiscard]] bool write_spell_graph(PayloadWriter& writer, const SourceLawSpellGraph& graph) {
    if (!validate_spell_graph_for_persistence(graph) ||
        graph.nodes.size() > std::numeric_limits<uint16_t>::max() ||
        graph.links.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    writer.write_u8(static_cast<uint8_t>(graph.kind));
    writer.write_u16(static_cast<uint16_t>(graph.nodes.size()));
    for (const SourceLawSpellNode& node : graph.nodes) {
        if (node.parameter_ids.size() > std::numeric_limits<uint16_t>::max()) return false;
        writer.write_u32(node.stable_node_id);
        writer.write_u8(static_cast<uint8_t>(node.kind));
        if (!writer.write_id(node.definition_id) ||
            !write_id_vector(writer, node.parameter_ids,
                             static_cast<uint16_t>(kMaxSpellNodeParameters))) {
            return false;
        }
    }
    writer.write_u16(static_cast<uint16_t>(graph.links.size()));
    for (const SourceLawSpellLink& link : graph.links) {
        writer.write_u32(link.from_node_id);
        if (!writer.write_id(link.from_port_id)) return false;
        writer.write_u32(link.to_node_id);
        if (!writer.write_id(link.to_port_id)) return false;
    }
    if (!write_id_vector(writer, graph.required_path_core_ids,
                         static_cast<uint16_t>(kMaxSpellGraphNodes)) ||
        !write_id_vector(writer, graph.requested_hybrid_link_ids,
                         static_cast<uint16_t>(kMaxSpellGraphNodes)) ||
        !write_id_vector(writer, graph.declared_primary_system_ids,
                         static_cast<uint16_t>(kMaxSpellGraphDeclaredSystems)) ||
        !write_id_vector(writer, graph.declared_coordinating_system_ids,
                         static_cast<uint16_t>(kMaxSpellGraphDeclaredSystems))) {
        return false;
    }
    writer.write_u16(graph.declared_max_control_steps);
    return true;
}

[[nodiscard]] bool read_spell_graph(PayloadReader& reader, SourceLawSpellGraph& graph) {
    uint8_t graph_kind = 0;
    uint16_t node_count = 0;
    if (!reader.read_u8(graph_kind) || !reader.read_u16(node_count) ||
        node_count == 0 || node_count > kMaxSpellGraphNodes) {
        return false;
    }
    graph = {};
    graph.kind = static_cast<SourceLawSpellGraphKind>(graph_kind);
    graph.nodes.reserve(node_count);
    for (uint16_t index = 0; index < node_count; ++index) {
        SourceLawSpellNode node;
        uint8_t node_kind = 0;
        if (!reader.read_u32(node.stable_node_id) || !reader.read_u8(node_kind) ||
            !reader.read_id(node.definition_id) ||
            !read_id_vector(reader, node.parameter_ids,
                            static_cast<uint16_t>(kMaxSpellNodeParameters))) {
            return false;
        }
        node.kind = static_cast<SourceLawSpellNodeKind>(node_kind);
        graph.nodes.push_back(std::move(node));
    }
    uint16_t link_count = 0;
    if (!reader.read_u16(link_count) || link_count > kMaxSpellGraphLinks) return false;
    graph.links.reserve(link_count);
    for (uint16_t index = 0; index < link_count; ++index) {
        SourceLawSpellLink link;
        if (!reader.read_u32(link.from_node_id) || !reader.read_id(link.from_port_id) ||
            !reader.read_u32(link.to_node_id) || !reader.read_id(link.to_port_id)) {
            return false;
        }
        graph.links.push_back(std::move(link));
    }
    if (!read_id_vector(reader, graph.required_path_core_ids,
                        static_cast<uint16_t>(kMaxSpellGraphNodes)) ||
        !read_id_vector(reader, graph.requested_hybrid_link_ids,
                        static_cast<uint16_t>(kMaxSpellGraphNodes)) ||
        !read_id_vector(reader, graph.declared_primary_system_ids,
                        static_cast<uint16_t>(kMaxSpellGraphDeclaredSystems)) ||
        !read_id_vector(reader, graph.declared_coordinating_system_ids,
                        static_cast<uint16_t>(kMaxSpellGraphDeclaredSystems)) ||
        !reader.read_u16(graph.declared_max_control_steps)) {
        return false;
    }
    return validate_spell_graph_for_persistence(graph);
}

[[nodiscard]] bool validate_spell_program_for_persistence(
    const PlayerSourceLawSpellProgram& program) {
    return program.program_id.value != 0 &&
           (!program.copied_from_graph_id || is_valid_id(*program.copied_from_graph_id)) &&
           !program.display_name.empty() &&
           program.display_name.size() <= kMaxSpellDisplayNameBytes &&
           program.display_name.find('\0') == std::string::npos &&
           program.source_revision != 0 &&
           program.graph.kind == SourceLawSpellGraphKind::kPlayerAuthored &&
           validate_spell_graph_for_persistence(program.graph);
}

[[nodiscard]] bool write_spell_program(PayloadWriter& writer,
                                        const PlayerSourceLawSpellProgram& program) {
    if (!validate_spell_program_for_persistence(program)) return false;
    writer.write_u64(program.program_id.value);
    writer.write_u8(program.copied_from_graph_id ? 1U : 0U);
    if (program.copied_from_graph_id && !writer.write_id(*program.copied_from_graph_id)) {
        return false;
    }
    if (!writer.write_id(program.display_name)) return false;
    writer.write_u32(program.source_revision);
    return write_spell_graph(writer, program.graph);
}

[[nodiscard]] bool read_spell_program(PayloadReader& reader,
                                       PlayerSourceLawSpellProgram& program) {
    uint8_t has_source = 0;
    if (!reader.read_u64(program.program_id.value) || !reader.read_u8(has_source) || has_source > 1U) {
        return false;
    }
    if (has_source != 0U) {
        SourceLawId source_id;
        if (!reader.read_id(source_id)) return false;
        program.copied_from_graph_id = std::move(source_id);
    } else {
        program.copied_from_graph_id.reset();
    }
    if (!reader.read_id(program.display_name, kMaxSpellDisplayNameBytes) ||
        !reader.read_u32(program.source_revision) || !read_spell_graph(reader, program.graph)) {
        return false;
    }
    return validate_spell_program_for_persistence(program);
}

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
        !is_unique_valid_ids(state.discovered_system_ids, kMaxDiscoveredSystems) ||
        state.personal_spell_programs.size() > kMaxPersonalSpellPrograms) {
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
    std::set<uint64_t> program_ids;
    for (const PlayerSourceLawSpellProgram& program : state.personal_spell_programs) {
        if (!validate_spell_program_for_persistence(program) ||
            !program_ids.insert(program.program_id.value).second) {
            return invalid_argument("Source-law personal spell program is invalid for persistence");
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
    writer.write_u64(body.body_revision);
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
    if (state.personal_spell_programs.size() > std::numeric_limits<uint8_t>::max()) {
        return false;
    }
    writer.write_u8(static_cast<uint8_t>(state.personal_spell_programs.size()));
    for (const PlayerSourceLawSpellProgram& program : state.personal_spell_programs) {
        if (!write_spell_program(writer, program)) return false;
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
        !reader.read_u16(body.source_level) || !reader.read_u64(body.body_revision) ||
        !reader.read_i32(body.source_reserve_current) ||
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
    uint8_t program_count = 0;
    if (!reader.read_u8(program_count) || program_count > kMaxPersonalSpellPrograms) return false;
    state.personal_spell_programs.clear();
    state.personal_spell_programs.reserve(program_count);
    for (uint8_t index = 0; index < program_count; ++index) {
        PlayerSourceLawSpellProgram program;
        if (!read_spell_program(reader, program)) return false;
        state.personal_spell_programs.push_back(std::move(program));
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
