// Shared deterministic player-motion simulation implementation.

#include "game/player/player_motion_simulation.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaximumCoordinateMagnitude = 16'000'000.0f;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool is_finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool is_finite_coordinate(float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= kMaximumCoordinateMagnitude;
}

[[nodiscard]] snt::player::Aabb player_body_aabb(
    const GamePlayerMotionState& state, const GamePlayerMotionConfig& config) {
    const float half_width = config.body_width_blocks * 0.5f;
    return {
        .min = {state.feet_x - half_width, state.feet_y, state.feet_z - half_width},
        .max = {state.feet_x + half_width, state.feet_y + config.body_height_blocks,
                state.feet_z + half_width},
    };
}

}  // namespace

snt::core::Expected<void> validate_game_player_motion_config(
    const GamePlayerMotionConfig& config) {
    if (!is_finite_positive(config.walk_speed_blocks_per_second) ||
        config.walk_speed_blocks_per_second > 128.0f ||
        !is_finite_positive(config.sprint_multiplier) || config.sprint_multiplier > 4.0f ||
        !is_finite_positive(config.jump_speed_blocks_per_second) ||
        config.jump_speed_blocks_per_second > 128.0f ||
        !is_finite_positive(config.gravity_blocks_per_second_squared) ||
        config.gravity_blocks_per_second_squared > 256.0f ||
        !is_finite_positive(config.terminal_velocity_blocks_per_second) ||
        config.terminal_velocity_blocks_per_second > 512.0f ||
        !is_finite_positive(config.body_width_blocks) || config.body_width_blocks > 4.0f ||
        !is_finite_positive(config.body_height_blocks) || config.body_height_blocks > 8.0f ||
        config.input_timeout_ticks == 0 || config.input_timeout_ticks > 600) {
        return invalid_argument("Game player motion configuration is invalid");
    }
    return {};
}

snt::core::Expected<void> validate_game_player_motion_state(
    const GamePlayerMotionState& state) {
    if (state.dimension_id.empty() ||
        !is_finite_coordinate(state.feet_x) || !is_finite_coordinate(state.feet_y) ||
        !is_finite_coordinate(state.feet_z) || !std::isfinite(state.velocity_x) ||
        !std::isfinite(state.velocity_y) || !std::isfinite(state.velocity_z) ||
        state.yaw_centidegrees < -18000 || state.yaw_centidegrees >= 18000 ||
        state.pitch_centidegrees < -8900 || state.pitch_centidegrees > 8900) {
        return invalid_argument("Game player motion state is invalid");
    }
    return {};
}

GamePlayerMotionState make_game_player_motion_state(
    const GamePlayerWorldPosition& position) {
    return {
        .dimension_id = position.dimension_id,
        .feet_x = static_cast<float>(position.position.x),
        .feet_y = static_cast<float>(position.position.y),
        .feet_z = static_cast<float>(position.position.z),
    };
}

GamePlayerWorldPosition game_player_motion_world_position(
    const GamePlayerMotionState& state) {
    return {
        .dimension_id = state.dimension_id,
        .position = {
            .x = snt::player::floor_to_i32(state.feet_x),
            .y = snt::player::floor_to_i32(state.feet_y),
            .z = snt::player::floor_to_i32(state.feet_z),
        },
    };
}

snt::core::Expected<GamePlayerMotionStepResult> advance_game_player_motion(
    GamePlayerMotionState& state, std::optional<GamePlayerMovementInput> input,
    const snt::player::IVoxelCollisionWorld& collision_world,
    const GamePlayerMotionConfig& config, float delta_seconds) {
    if (auto result = validate_game_player_motion_config(config); !result) return result.error();
    if (auto result = validate_game_player_motion_state(state); !result) return result.error();
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f || delta_seconds > 1.0f) {
        return invalid_argument("Game player motion tick delta is invalid");
    }
    if (input.has_value()) {
        if (auto result = validate_game_player_movement_input(*input); !result) {
            return result.error();
        }
        state.yaw_centidegrees = input->yaw_centidegrees;
        state.pitch_centidegrees = input->pitch_centidegrees;
        state.last_processed_input_sequence = std::max(
            state.last_processed_input_sequence, input->client_sequence);
    }

    float forward_axis = input.has_value() ? static_cast<float>(input->forward_axis) : 0.0f;
    float strafe_axis = input.has_value() ? static_cast<float>(input->strafe_axis) : 0.0f;
    const float input_length = std::sqrt(forward_axis * forward_axis +
                                         strafe_axis * strafe_axis);
    if (input_length > 1.0f) {
        forward_axis /= input_length;
        strafe_axis /= input_length;
    }

    const float yaw_radians = static_cast<float>(state.yaw_centidegrees) *
                              (kPi / 18000.0f);
    const float forward_x = std::cos(yaw_radians);
    const float forward_z = std::sin(yaw_radians);
    const float right_x = -forward_z;
    const float right_z = forward_x;
    float speed = config.walk_speed_blocks_per_second;
    if (input.has_value() && (input->flags & kGamePlayerMovementFlagSprint) != 0) {
        speed *= config.sprint_multiplier;
    }
    state.velocity_x = (forward_x * forward_axis + right_x * strafe_axis) * speed;
    state.velocity_z = (forward_z * forward_axis + right_z * strafe_axis) * speed;

    if (input.has_value() && state.grounded &&
        (input->flags & kGamePlayerMovementFlagJump) != 0 &&
        state.last_consumed_jump_sequence != input->client_sequence) {
        state.velocity_y = config.jump_speed_blocks_per_second;
        state.grounded = false;
        state.last_consumed_jump_sequence = input->client_sequence;
    }
    state.velocity_y = std::max(
        state.velocity_y - config.gravity_blocks_per_second_squared * delta_seconds,
        -config.terminal_velocity_blocks_per_second);

    const snt::player::CollisionMoveResult move = snt::player::move_aabb_collide_voxels(
        collision_world, player_body_aabb(state, config),
        {.x = state.velocity_x * delta_seconds,
         .y = state.velocity_y * delta_seconds,
         .z = state.velocity_z * delta_seconds});
    state.feet_x += move.delta.x;
    state.feet_y += move.delta.y;
    state.feet_z += move.delta.z;
    if (move.hit_x) state.velocity_x = 0.0f;
    if (move.hit_z) state.velocity_z = 0.0f;
    if (move.hit_y) {
        state.velocity_y = 0.0f;
        state.grounded = move.grounded;
    } else {
        state.grounded = false;
    }
    if (auto result = validate_game_player_motion_state(state); !result) return result.error();
    return GamePlayerMotionStepResult{
        .hit_x = move.hit_x,
        .hit_y = move.hit_y,
        .hit_z = move.hit_z,
        .grounded = state.grounded,
    };
}

}  // namespace snt::game::replication
