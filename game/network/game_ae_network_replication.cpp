// AE physical-topology presentation replication implementation.

#include "game/network/game_ae_network_replication.h"

#include "core/error.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxDimensionBytes = kMaxGameDimensionIdBytes;
constexpr size_t kEncodedNodeFieldsBytes =
    sizeof(uint64_t) + sizeof(uint32_t) * 3 + sizeof(uint8_t) * 3 +
    sizeof(uint32_t) * 2 + sizeof(uint64_t) * 2 + sizeof(uint32_t) * 5 +
    sizeof(uint8_t);

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

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] size_t encoded_string_size(std::string_view value) noexcept {
    return sizeof(uint16_t) + value.size();
}

[[nodiscard]] snt::core::Expected<void> append_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name) {
    if (value.empty() || value.size() > maximum ||
        value.size() > std::numeric_limits<uint16_t>::max() || has_embedded_nul(value)) {
        return protocol_error(std::string("AE network ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("AE network ") + field_name + " is truncated");
    }
    const size_t length = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if (length == 0 || length > maximum || bytes.size() - offset < length) {
        return protocol_error(std::string("AE network ") + field_name + " is invalid");
    }
    std::string value;
    value.reserve(length);
    for (size_t index = 0; index < length; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += length;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("AE network ") + field_name + " contains NUL");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> append_chunk(
    std::vector<std::byte>& bytes, const snt::voxel::ChunkKey& chunk) {
    if (auto result = append_string(bytes, chunk.dimension_id, kMaxDimensionBytes,
                                    "anchor dimension");
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
    auto dimension = read_string(bytes, offset, kMaxDimensionBytes, "anchor dimension");
    if (!dimension) return dimension.error();
    if (bytes.size() - offset < sizeof(uint32_t) * 3) {
        return protocol_error("AE network anchor chunk is truncated");
    }
    const int32_t chunk_x = read_i32(bytes, offset);
    const int32_t chunk_y = read_i32(bytes, offset + sizeof(uint32_t));
    const int32_t chunk_z = read_i32(bytes, offset + sizeof(uint32_t) * 2);
    offset += sizeof(uint32_t) * 3;
    return snt::voxel::ChunkKey{std::move(*dimension), chunk_x, chunk_y, chunk_z};
}

[[nodiscard]] snt::core::Expected<void> validate_state(
    const GameAeNetworkReplicationState& state) {
    if (state.anchor_chunk.dimension_id.empty() ||
        state.anchor_chunk.dimension_id.size() > kMaxDimensionBytes ||
        has_embedded_nul(state.anchor_chunk.dimension_id) || state.anchor_entity_id == 0 ||
        !is_known_ae_network_node_type(state.type) || state.provided_channels < 0 ||
        (!ae_network_node_is_channel_provider(state.type) && state.provided_channels != 0) ||
        state.authoritative_revision == 0 || state.topology_revision == 0) {
        return protocol_error("AE network replication state header is invalid");
    }
    if (state.component_id == 0) {
        if (state.enabled || state.online || state.component_node_count != 0 ||
            state.component_controller_count != 0 || state.component_total_channels != 0 ||
            state.component_online_devices != 0 || state.component_offline_devices != 0 ||
            state.component_powered) {
            return protocol_error("AE network offline node has component state");
        }
        return {};
    }
    if (!state.enabled || (state.online && !state.component_powered) ||
        state.component_node_count == 0 || state.component_total_channels < 0 ||
        state.component_online_devices < 0 || state.component_offline_devices < 0 ||
        state.component_online_devices > static_cast<int32_t>(state.component_node_count) ||
        state.component_offline_devices > static_cast<int32_t>(state.component_node_count) ||
        state.component_online_devices + state.component_offline_devices >
            static_cast<int32_t>(state.component_node_count) ||
        state.component_powered != (state.component_controller_count == 1)) {
        return protocol_error("AE network component summary is invalid");
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
        if (value.kind != GameReplicationValueKind::kAeNetworkNodes) continue;
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
measure_game_ae_network_replication_state(const GameAeNetworkReplicationState& state) {
    if (auto result = validate_state(state); !result) return result.error();
    return encoded_string_size(state.anchor_chunk.dimension_id) + sizeof(uint32_t) * 3 +
        kEncodedNodeFieldsBytes;
}

snt::core::Expected<std::vector<std::byte>>
encode_game_ae_network_replication_snapshot(const GameAeNetworkReplicationSnapshot& snapshot) {
    if (snapshot.nodes.size() > kMaxGameAeNetworkReplicationStates) {
        return protocol_error("AE network snapshot has too many nodes");
    }
    size_t encoded_size = kGameAeNetworkReplicationHeaderBytes;
    std::unordered_set<uint64_t> anchors;
    anchors.reserve(snapshot.nodes.size());
    for (const GameAeNetworkReplicationState& state : snapshot.nodes) {
        auto state_size = measure_game_ae_network_replication_state(state);
        if (!state_size) return state_size.error();
        if (!anchors.insert(state.anchor_entity_id).second ||
            *state_size > kMaxGameAeNetworkReplicationPayloadBytes - encoded_size) {
            return protocol_error("AE network snapshot has invalid or oversized nodes");
        }
        encoded_size += *state_size;
    }

    std::vector<std::byte> bytes;
    bytes.reserve(encoded_size);
    bytes.push_back(static_cast<std::byte>(kGameAeNetworkReplicationVersion));
    append_u16(bytes, static_cast<uint16_t>(snapshot.nodes.size()));
    for (const GameAeNetworkReplicationState& state : snapshot.nodes) {
        if (auto result = append_chunk(bytes, state.anchor_chunk); !result) return result.error();
        append_u64(bytes, state.anchor_entity_id);
        append_i32(bytes, state.root_x);
        append_i32(bytes, state.root_y);
        append_i32(bytes, state.root_z);
        bytes.push_back(static_cast<std::byte>(state.type));
        bytes.push_back(state.enabled ? std::byte{1} : std::byte{0});
        bytes.push_back(state.online ? std::byte{1} : std::byte{0});
        append_u32(bytes, state.component_id);
        append_i32(bytes, state.provided_channels);
        append_u64(bytes, state.authoritative_revision);
        append_u64(bytes, state.topology_revision);
        append_u32(bytes, state.component_node_count);
        append_u32(bytes, state.component_controller_count);
        append_i32(bytes, state.component_total_channels);
        append_i32(bytes, state.component_online_devices);
        append_i32(bytes, state.component_offline_devices);
        bytes.push_back(state.component_powered ? std::byte{1} : std::byte{0});
    }
    return bytes;
}

snt::core::Expected<GameAeNetworkReplicationSnapshot>
decode_game_ae_network_replication_snapshot(std::span<const std::byte> payload) {
    if (payload.size() > kMaxGameAeNetworkReplicationPayloadBytes ||
        payload.size() < kGameAeNetworkReplicationHeaderBytes ||
        std::to_integer<uint8_t>(payload.front()) != kGameAeNetworkReplicationVersion) {
        return protocol_error("AE network snapshot version is invalid");
    }
    size_t offset = sizeof(uint8_t);
    const size_t count = read_u16(payload, offset);
    offset += sizeof(uint16_t);
    if (count > kMaxGameAeNetworkReplicationStates) {
        return protocol_error("AE network snapshot has too many nodes");
    }
    GameAeNetworkReplicationSnapshot snapshot;
    snapshot.nodes.reserve(count);
    std::unordered_set<uint64_t> anchors;
    anchors.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        auto chunk = read_chunk(payload, offset);
        if (!chunk) return chunk.error();
        if (payload.size() - offset < kEncodedNodeFieldsBytes) {
            return protocol_error("AE network node state is truncated");
        }
        GameAeNetworkReplicationState state;
        state.anchor_chunk = std::move(*chunk);
        state.anchor_entity_id = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        state.root_x = read_i32(payload, offset);
        state.root_y = read_i32(payload, offset + sizeof(uint32_t));
        state.root_z = read_i32(payload, offset + sizeof(uint32_t) * 2);
        offset += sizeof(uint32_t) * 3;
        state.type = static_cast<AeNetworkNodeType>(std::to_integer<uint8_t>(payload[offset++]));
        const uint8_t enabled = std::to_integer<uint8_t>(payload[offset++]);
        const uint8_t online = std::to_integer<uint8_t>(payload[offset++]);
        if (enabled > 1 || online > 1) {
            return protocol_error("AE network node flags are invalid");
        }
        state.enabled = enabled != 0;
        state.online = online != 0;
        state.component_id = read_u32(payload, offset);
        offset += sizeof(uint32_t);
        state.provided_channels = read_i32(payload, offset);
        offset += sizeof(uint32_t);
        state.authoritative_revision = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        state.topology_revision = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        state.component_node_count = read_u32(payload, offset);
        offset += sizeof(uint32_t);
        state.component_controller_count = read_u32(payload, offset);
        offset += sizeof(uint32_t);
        state.component_total_channels = read_i32(payload, offset);
        offset += sizeof(uint32_t);
        state.component_online_devices = read_i32(payload, offset);
        offset += sizeof(uint32_t);
        state.component_offline_devices = read_i32(payload, offset);
        offset += sizeof(uint32_t);
        const uint8_t powered = std::to_integer<uint8_t>(payload[offset++]);
        if (powered > 1) return protocol_error("AE network component power flag is invalid");
        state.component_powered = powered != 0;
        if (auto result = validate_state(state); !result) return result.error();
        if (!anchors.insert(state.anchor_entity_id).second) {
            return protocol_error("AE network snapshot has duplicate anchors");
        }
        snapshot.nodes.push_back(std::move(state));
    }
    if (offset != payload.size()) {
        return protocol_error("AE network snapshot has trailing bytes");
    }
    return snapshot;
}

size_t GameRemoteAeNetworkWorld::PositionHash::operator()(
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

snt::core::Expected<void> GameRemoteAeNetworkWorld::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Remote AE network world received an invalid snapshot id");
    }
    const ReplicationValueLookup lookup = find_value(snapshot.values, true);
    if (lookup.invalid) {
        return protocol_error("Remote AE network snapshot has an invalid value record");
    }
    if (lookup.value == nullptr) {
        nodes_.clear();
        anchors_by_position_.clear();
    } else {
        auto decoded = decode_game_ae_network_replication_snapshot(lookup.value->payload);
        if (!decoded) return decoded.error();
        if (auto result = replace_current_set(*decoded); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameRemoteAeNetworkWorld::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Remote AE network delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Remote AE network delta sequence is invalid");
    }
    const ReplicationValueLookup lookup = find_value(delta.values, false);
    if (lookup.invalid) {
        return protocol_error("Remote AE network delta has duplicate value records");
    }
    if (lookup.value != nullptr) {
        switch (lookup.value->operation) {
            case GameReplicationValueOperation::kUpsert: {
                auto decoded = decode_game_ae_network_replication_snapshot(lookup.value->payload);
                if (!decoded) return decoded.error();
                if (auto result = replace_current_set(*decoded); !result) return result.error();
                break;
            }
            case GameReplicationValueOperation::kRemove:
                if (!lookup.value->payload.empty()) {
                    return protocol_error("Remote AE network remove carries a payload");
                }
                nodes_.clear();
                anchors_by_position_.clear();
                break;
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

std::optional<GameAeNetworkReplicationState>
GameRemoteAeNetworkWorld::find_node(uint64_t anchor_entity_id) const {
    const auto found = nodes_.find(anchor_entity_id);
    return found == nodes_.end()
        ? std::nullopt
        : std::optional<GameAeNetworkReplicationState>{found->second};
}

std::optional<GameAeNetworkReplicationState>
GameRemoteAeNetworkWorld::find_node_at(
    std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const {
    const Position position{
        .dimension_id = std::string(dimension_id),
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
    };
    const auto indexed = anchors_by_position_.find(position);
    return indexed == anchors_by_position_.end() ? std::nullopt : find_node(indexed->second);
}

std::vector<GameAeNetworkReplicationState> GameRemoteAeNetworkWorld::nodes() const {
    std::vector<GameAeNetworkReplicationState> result;
    result.reserve(nodes_.size());
    for (const auto& [anchor_id, node] : nodes_) {
        static_cast<void>(anchor_id);
        result.push_back(node);
    }
    std::sort(result.begin(), result.end(),
              [](const GameAeNetworkReplicationState& left,
                 const GameAeNetworkReplicationState& right) {
                  return left.anchor_entity_id < right.anchor_entity_id;
              });
    return result;
}

void GameRemoteAeNetworkWorld::clear() noexcept {
    nodes_.clear();
    anchors_by_position_.clear();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameRemoteAeNetworkWorld::replace_current_set(
    const GameAeNetworkReplicationSnapshot& snapshot) {
    if (snapshot.nodes.size() > kMaxGameAeNetworkReplicationStates) {
        return protocol_error("Remote AE network snapshot has too many nodes");
    }
    std::unordered_map<uint64_t, GameAeNetworkReplicationState> next_nodes;
    std::unordered_map<Position, uint64_t, PositionHash> next_positions;
    next_nodes.reserve(snapshot.nodes.size());
    next_positions.reserve(snapshot.nodes.size());
    for (const GameAeNetworkReplicationState& state : snapshot.nodes) {
        if (auto result = validate_state(state); !result) return result.error();
        Position position{
            .dimension_id = state.anchor_chunk.dimension_id,
            .root_x = state.root_x,
            .root_y = state.root_y,
            .root_z = state.root_z,
        };
        if (!next_nodes.emplace(state.anchor_entity_id, state).second ||
            !next_positions.emplace(std::move(position), state.anchor_entity_id).second) {
            return protocol_error("Remote AE network snapshot has duplicate identity");
        }
    }
    nodes_ = std::move(next_nodes);
    anchors_by_position_ = std::move(next_positions);
    return {};
}

}  // namespace snt::game::replication
