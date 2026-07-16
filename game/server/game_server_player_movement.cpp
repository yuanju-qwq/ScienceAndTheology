// Dedicated-server authoritative player movement implementation.

#define SNT_LOG_CHANNEL "game.server_player_movement"
#include "game/server/game_server_player_movement.h"

#include "core/error.h"
#include "core/log.h"
#include "game/server/game_server_player_state.h"
#include "player/voxel_collision.h"
#include "voxel/data/chunk_registry.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool same_position(const snt::ecs::Position& left,
                                  const snt::ecs::Position& right) noexcept {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] snt::ecs::Position block_grid_position(
    float feet_x, float feet_y, float feet_z) {
    return {
        .x = snt::player::floor_to_i32(feet_x),
        .y = snt::player::floor_to_i32(feet_y),
        .z = snt::player::floor_to_i32(feet_z),
    };
}

[[nodiscard]] snt::player::Aabb player_body_aabb(
    float feet_x, float feet_y, float feet_z,
    const GameServerPlayerMovementConfig& config) {
    const float half_width = config.body_width_blocks * 0.5f;
    return {
        .min = {feet_x - half_width, feet_y, feet_z - half_width},
        .max = {feet_x + half_width, feet_y + config.body_height_blocks,
                feet_z + half_width},
    };
}

[[nodiscard]] GameAuthenticatedPeer peer_for_snapshot(
    const GameServerPlayerSnapshot& snapshot) {
    return {
        .peer = snapshot.peer,
        .identity = {
            .provider = snapshot.identity_provider,
            .account_id = snapshot.account_id,
            .display_name = snapshot.display_name,
        },
    };
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerPlayerMovement>> GameServerPlayerMovement::create(
    GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
    GameServerPlayerMovementConfig config) {
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
        return invalid_argument("Dedicated server player movement configuration is invalid");
    }
    return std::unique_ptr<GameServerPlayerMovement>(
        new GameServerPlayerMovement(player_state, chunks, std::move(config)));
}

GameServerPlayerMovement::GameServerPlayerMovement(
    GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
    GameServerPlayerMovementConfig config)
    : player_state_(&player_state), chunks_(&chunks), config_(std::move(config)) {}

snt::core::Expected<void> GameServerPlayerMovement::enqueue_player_movement_input(
    const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
    const snt::network::ReplicationTickContext& context) {
    if (player_state_ == nullptr || chunks_ == nullptr) {
        return invalid_state("Dedicated server player movement has no world services");
    }
    if (auto result = validate_game_player_movement_input(input); !result) return result.error();
    if (auto snapshot = player_state_->snapshot_for_peer(peer); !snapshot) {
        return snapshot.error();
    }
    inputs_.insert_or_assign(peer.peer, TimedInput{
        .input = std::move(input),
        .received_tick = context.tick_index,
    });
    return {};
}

void GameServerPlayerMovement::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                     std::string_view) noexcept {
    if (peer.peer != snt::network::kInvalidPeerId) inputs_.erase(peer.peer);
}

snt::core::Expected<void> GameServerPlayerMovement::reset_player_motion(
    const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) {
        return invalid_state("Dedicated server player movement has no player state");
    }
    auto snapshot = player_state_->snapshot_for_peer(peer);
    if (!snapshot) return snapshot.error();
    motions_.erase(snapshot->entity_guid.value);
    inputs_.erase(peer.peer);
    return {};
}

snt::core::Expected<void> GameServerPlayerMovement::tick(
    const snt::network::ReplicationTickContext& context) {
    if (player_state_ == nullptr || chunks_ == nullptr) {
        return invalid_state("Dedicated server player movement has no world services");
    }
    if (!std::isfinite(context.delta_seconds) || context.delta_seconds <= 0.0f ||
        context.delta_seconds > 1.0f) {
        return invalid_argument("Dedicated server player movement tick delta is invalid");
    }

    auto snapshots = player_state_->active_player_snapshots();
    if (!snapshots) return snapshots.error();

    std::set<uint64_t> active_entities;
    for (const GameServerPlayerSnapshot& snapshot : *snapshots) {
        if (!snapshot.entity_guid.valid() || snapshot.peer == snt::network::kInvalidPeerId) {
            return invalid_state("Dedicated server movement observed an invalid active player snapshot");
        }
        active_entities.insert(snapshot.entity_guid.value);

        MotionState& state = motions_[snapshot.entity_guid.value];
        const bool needs_reset = state.dimension_id != snapshot.position.dimension_id ||
            !same_position(block_grid_position(state.feet_x, state.feet_y, state.feet_z),
                           snapshot.position.position);
        if (needs_reset) {
            state = {
                .dimension_id = snapshot.position.dimension_id,
                .feet_x = static_cast<float>(snapshot.position.position.x),
                .feet_y = static_cast<float>(snapshot.position.position.y),
                .feet_z = static_cast<float>(snapshot.position.position.z),
            };
        }

        const auto input = inputs_.find(snapshot.peer);
        const bool has_fresh_input = input != inputs_.end() &&
            context.tick_index >= input->second.received_tick &&
            context.tick_index - input->second.received_tick <= config_.input_timeout_ticks;
        if (has_fresh_input) {
            state.yaw_centidegrees = input->second.input.yaw_centidegrees;
            state.pitch_centidegrees = input->second.input.pitch_centidegrees;
        }

        float forward_axis = has_fresh_input
            ? static_cast<float>(input->second.input.forward_axis) : 0.0f;
        float strafe_axis = has_fresh_input
            ? static_cast<float>(input->second.input.strafe_axis) : 0.0f;
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
        float speed = config_.walk_speed_blocks_per_second;
        if (has_fresh_input &&
            (input->second.input.flags & kGamePlayerMovementFlagSprint) != 0) {
            speed *= config_.sprint_multiplier;
        }
        state.velocity_x = (forward_x * forward_axis + right_x * strafe_axis) * speed;
        state.velocity_z = (forward_z * forward_axis + right_z * strafe_axis) * speed;

        if (has_fresh_input && state.grounded &&
            (input->second.input.flags & kGamePlayerMovementFlagJump) != 0 &&
            state.last_consumed_jump_sequence != input->second.input.client_sequence) {
            state.velocity_y = config_.jump_speed_blocks_per_second;
            state.grounded = false;
            state.last_consumed_jump_sequence = input->second.input.client_sequence;
        }
        state.velocity_y = std::max(
            state.velocity_y - config_.gravity_blocks_per_second_squared * context.delta_seconds,
            -config_.terminal_velocity_blocks_per_second);

        const snt::player::CollisionWorldView collision_world(
            chunks_, snapshot.position.dimension_id, config_.missing_chunks_are_solid);
        const snt::player::CollisionMoveResult move = snt::player::move_aabb_collide_voxels(
            collision_world, player_body_aabb(state.feet_x, state.feet_y, state.feet_z, config_),
            {.x = state.velocity_x * context.delta_seconds,
             .y = state.velocity_y * context.delta_seconds,
             .z = state.velocity_z * context.delta_seconds});
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

        const snt::ecs::Position next_grid_position =
            block_grid_position(state.feet_x, state.feet_y, state.feet_z);
        if (!same_position(next_grid_position, snapshot.position.position)) {
            auto result = player_state_->set_authoritative_position(
                peer_for_snapshot(snapshot),
                {.dimension_id = snapshot.position.dimension_id, .position = next_grid_position});
            if (!result) {
                auto error = result.error();
                error.with_context("GameServerPlayerMovement::tick(update position)");
                return error;
            }
        }
    }

    std::erase_if(motions_, [&active_entities](const auto& entry) {
        return !active_entities.contains(entry.first);
    });
    std::erase_if(inputs_, [&snapshots](const auto& entry) {
        return std::none_of(snapshots->begin(), snapshots->end(), [&entry](const auto& snapshot) {
            return snapshot.peer == entry.first;
        });
    });
    return {};
}

}  // namespace snt::game::replication
