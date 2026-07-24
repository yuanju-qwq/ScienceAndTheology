// Client ground-loot selection and command mapping implementation.

#include "game/client/ground_loot_interaction.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace snt::game {
namespace {

constexpr float kOcclusionEpsilonBlocks = 1.0e-4f;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool finite_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] std::optional<float> ray_sphere_hit_distance(
    const GameClientGroundLootRaycast& raycast, float direction_x, float direction_y,
    float direction_z, const replication::GameGroundLootPresentationState& loot,
    GameClientGroundLootPickConfig config) noexcept {
    const float offset_x = raycast.origin_x - loot.position_x;
    const float offset_y = raycast.origin_y - loot.position_y;
    const float offset_z = raycast.origin_z - loot.position_z;
    const float projection = offset_x * direction_x + offset_y * direction_y +
        offset_z * direction_z;
    const float radius_squared = config.pickup_radius_blocks * config.pickup_radius_blocks;
    const float distance_squared = offset_x * offset_x + offset_y * offset_y +
        offset_z * offset_z;
    const float discriminant = projection * projection - (distance_squared - radius_squared);
    if (!std::isfinite(discriminant) || discriminant < 0.0f) return std::nullopt;

    const float root = std::sqrt(discriminant);
    const float near_distance = -projection - root;
    const float far_distance = -projection + root;
    if (!std::isfinite(near_distance) || !std::isfinite(far_distance) ||
        far_distance < 0.0f) {
        return std::nullopt;
    }
    const float distance = std::max(0.0f, near_distance);
    return distance <= raycast.max_distance_blocks ? std::optional<float>(distance)
                                                   : std::nullopt;
}

}  // namespace

std::optional<GameClientGroundLootInteractionTarget>
pick_game_client_ground_loot_interaction_target(
    std::span<const replication::GameGroundLootPresentationState> loot,
    std::string_view dimension_id, const GameClientGroundLootRaycast& raycast,
    GameClientGroundLootPickConfig config) {
    if (dimension_id.empty() || !finite_positive(raycast.max_distance_blocks) ||
        !finite_positive(config.pickup_radius_blocks) || !std::isfinite(raycast.origin_x) ||
        !std::isfinite(raycast.origin_y) || !std::isfinite(raycast.origin_z) ||
        !std::isfinite(raycast.direction_x) || !std::isfinite(raycast.direction_y) ||
        !std::isfinite(raycast.direction_z)) {
        return std::nullopt;
    }

    const float direction_length_squared = raycast.direction_x * raycast.direction_x +
        raycast.direction_y * raycast.direction_y + raycast.direction_z * raycast.direction_z;
    if (!finite_positive(direction_length_squared)) return std::nullopt;
    const float inverse_direction_length = 1.0f / std::sqrt(direction_length_squared);
    const float direction_x = raycast.direction_x * inverse_direction_length;
    const float direction_y = raycast.direction_y * inverse_direction_length;
    const float direction_z = raycast.direction_z * inverse_direction_length;
    const bool has_terrain_hit = raycast.terrain_hit_distance_blocks.has_value() &&
        std::isfinite(*raycast.terrain_hit_distance_blocks) &&
        *raycast.terrain_hit_distance_blocks >= 0.0f;

    std::optional<GameClientGroundLootInteractionTarget> result;
    for (const replication::GameGroundLootPresentationState& state : loot) {
        if (state.loot_id == 0 || state.chunk.dimension_id != dimension_id ||
            !std::isfinite(state.position_x) || !std::isfinite(state.position_y) ||
            !std::isfinite(state.position_z)) {
            continue;
        }
        const auto distance = ray_sphere_hit_distance(
            raycast, direction_x, direction_y, direction_z, state, config);
        if (!distance.has_value() ||
            (has_terrain_hit && *raycast.terrain_hit_distance_blocks +
                    kOcclusionEpsilonBlocks < *distance)) {
            continue;
        }
        if (result.has_value() &&
            (*distance > result->hit_distance_blocks ||
             (*distance == result->hit_distance_blocks && state.loot_id > result->loot_id))) {
            continue;
        }
        result = GameClientGroundLootInteractionTarget{
            .loot_id = state.loot_id,
            .hit_distance_blocks = *distance,
        };
    }
    return result;
}

snt::core::Expected<bool> GameClientGroundLootInteractionController::handle_input(
    const GameClientGroundLootInteractionInput& input,
    const std::optional<GameClientGroundLootInteractionTarget>& target,
    IGameClientGroundLootInteractionCommandSink& sink) const {
    if (!input.pickup_pressed || !target.has_value()) return false;
    if (target->loot_id == 0) {
        return invalid_argument("Client ground loot interaction target has no stable loot id");
    }
    if (auto result = sink.submit_ground_loot_pickup({.loot_id = target->loot_id}); !result) {
        return result.error();
    }
    return true;
}

}  // namespace snt::game
