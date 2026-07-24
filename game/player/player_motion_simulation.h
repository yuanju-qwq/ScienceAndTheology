// Shared player-motion simulation.
//
// This module is the current game-owned physics kernel for authoritative
// server movement and client-side prediction. It accepts only typed movement
// intent plus a narrow collision view; neither side imports Godot state,
// transport objects, or an ECS World into the simulation step.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "player/voxel_collision.h"

#include <cstdint>
#include <optional>
#include <string>

namespace snt::game::replication {

struct GamePlayerMotionConfig {
    float walk_speed_blocks_per_second = 4.3f;
    float sprint_multiplier = 1.45f;
    float jump_speed_blocks_per_second = 6.2f;
    float gravity_blocks_per_second_squared = 20.0f;
    float terminal_velocity_blocks_per_second = 48.0f;
    float body_width_blocks = 0.6f;
    float body_height_blocks = 1.8f;
    uint64_t input_timeout_ticks = 6;
};

// Runtime-only continuous state. Persistent player state remains block-grid
// based; this value is reconstructed on login and reset by teleports.
struct GamePlayerMotionState {
    std::string dimension_id;
    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_z = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float velocity_z = 0.0f;
    int16_t yaw_centidegrees = 0;
    int16_t pitch_centidegrees = 0;
    uint64_t last_consumed_jump_sequence = 0;
    uint64_t last_processed_input_sequence = 0;
    bool grounded = false;
};

struct GamePlayerMotionStepResult {
    bool hit_x = false;
    bool hit_y = false;
    bool hit_z = false;
    bool grounded = false;
};

[[nodiscard]] snt::core::Expected<void> validate_game_player_motion_config(
    const GamePlayerMotionConfig& config);
[[nodiscard]] snt::core::Expected<void> validate_game_player_motion_state(
    const GamePlayerMotionState& state);

[[nodiscard]] GamePlayerMotionState make_game_player_motion_state(
    const GamePlayerWorldPosition& position);
[[nodiscard]] GamePlayerWorldPosition game_player_motion_world_position(
    const GamePlayerMotionState& state);

// Advances one fixed simulation tick. An absent input means that movement
// axes are zero while gravity continues to apply. Callers decide freshness and
// packet coalescing before invoking this function.
[[nodiscard]] snt::core::Expected<GamePlayerMotionStepResult> advance_game_player_motion(
    GamePlayerMotionState& state, std::optional<GamePlayerMovementInput> input,
    const snt::player::IVoxelCollisionWorld& collision_world,
    const GamePlayerMotionConfig& config, float delta_seconds);

// Server replication consumes only this narrow value source. It avoids giving
// replication access to the server movement cache or its input queues.
class IGameAuthoritativePlayerMotionSource {
public:
    virtual ~IGameAuthoritativePlayerMotionSource() = default;

    [[nodiscard]] virtual std::optional<GamePlayerMotionState> motion_snapshot_for_player(
        snt::ecs::EntityGuid entity_guid) const = 0;
};

}  // namespace snt::game::replication
