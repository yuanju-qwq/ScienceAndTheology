// AE physical-topology presentation replication.
//
// The server projects only active, AOI-visible value state from
// AeNetworkRuntimeService. It carries no storage object, ResourceKey runtime
// id, or mutable topology handle. Client lookup keeps anchor and position
// indexes so terminal/UI selection remains expected O(1).

#pragma once

#include "core/expected.h"
#include "game/automation/ae_network_types.h"
#include "game/network/game_replication_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameAeNetworkReplicationVersion = 1;
inline constexpr size_t kMaxGameAeNetworkReplicationPayloadBytes = 96u * 1024u;
inline constexpr size_t kGameAeNetworkReplicationHeaderBytes =
    sizeof(uint8_t) + sizeof(uint16_t);
inline constexpr size_t kMaxGameAeNetworkReplicationStates = 1024;
static_assert(kMaxGameAeNetworkReplicationPayloadBytes <=
              kMaxGameReplicationValuePayloadBytes);

struct GameAeNetworkReplicationState {
    snt::voxel::ChunkKey anchor_chunk;
    uint64_t anchor_entity_id = 0;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = false;
    bool online = false;
    uint32_t component_id = 0;
    int32_t provided_channels = 0;
    uint64_t authoritative_revision = 0;
    uint64_t topology_revision = 0;
    uint32_t component_node_count = 0;
    uint32_t component_controller_count = 0;
    int32_t component_total_channels = 0;
    int32_t component_online_devices = 0;
    int32_t component_offline_devices = 0;
    bool component_powered = false;
};

struct GameAeNetworkReplicationSnapshot {
    std::vector<GameAeNetworkReplicationState> nodes;
};

[[nodiscard]] snt::core::Expected<size_t>
measure_game_ae_network_replication_state(const GameAeNetworkReplicationState& state);
[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_ae_network_replication_snapshot(const GameAeNetworkReplicationSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameAeNetworkReplicationSnapshot>
decode_game_ae_network_replication_snapshot(std::span<const std::byte> payload);

class GameRemoteAeNetworkWorld final {
public:
    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::optional<GameAeNetworkReplicationState> find_node(
        uint64_t anchor_entity_id) const;
    [[nodiscard]] std::optional<GameAeNetworkReplicationState> find_node_at(
        std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const;
    [[nodiscard]] std::vector<GameAeNetworkReplicationState> nodes() const;
    void clear() noexcept;

private:
    struct Position {
        std::string dimension_id;
        int32_t root_x = 0;
        int32_t root_y = 0;
        int32_t root_z = 0;

        friend bool operator==(const Position&, const Position&) = default;
    };

    struct PositionHash {
        [[nodiscard]] size_t operator()(const Position& position) const noexcept;
    };

    [[nodiscard]] snt::core::Expected<void> replace_current_set(
        const GameAeNetworkReplicationSnapshot& snapshot);

    std::unordered_map<uint64_t, GameAeNetworkReplicationState> nodes_;
    std::unordered_map<Position, uint64_t, PositionHash> anchors_by_position_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
