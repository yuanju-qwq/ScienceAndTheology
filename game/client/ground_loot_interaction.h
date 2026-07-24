// Client selection and typed command mapping for replicated ground loot.
//
// This module turns an AOI-filtered presentation set into a stable loot-id
// pickup intent. It never predicts inventory changes or owns world records;
// the dedicated server remains the authority for reach, capacity, and erase.

#pragma once

#include "core/expected.h"
#include "game/network/game_ground_loot_replication.h"
#include "game/network/game_replication_protocol.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace snt::game {

struct GameClientGroundLootRaycast {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float origin_z = 0.0f;
    float direction_x = 0.0f;
    float direction_y = 0.0f;
    float direction_z = 0.0f;
    float max_distance_blocks = 0.0f;
    // A terrain hit blocks a loot target farther along the same ray.
    std::optional<float> terrain_hit_distance_blocks;
};

struct GameClientGroundLootPickConfig {
    float pickup_radius_blocks = 0.45f;
};

struct GameClientGroundLootInteractionTarget {
    uint64_t loot_id = 0;
    float hit_distance_blocks = 0.0f;
};

// Finds the nearest ground-loot presentation record on the local side of a
// terrain hit. The returned id is only a typed command target, never proof
// that the record remains available on the host.
[[nodiscard]] std::optional<GameClientGroundLootInteractionTarget>
pick_game_client_ground_loot_interaction_target(
    std::span<const replication::GameGroundLootPresentationState> loot,
    std::string_view dimension_id, const GameClientGroundLootRaycast& raycast,
    GameClientGroundLootPickConfig config = {});

struct GameClientGroundLootInteractionInput {
    bool pickup_pressed = false;
};

class IGameClientGroundLootInteractionCommandSink {
public:
    virtual ~IGameClientGroundLootInteractionCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_ground_loot_pickup(
        replication::GameGroundLootPickupCommand command) = 0;
};

class GameClientGroundLootInteractionController final {
public:
    // Returns true only when a target consumed an explicit pickup click.
    [[nodiscard]] snt::core::Expected<bool> handle_input(
        const GameClientGroundLootInteractionInput& input,
        const std::optional<GameClientGroundLootInteractionTarget>& target,
        IGameClientGroundLootInteractionCommandSink& sink) const;
};

}  // namespace snt::game
