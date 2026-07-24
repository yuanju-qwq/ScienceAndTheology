// Game-owned player entity replication values.
//
// This module defines the concrete player payload carried inside the generic
// SNTG snapshot/delta entity blobs. It keeps presentation-safe player state
// separate from authoritative inventory, organ, and tool state, and provides
// a headless client-side value world for ordered remote-player updates.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/player/player_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGamePlayerReplicationEntityKind = 1;
inline constexpr uint8_t kGamePlayerReplicationEntityVersion = 2;

enum class GamePlayerReplicationOperation : uint8_t {
    kUpsert = 1,
    kRemove = 2,
};

// A presentation-safe continuous motion snapshot. The server is still the
// only physics authority; clients use this value to confirm predicted input,
// smooth reconciliation, and interpolate remote players.
struct GameReplicatedPlayerMotion {
    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_z = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float velocity_z = 0.0f;
    int16_t yaw_centidegrees = 0;
    int16_t pitch_centidegrees = 0;
    uint64_t last_processed_input_sequence = 0;
    uint64_t source_tick = 0;
    bool grounded = false;
};

// This is the only player state exposed to other clients. Equipment instance
// data, inventory, organs, and trusted tool tags stay server-only.
struct GameReplicatedPlayerState {
    PlayerIdentity identity;
    GamePlayerWorldPosition position;
    GameReplicatedPlayerMotion motion;
    std::array<std::string, kGamePlayerEquipmentSlotCount> equipment_item_ids;
};

// A remove payload intentionally carries no player data. The outer entity
// Guid identifies the actor to remove from a remote player's AOI value world.
struct GamePlayerReplicationEntity {
    GamePlayerReplicationOperation operation = GamePlayerReplicationOperation::kUpsert;
    std::optional<GameReplicatedPlayerState> player;
};

[[nodiscard]] bool is_game_player_replication_entity_payload(
    std::span<const std::byte> payload) noexcept;
[[nodiscard]] snt::core::Expected<void> validate_game_replicated_player_state(
    const GameReplicatedPlayerState& state);
[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_game_player_replication_entity(
    const GamePlayerReplicationEntity& entity);
[[nodiscard]] snt::core::Expected<GamePlayerReplicationEntity>
decode_game_player_replication_entity(std::span<const std::byte> payload);

struct GameRemotePlayerState {
    snt::ecs::EntityGuid entity_guid;
    GameReplicatedPlayerState player;
};

// The graphical host owns eventual mesh/avatar presentation. This value world
// only validates, orders, and exposes the server-originated player state that
// presentation consumes; it never owns transport or an authoritative ECS World.
class GameRemotePlayerWorld final {
public:
    explicit GameRemotePlayerWorld(std::string local_account_id);

    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] std::optional<GameRemotePlayerState> authoritative_local_player() const;
    [[nodiscard]] std::vector<GameRemotePlayerState> remote_players() const;
    [[nodiscard]] size_t player_count() const noexcept { return players_.size(); }
    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }

    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> apply_entity(
        snt::ecs::EntityGuid entity_guid, const GamePlayerReplicationEntity& entity);

    std::string local_account_id_;
    std::map<uint64_t, GameRemotePlayerState> players_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
