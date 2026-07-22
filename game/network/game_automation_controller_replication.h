// Automation-controller presentation replication.
//
// Controller programs are durable sidecar values on the server. This module
// carries an AOI-filtered, value-only projection to clients and maintains an
// O(1) anchor/position cache for controller UI lookup. It never transports an
// executor, endpoint pointer, ResourceKey numeric id, or editable authority.

#pragma once

#include "core/expected.h"
#include "game/automation/sfm_flow_program.h"
#include "game/network/game_replication_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameAutomationControllerReplicationVersion = 1;
// Controller graphs are AOI presentation, not bulk save transfer. Keep one
// value well below the peer's default reliable-tick budget so terrain and
// player state still have room in the same snapshot.
inline constexpr size_t kMaxGameAutomationControllerReplicationPayloadBytes =
    96u * 1024u;
inline constexpr size_t kGameAutomationControllerReplicationHeaderBytes =
    sizeof(uint8_t) + sizeof(uint16_t);
inline constexpr size_t kMaxGameAutomationControllerStates = 256;
static_assert(kMaxGameAutomationControllerReplicationPayloadBytes <=
              kMaxGameReplicationValuePayloadBytes);

struct GameAutomationControllerReplicationState {
    snt::voxel::ChunkKey anchor_chunk;
    uint64_t anchor_entity_id = 0;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    std::string controller_key;
    uint64_t authoritative_revision = 0;
    bool online = false;
    SfmFlowProgramRecord sfm_program;
};

struct GameAutomationControllerReplicationSnapshot {
    std::vector<GameAutomationControllerReplicationState> controllers;
};

// Returns the exact encoded size of one state, excluding the snapshot header.
// Server AOI sources use this to choose a complete bounded projection in one
// linear pass before serializing it. Invalid durable/editor values are
// rejected here rather than being allowed to overflow a transport value.
[[nodiscard]] snt::core::Expected<size_t>
measure_game_automation_controller_replication_state(
    const GameAutomationControllerReplicationState& state);

[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_automation_controller_replication_snapshot(
    const GameAutomationControllerReplicationSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameAutomationControllerReplicationSnapshot>
decode_game_automation_controller_replication_snapshot(std::span<const std::byte> payload);

// Client-side cache for authoritative controller presentation. The two hash
// indexes keep UI interaction lookup expected O(1); `controllers()` returns
// copies for non-hot presentation inspection only.
class GameRemoteAutomationControllerWorld final {
public:
    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] size_t controller_count() const noexcept { return controllers_.size(); }
    [[nodiscard]] std::optional<GameAutomationControllerReplicationState>
    find_controller(uint64_t anchor_entity_id) const;
    [[nodiscard]] std::optional<GameAutomationControllerReplicationState>
    find_controller_at(std::string_view dimension_id, int32_t root_x, int32_t root_y,
                       int32_t root_z) const;
    [[nodiscard]] std::vector<GameAutomationControllerReplicationState> controllers() const;
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
        const GameAutomationControllerReplicationSnapshot& snapshot);

    std::unordered_map<uint64_t, GameAutomationControllerReplicationState> controllers_;
    std::unordered_map<Position, uint64_t, PositionHash> anchors_by_position_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
