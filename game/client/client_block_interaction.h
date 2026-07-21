// Game-owned graphical-client block interaction mapping.
//
// Ownership: this module converts local, value-only presentation input into
// the current SNTG BlockInteraction command. It never mutates terrain,
// inventories, sidecars, or machine runtime state; the authenticated host
// remains the only authoritative commit boundary.

#pragma once

#include "core/expected.h"
#include "game/client/game_session_config.h"
#include "game/network/game_replication_protocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace snt::game {

// A local DDA pick result. The hit cell is used by mine/use/machine commands;
// placement uses the adjacent cell and carries its own optimistic material.
struct GameClientBlockInteractionTarget {
    std::string dimension_id;
    int32_t hit_x = 0;
    int32_t hit_y = 0;
    int32_t hit_z = 0;
    uint16_t hit_material = 0;

    struct PlacementCell {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint16_t expected_material = replication::kGameNoExpectedTerrainMaterial;
    };

    std::optional<PlacementCell> placement;

    // Farming classification is calculated from the currently presented
    // worldgen/content snapshots before the controller receives the input.
    // It is only a local command-selection hint; the server repeats every
    // material, item, reach, and sidecar validation before committing.
    struct FarmingTarget {
        bool can_till = false;
        bool can_plant = false;
        bool can_fertilize = false;
        bool can_harvest = false;
        int32_t planting_x = 0;
        int32_t planting_y = 0;
        int32_t planting_z = 0;
        uint16_t planting_expected_material = replication::kGameNoExpectedTerrainMaterial;
    };

    std::optional<FarmingTarget> farming;
};

// The remote-machine cache supplies only presentation values. This distilled
// record keeps the input mapper independent of replication cache ownership
// and leaves a stable future seam for the production machine panel.
struct GameClientMachineInteractionTarget {
    std::string machine_id;
    bool has_collectible_output = false;
    bool requires_manual_activation = false;
    uint8_t activation_hints = 0;
};

struct GameClientBlockInteractionInput {
    bool mine_pressed = false;
    bool context_pressed = false;
};

// The network session is one implementation. A local-host adapter or a
// future queued UI command adapter can use the same presentation boundary.
class IGameClientBlockInteractionCommandSink {
public:
    virtual ~IGameClientBlockInteractionCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_block_interaction(
        replication::GameBlockInteractionCommand command) = 0;
};

class GameClientBlockInteractionController final {
public:
    explicit GameClientBlockInteractionController(GameClientInteractionConfig config);

    // At most one command is submitted per frame. Mining has precedence when
    // both pointer buttons transition in the same input frame.
    [[nodiscard]] snt::core::Expected<void> handle_input(
        const GameClientBlockInteractionInput& input,
        const std::optional<GameClientBlockInteractionTarget>& target,
        std::string selected_item_id,
        const std::optional<GameClientMachineInteractionTarget>& machine,
        IGameClientBlockInteractionCommandSink& sink) const;

private:
    [[nodiscard]] replication::GameBlockInteractionCommand make_hit_command(
        replication::GameBlockInteractionAction action,
        const GameClientBlockInteractionTarget& target) const;

    GameClientInteractionConfig config_;
};

}  // namespace snt::game
