// Automation-controller presentation replication implementation.

#include "game/network/game_automation_controller_replication.h"

#include "core/error.h"

#include <bit>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxControllerKeyBytes = 256;
constexpr size_t kMaxEndpointAddressBytes = 512;
constexpr size_t kMaxFlowNodes = 1024;
constexpr size_t kMaxFlowConnections = 4096;

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

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

void append_i64(std::vector<std::byte>& bytes, int64_t value) {
    append_u64(bytes, std::bit_cast<uint64_t>(value));
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

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool is_known_node_type(SfmFlowNodeType type) noexcept {
    return type == SfmFlowNodeType::kInterval || type == SfmFlowNodeType::kTransfer;
}

[[nodiscard]] uint64_t connection_key(SfmFlowNodeId source,
                                      SfmFlowNodeId destination) noexcept {
    return (static_cast<uint64_t>(source) << 32u) | destination;
}

[[nodiscard]] size_t encoded_string_size(std::string_view value) noexcept {
    return sizeof(uint16_t) + value.size();
}

[[nodiscard]] size_t encoded_content_stack_size(
    const ResourceContentStack& stack) noexcept {
    return encoded_string_size(stack.key.type) + encoded_string_size(stack.key.id) +
        encoded_string_size(stack.key.variant) + sizeof(int64_t);
}

[[nodiscard]] snt::core::Expected<void> append_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if ((require_non_empty && value.empty()) || value.size() > maximum ||
        value.size() > std::numeric_limits<uint16_t>::max() || has_embedded_nul(value)) {
        return protocol_error(std::string("Automation controller ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Automation controller ") + field_name + " is truncated");
    }
    const size_t length = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && length == 0) || length > maximum ||
        bytes.size() - offset < length) {
        return protocol_error(std::string("Automation controller ") + field_name + " is invalid");
    }
    std::string value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += length;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("Automation controller ") + field_name + " contains NUL");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> append_chunk(
    std::vector<std::byte>& bytes, const snt::voxel::ChunkKey& chunk) {
    if (auto result = append_string(bytes, chunk.dimension_id, kMaxGameDimensionIdBytes,
                                    "anchor dimension", true);
        !result) {
        return result.error();
    }
    append_i32(bytes, chunk.chunk_x);
    append_i32(bytes, chunk.chunk_y);
    append_i32(bytes, chunk.chunk_z);
    return {};
}

[[nodiscard]] snt::core::Expected<snt::voxel::ChunkKey> read_chunk(
    std::span<const std::byte> bytes, size_t& offset) {
    auto dimension_id = read_string(bytes, offset, kMaxGameDimensionIdBytes,
                                    "anchor dimension", true);
    if (!dimension_id) return dimension_id.error();
    if (bytes.size() - offset < sizeof(uint32_t) * 3) {
        return protocol_error("Automation controller anchor chunk is truncated");
    }
    const int32_t chunk_x = read_i32(bytes, offset);
    const int32_t chunk_y = read_i32(bytes, offset + sizeof(uint32_t));
    const int32_t chunk_z = read_i32(bytes, offset + sizeof(uint32_t) * 2);
    offset += sizeof(uint32_t) * 3;
    return snt::voxel::ChunkKey{
        std::move(*dimension_id), chunk_x, chunk_y, chunk_z};
}

[[nodiscard]] snt::core::Expected<void> append_content_stack(
    std::vector<std::byte>& bytes, const ResourceContentStack& stack) {
    if (!stack.is_valid()) {
        return protocol_error("Automation controller transfer stack is invalid");
    }
    if (auto result = append_string(bytes, stack.key.type, kMaxGameResourceTypeBytes,
                                    "transfer resource type", true);
        !result) {
        return result.error();
    }
    if (auto result = append_string(bytes, stack.key.id, kMaxGameResourceIdBytes,
                                    "transfer resource id", true);
        !result) {
        return result.error();
    }
    if (auto result = append_string(bytes, stack.key.variant, kMaxGameResourceVariantBytes,
                                    "transfer resource variant", false);
        !result) {
        return result.error();
    }
    append_i64(bytes, stack.amount);
    return {};
}

[[nodiscard]] snt::core::Expected<ResourceContentStack> read_content_stack(
    std::span<const std::byte> bytes, size_t& offset) {
    auto type = read_string(bytes, offset, kMaxGameResourceTypeBytes,
                            "transfer resource type", true);
    if (!type) return type.error();
    auto id = read_string(bytes, offset, kMaxGameResourceIdBytes,
                          "transfer resource id", true);
    if (!id) return id.error();
    auto variant = read_string(bytes, offset, kMaxGameResourceVariantBytes,
                               "transfer resource variant", false);
    if (!variant) return variant.error();
    if (bytes.size() - offset < sizeof(uint64_t)) {
        return protocol_error("Automation controller transfer amount is truncated");
    }
    ResourceContentStack stack{
        .key = {.type = std::move(*type), .id = std::move(*id), .variant = std::move(*variant)},
        .amount = read_i64(bytes, offset),
    };
    offset += sizeof(uint64_t);
    if (!stack.is_valid()) {
        return protocol_error("Automation controller transfer stack is invalid");
    }
    return stack;
}

[[nodiscard]] snt::core::Expected<void> validate_state(
    const GameAutomationControllerReplicationState& state) {
    if (state.anchor_chunk.dimension_id.empty() ||
        state.anchor_chunk.dimension_id.size() > kMaxGameDimensionIdBytes ||
        has_embedded_nul(state.anchor_chunk.dimension_id) || state.anchor_entity_id == 0 ||
        state.controller_key.empty() || state.controller_key.size() > kMaxControllerKeyBytes ||
        has_embedded_nul(state.controller_key) || state.authoritative_revision == 0 ||
        state.sfm_program.nodes.size() > kMaxFlowNodes ||
        state.sfm_program.connections.size() > kMaxFlowConnections) {
        return protocol_error("Automation controller replication state header is invalid");
    }

    std::unordered_map<SfmFlowNodeId, SfmFlowNodeType> node_types;
    node_types.reserve(state.sfm_program.nodes.size());
    for (const SfmFlowNodeRecord& node : state.sfm_program.nodes) {
        if (node.id == kInvalidSfmFlowNodeId || !is_known_node_type(node.type) ||
            !node_types.emplace(node.id, node.type).second) {
            return protocol_error("Automation controller replication has invalid flow nodes");
        }
        if (node.type == SfmFlowNodeType::kInterval) {
            if (node.interval_ticks == 0 || node.transfer.is_valid()) {
                return protocol_error("Automation controller replication interval node is invalid");
            }
        } else if (node.interval_ticks != 0 || !node.transfer.is_valid() ||
                   node.transfer.source.value.size() > kMaxEndpointAddressBytes ||
                   node.transfer.destination.value.size() > kMaxEndpointAddressBytes ||
                   has_embedded_nul(node.transfer.source.value) ||
                   has_embedded_nul(node.transfer.destination.value)) {
            return protocol_error("Automation controller replication transfer node is invalid");
        }
    }
    std::unordered_set<uint64_t> connections;
    connections.reserve(state.sfm_program.connections.size());
    for (const SfmFlowConnectionRecord& connection : state.sfm_program.connections) {
        const auto source = node_types.find(connection.source);
        const auto destination = node_types.find(connection.destination);
        if (source == node_types.end() || destination == node_types.end() ||
            destination->second == SfmFlowNodeType::kInterval ||
            !connections.insert(connection_key(connection.source, connection.destination)).second) {
            return protocol_error("Automation controller replication connection is invalid");
        }
    }
    return {};
}

struct ReplicationValueLookup {
    const GameReplicationValue* value = nullptr;
    bool invalid = false;
};

[[nodiscard]] ReplicationValueLookup find_value(
    const std::vector<GameReplicationValue>& values, bool snapshot) {
    ReplicationValueLookup result;
    for (const GameReplicationValue& value : values) {
        if (value.kind != GameReplicationValueKind::kAutomationControllers) continue;
        if (result.value != nullptr ||
            (snapshot && value.operation != GameReplicationValueOperation::kUpsert)) {
            result.invalid = true;
            return result;
        }
        result.value = &value;
    }
    return result;
}

}  // namespace

snt::core::Expected<size_t>
measure_game_automation_controller_replication_state(
    const GameAutomationControllerReplicationState& state) {
    if (auto result = validate_state(state); !result) return result.error();

    size_t bytes = encoded_string_size(state.anchor_chunk.dimension_id) +
        sizeof(uint32_t) * 3 + sizeof(uint64_t) + sizeof(uint32_t) * 3 +
        encoded_string_size(state.controller_key) + sizeof(uint64_t) +
        sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint16_t) +
        sizeof(uint16_t);
    for (const SfmFlowNodeRecord& node : state.sfm_program.nodes) {
        bytes += sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
        if (node.type == SfmFlowNodeType::kTransfer) {
            bytes += encoded_string_size(node.transfer.source.value) +
                encoded_string_size(node.transfer.destination.value) +
                encoded_content_stack_size(node.transfer.requested);
        }
    }
    bytes += state.sfm_program.connections.size() * sizeof(uint32_t) * 2;
    return bytes;
}

snt::core::Expected<std::vector<std::byte>>
encode_game_automation_controller_replication_snapshot(
    const GameAutomationControllerReplicationSnapshot& snapshot) {
    if (snapshot.controllers.size() > kMaxGameAutomationControllerStates) {
        return protocol_error("Automation controller snapshot has too many controllers");
    }
    std::unordered_set<uint64_t> anchor_ids;
    anchor_ids.reserve(snapshot.controllers.size());
    size_t encoded_size = kGameAutomationControllerReplicationHeaderBytes;
    for (const GameAutomationControllerReplicationState& state : snapshot.controllers) {
        auto state_size = measure_game_automation_controller_replication_state(state);
        if (!state_size) return state_size.error();
        if (!anchor_ids.insert(state.anchor_entity_id).second) {
            return protocol_error("Automation controller snapshot has duplicate anchors");
        }
        if (*state_size >
            kMaxGameAutomationControllerReplicationPayloadBytes - encoded_size) {
            return protocol_error("Automation controller snapshot exceeds its value payload budget");
        }
        encoded_size += *state_size;
    }

    anchor_ids.clear();
    std::vector<std::byte> bytes;
    bytes.reserve(encoded_size);
    bytes.push_back(static_cast<std::byte>(kGameAutomationControllerReplicationVersion));
    append_u16(bytes, static_cast<uint16_t>(snapshot.controllers.size()));
    for (const GameAutomationControllerReplicationState& state : snapshot.controllers) {
        if (!anchor_ids.insert(state.anchor_entity_id).second) {
            return protocol_error("Automation controller snapshot has duplicate anchors");
        }
        if (auto result = append_chunk(bytes, state.anchor_chunk); !result) return result.error();
        append_u64(bytes, state.anchor_entity_id);
        append_i32(bytes, state.root_x);
        append_i32(bytes, state.root_y);
        append_i32(bytes, state.root_z);
        if (auto result = append_string(bytes, state.controller_key, kMaxControllerKeyBytes,
                                        "controller key", true);
            !result) {
            return result.error();
        }
        append_u64(bytes, state.authoritative_revision);
        bytes.push_back(state.online ? std::byte{1} : std::byte{0});
        append_u64(bytes, state.sfm_program.revision);
        append_u16(bytes, static_cast<uint16_t>(state.sfm_program.nodes.size()));
        for (const SfmFlowNodeRecord& node : state.sfm_program.nodes) {
            append_u32(bytes, node.id);
            bytes.push_back(static_cast<std::byte>(node.type));
            append_u32(bytes, node.interval_ticks);
            if (node.type == SfmFlowNodeType::kTransfer) {
                if (auto result = append_string(bytes, node.transfer.source.value,
                                                kMaxEndpointAddressBytes,
                                                "transfer source endpoint", true);
                    !result) {
                    return result.error();
                }
                if (auto result = append_string(bytes, node.transfer.destination.value,
                                                kMaxEndpointAddressBytes,
                                                "transfer destination endpoint", true);
                    !result) {
                    return result.error();
                }
                if (auto result = append_content_stack(bytes, node.transfer.requested); !result) {
                    return result.error();
                }
            }
        }
        append_u16(bytes, static_cast<uint16_t>(state.sfm_program.connections.size()));
        for (const SfmFlowConnectionRecord& connection : state.sfm_program.connections) {
            append_u32(bytes, connection.source);
            append_u32(bytes, connection.destination);
        }
    }
    return bytes;
}

snt::core::Expected<GameAutomationControllerReplicationSnapshot>
decode_game_automation_controller_replication_snapshot(std::span<const std::byte> payload) {
    if (payload.size() > kMaxGameAutomationControllerReplicationPayloadBytes ||
        payload.size() < kGameAutomationControllerReplicationHeaderBytes ||
        std::to_integer<uint8_t>(payload.front()) !=
            kGameAutomationControllerReplicationVersion) {
        return protocol_error("Automation controller snapshot version is invalid");
    }
    size_t offset = sizeof(uint8_t);
    const size_t controller_count = read_u16(payload, offset);
    offset += sizeof(uint16_t);
    if (controller_count > kMaxGameAutomationControllerStates) {
        return protocol_error("Automation controller snapshot has too many controllers");
    }
    GameAutomationControllerReplicationSnapshot snapshot;
    snapshot.controllers.reserve(controller_count);
    std::unordered_set<uint64_t> anchor_ids;
    anchor_ids.reserve(controller_count);
    for (size_t controller_index = 0; controller_index < controller_count; ++controller_index) {
        auto chunk = read_chunk(payload, offset);
        if (!chunk) return chunk.error();
        if (payload.size() - offset < sizeof(uint64_t) + sizeof(uint32_t) * 3) {
            return protocol_error("Automation controller state is truncated");
        }
        GameAutomationControllerReplicationState state;
        state.anchor_chunk = std::move(*chunk);
        state.anchor_entity_id = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        state.root_x = read_i32(payload, offset);
        state.root_y = read_i32(payload, offset + sizeof(uint32_t));
        state.root_z = read_i32(payload, offset + sizeof(uint32_t) * 2);
        offset += sizeof(uint32_t) * 3;
        auto controller_key = read_string(payload, offset, kMaxControllerKeyBytes,
                                          "controller key", true);
        if (!controller_key) return controller_key.error();
        state.controller_key = std::move(*controller_key);
        if (payload.size() - offset < sizeof(uint64_t) + sizeof(uint8_t) +
                                       sizeof(uint64_t) + sizeof(uint16_t)) {
            return protocol_error("Automation controller program header is truncated");
        }
        state.authoritative_revision = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        const uint8_t online = std::to_integer<uint8_t>(payload[offset++]);
        if (online > 1) return protocol_error("Automation controller online state is invalid");
        state.online = online != 0;
        state.sfm_program.revision = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        const size_t node_count = read_u16(payload, offset);
        offset += sizeof(uint16_t);
        if (node_count > kMaxFlowNodes) {
            return protocol_error("Automation controller program has too many nodes");
        }
        state.sfm_program.nodes.reserve(node_count);
        for (size_t node_index = 0; node_index < node_count; ++node_index) {
            if (payload.size() - offset < sizeof(uint32_t) + sizeof(uint8_t) +
                                           sizeof(uint32_t)) {
                return protocol_error("Automation controller flow node is truncated");
            }
            SfmFlowNodeRecord node;
            node.id = read_u32(payload, offset);
            offset += sizeof(uint32_t);
            node.type = static_cast<SfmFlowNodeType>(
                std::to_integer<uint8_t>(payload[offset++]));
            node.interval_ticks = read_u32(payload, offset);
            offset += sizeof(uint32_t);
            if (!is_known_node_type(node.type)) {
                return protocol_error("Automation controller flow node type is invalid");
            }
            if (node.type == SfmFlowNodeType::kTransfer) {
                auto source = read_string(payload, offset, kMaxEndpointAddressBytes,
                                          "transfer source endpoint", true);
                if (!source) return source.error();
                auto destination = read_string(payload, offset, kMaxEndpointAddressBytes,
                                               "transfer destination endpoint", true);
                if (!destination) return destination.error();
                auto requested = read_content_stack(payload, offset);
                if (!requested) return requested.error();
                node.transfer = {
                    .source = {.value = std::move(*source)},
                    .destination = {.value = std::move(*destination)},
                    .requested = std::move(*requested),
                };
            }
            state.sfm_program.nodes.push_back(std::move(node));
        }
        if (payload.size() - offset < sizeof(uint16_t)) {
            return protocol_error("Automation controller connection count is truncated");
        }
        const size_t connection_count = read_u16(payload, offset);
        offset += sizeof(uint16_t);
        if (connection_count > kMaxFlowConnections ||
            payload.size() - offset < connection_count * sizeof(uint32_t) * 2) {
            return protocol_error("Automation controller connections are invalid");
        }
        state.sfm_program.connections.reserve(connection_count);
        for (size_t connection_index = 0; connection_index < connection_count;
             ++connection_index) {
            state.sfm_program.connections.push_back({
                .source = read_u32(payload, offset),
                .destination = read_u32(payload, offset + sizeof(uint32_t)),
            });
            offset += sizeof(uint32_t) * 2;
        }
        if (auto result = validate_state(state); !result) return result.error();
        if (!anchor_ids.insert(state.anchor_entity_id).second) {
            return protocol_error("Automation controller snapshot has duplicate anchors");
        }
        snapshot.controllers.push_back(std::move(state));
    }
    if (offset != payload.size()) {
        return protocol_error("Automation controller snapshot has trailing bytes");
    }
    return snapshot;
}

size_t GameRemoteAutomationControllerWorld::PositionHash::operator()(
    const Position& position) const noexcept {
    size_t hash = std::hash<std::string>{}(position.dimension_id);
    const auto mix = [&hash](int32_t value) {
        hash ^= std::hash<int32_t>{}(value) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
    };
    mix(position.root_x);
    mix(position.root_y);
    mix(position.root_z);
    return hash;
}

snt::core::Expected<void> GameRemoteAutomationControllerWorld::apply(
    const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Remote automation controller world received an invalid snapshot id");
    }
    const ReplicationValueLookup lookup = find_value(snapshot.values, true);
    if (lookup.invalid) {
        return protocol_error("Remote automation controller snapshot has an invalid value record");
    }
    const GameReplicationValue* const value = lookup.value;
    if (value == nullptr) {
        controllers_.clear();
        anchors_by_position_.clear();
    } else {
        auto decoded = decode_game_automation_controller_replication_snapshot(value->payload);
        if (!decoded) return decoded.error();
        if (auto result = replace_current_set(*decoded); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemoteAutomationControllerWorld::apply(
    const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Remote automation controller delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Remote automation controller delta sequence is invalid");
    }
    const ReplicationValueLookup lookup = find_value(delta.values, false);
    if (lookup.invalid) {
        return protocol_error("Remote automation controller delta has duplicate value records");
    }
    const GameReplicationValue* const value = lookup.value;
    if (value != nullptr) {
        switch (value->operation) {
            case GameReplicationValueOperation::kUpsert: {
                auto decoded = decode_game_automation_controller_replication_snapshot(value->payload);
                if (!decoded) return decoded.error();
                if (auto result = replace_current_set(*decoded); !result) return result.error();
                break;
            }
            case GameReplicationValueOperation::kRemove:
                if (!value->payload.empty()) {
                    return protocol_error("Remote automation controller remove carries a payload");
                }
                controllers_.clear();
                anchors_by_position_.clear();
                break;
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::optional<GameAutomationControllerReplicationState>
GameRemoteAutomationControllerWorld::find_controller(uint64_t anchor_entity_id) const {
    const auto found = controllers_.find(anchor_entity_id);
    return found == controllers_.end()
        ? std::nullopt
        : std::optional<GameAutomationControllerReplicationState>{found->second};
}

std::optional<GameAutomationControllerReplicationState>
GameRemoteAutomationControllerWorld::find_controller_at(
    std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const {
    const Position position{
        .dimension_id = std::string(dimension_id),
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
    };
    const auto indexed = anchors_by_position_.find(position);
    if (indexed == anchors_by_position_.end()) return std::nullopt;
    return find_controller(indexed->second);
}

std::vector<GameAutomationControllerReplicationState>
GameRemoteAutomationControllerWorld::controllers() const {
    std::vector<GameAutomationControllerReplicationState> result;
    result.reserve(controllers_.size());
    for (const auto& [anchor_id, controller] : controllers_) {
        static_cast<void>(anchor_id);
        result.push_back(controller);
    }
    return result;
}

void GameRemoteAutomationControllerWorld::clear() noexcept {
    controllers_.clear();
    anchors_by_position_.clear();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemoteAutomationControllerWorld::replace_current_set(
    const GameAutomationControllerReplicationSnapshot& snapshot) {
    if (snapshot.controllers.size() > kMaxGameAutomationControllerStates) {
        return protocol_error("Remote automation controller snapshot has too many states");
    }
    std::unordered_map<uint64_t, GameAutomationControllerReplicationState> next_controllers;
    std::unordered_map<Position, uint64_t, PositionHash> next_positions;
    next_controllers.reserve(snapshot.controllers.size());
    next_positions.reserve(snapshot.controllers.size());
    for (const GameAutomationControllerReplicationState& state : snapshot.controllers) {
        if (auto result = validate_state(state); !result) return result.error();
        Position position{
            .dimension_id = state.anchor_chunk.dimension_id,
            .root_x = state.root_x,
            .root_y = state.root_y,
            .root_z = state.root_z,
        };
        if (!next_controllers.emplace(state.anchor_entity_id, state).second ||
            !next_positions.emplace(std::move(position), state.anchor_entity_id).second) {
            return protocol_error("Remote automation controller snapshot has duplicate identity");
        }
    }
    controllers_ = std::move(next_controllers);
    anchors_by_position_ = std::move(next_positions);
    return {};
}

}  // namespace snt::game::replication
