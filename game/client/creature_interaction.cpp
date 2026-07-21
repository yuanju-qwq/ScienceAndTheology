// Native creature selection and command mapping implementation.

#include "game/client/creature_interaction.h"

#include "core/error.h"

#include <cmath>
#include <limits>
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
    const GameClientCreatureRaycast& raycast, float direction_x, float direction_y,
    float direction_z, const GameCreaturePresentationState& creature,
    GameClientCreaturePickConfig config) noexcept {
    const float center_x = creature.position_x;
    const float center_y = creature.position_y + config.body_height_blocks * 0.5f;
    const float center_z = creature.position_z;
    const float offset_x = raycast.origin_x - center_x;
    const float offset_y = raycast.origin_y - center_y;
    const float offset_z = raycast.origin_z - center_z;
    const float projection = offset_x * direction_x + offset_y * direction_y +
        offset_z * direction_z;
    const float radius_squared = config.body_radius_blocks * config.body_radius_blocks;
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

    float distance = near_distance;
    if (distance < 0.0f) distance = 0.0f;
    if (distance > raycast.max_distance_blocks) return std::nullopt;
    return distance;
}

[[nodiscard]] bool eligible_for_interaction(
    const GameCreaturePresentationState& creature) noexcept {
    return creature.entity_id != 0 &&
        ((creature.is_interactive && !creature.is_captive) || creature.is_captive);
}

}  // namespace

std::optional<GameClientCreatureInteractionTarget>
pick_game_client_creature_interaction_target(
    std::span<const GameCreaturePresentationState> creatures,
    std::string_view dimension_id, const GameClientCreatureRaycast& raycast,
    GameClientCreaturePickConfig config) {
    if (dimension_id.empty() || !finite_positive(raycast.max_distance_blocks) ||
        !finite_positive(config.body_radius_blocks) ||
        !finite_positive(config.body_height_blocks) ||
        !std::isfinite(raycast.origin_x) || !std::isfinite(raycast.origin_y) ||
        !std::isfinite(raycast.origin_z) || !std::isfinite(raycast.direction_x) ||
        !std::isfinite(raycast.direction_y) || !std::isfinite(raycast.direction_z)) {
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

    std::optional<GameClientCreatureInteractionTarget> result;
    for (const GameCreaturePresentationState& creature : creatures) {
        if (!eligible_for_interaction(creature) || creature.chunk.dimension_id != dimension_id ||
            !std::isfinite(creature.position_x) || !std::isfinite(creature.position_y) ||
            !std::isfinite(creature.position_z)) {
            continue;
        }
        const auto distance = ray_sphere_hit_distance(
            raycast, direction_x, direction_y, direction_z, creature, config);
        if (!distance.has_value() ||
            (has_terrain_hit && *raycast.terrain_hit_distance_blocks +
                    kOcclusionEpsilonBlocks < *distance)) {
            continue;
        }
        if (result.has_value() &&
            (*distance > result->hit_distance_blocks ||
             (*distance == result->hit_distance_blocks &&
              creature.entity_id > result->creature_entity_id))) {
            continue;
        }
        result = GameClientCreatureInteractionTarget{
            .creature_entity_id = creature.entity_id,
            .kind = creature.is_captive
                ? GameClientCreatureInteractionTargetKind::kCaptive
                : GameClientCreatureInteractionTargetKind::kWild,
            .is_tamed = creature.is_tamed,
            .hit_distance_blocks = *distance,
        };
    }
    return result;
}

snt::core::Expected<bool> GameClientCreatureInteractionController::handle_input(
    const GameClientCreatureInteractionInput& input,
    const std::optional<GameClientCreatureInteractionTarget>& target,
    std::string_view selected_item_id,
    IGameClientCreatureInteractionCommandSink& sink) const {
    if ((!input.attack_pressed && !input.context_pressed) || !target.has_value()) {
        return false;
    }
    if (target->creature_entity_id == 0) {
        return invalid_argument("Client creature interaction target has no stable entity id");
    }

    if (input.attack_pressed) {
        if (target->kind == GameClientCreatureInteractionTargetKind::kWild) {
            if (auto result = sink.submit_creature_attack(
                    {.creature_entity_id = target->creature_entity_id}); !result) {
                return result.error();
            }
        }
        return true;
    }

    if (target->kind == GameClientCreatureInteractionTargetKind::kWild) {
        if (auto result = sink.submit_creature_capture(
                {.creature_entity_id = target->creature_entity_id}); !result) {
            return result.error();
        }
        return true;
    }
    if (!target->is_tamed && !selected_item_id.empty()) {
        if (auto result = sink.submit_captive_creature_feed({
                .creature_entity_id = target->creature_entity_id,
                .feed_item_id = std::string(selected_item_id),
            }); !result) {
            return result.error();
        }
    }
    return true;
}

}  // namespace snt::game
