// Native creature selection and command mapping.
//
// Ownership: this module turns value-only replicated creature presentation
// into client command intent. Terrain visibility remains an input to picking,
// while the dedicated server remains authoritative for reach, inventory,
// creature state, and enclosure validation.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/world/defs/creature_presentation.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace snt::game {

struct GameClientCreatureRaycast {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float origin_z = 0.0f;
    float direction_x = 0.0f;
    float direction_y = 0.0f;
    float direction_z = 0.0f;
    float max_distance_blocks = 0.0f;
    // When present, a solid terrain cell has already been found at this
    // distance. A creature behind it cannot be selected through the wall.
    std::optional<float> terrain_hit_distance_blocks;
};

// Presentation models are centered at their feet. The generic capsule-like
// pick volume deliberately lives outside renderer meshes so future species
// meshes can replace it without changing command admission.
struct GameClientCreaturePickConfig {
    float body_radius_blocks = 0.8f;
    float body_height_blocks = 1.0f;
};

enum class GameClientCreatureInteractionTargetKind : uint8_t {
    kWild,
    kCaptive,
};

struct GameClientCreatureInteractionTarget {
    uint64_t creature_entity_id = 0;
    GameClientCreatureInteractionTargetKind kind =
        GameClientCreatureInteractionTargetKind::kWild;
    bool is_tamed = false;
    float hit_distance_blocks = 0.0f;
};

// Finds the nearest selectable wild or captive representative on the local
// side of the terrain hit. Far visual representatives are intentionally not
// eligible because they do not own an authoritative wildlife actor.
[[nodiscard]] std::optional<GameClientCreatureInteractionTarget>
pick_game_client_creature_interaction_target(
    std::span<const GameCreaturePresentationState> creatures,
    std::string_view dimension_id, const GameClientCreatureRaycast& raycast,
    GameClientCreaturePickConfig config = {});

struct GameClientCreatureInteractionInput {
    bool attack_pressed = false;
    bool context_pressed = false;
};

// Kept independent from the replication session so offline-host and queued
// command adapters can reuse the same presentation input policy.
class IGameClientCreatureInteractionCommandSink {
public:
    virtual ~IGameClientCreatureInteractionCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_creature_attack(
        replication::GameCreatureAttackCommand command) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> submit_creature_capture(
        replication::GameCreatureCaptureCommand command) = 0;
    [[nodiscard]] virtual snt::core::Expected<void> submit_captive_creature_feed(
        replication::GameCaptiveCreatureFeedCommand command) = 0;
};

class GameClientCreatureInteractionController final {
public:
    // Returns true when a selectable creature consumed the pointer input.
    // Attack takes precedence if both mouse buttons transition in one frame.
    // A tamed captive or an empty feed slot still consumes context input so
    // the player cannot mine or use terrain through the selected creature.
    [[nodiscard]] snt::core::Expected<bool> handle_input(
        const GameClientCreatureInteractionInput& input,
        const std::optional<GameClientCreatureInteractionTarget>& target,
        std::string_view selected_item_id,
        IGameClientCreatureInteractionCommandSink& sink) const;
};

}  // namespace snt::game
