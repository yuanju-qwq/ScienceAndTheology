#pragma once

#include <cstdint>
#include <string>

#include "player_id.hpp"
#include "inventory.hpp"
#include "equipment.hpp"

namespace science_and_theology {

// PlayerState — per-player authoritative state owned by the server.
//
// This is the engine-agnostic core representation of a player.
// The GDExtension layer (GDPlayerInventory / GDPlayerEquipment)
// owns the actual gt::Inventory / gt::Equipment instances and
// registers raw pointers here when a player joins.
//
// Lifetime contract:
//   - inventory / equipment pointers are owned by the binding layer.
//   - The binding layer MUST unregister the player (or clear the
//     pointers) before destroying the owning Godot Resource.
//   - PlayerManager never deletes these pointers.
//
// M1 scope: only the fields needed by GDGameCommandServer and
// TickSystem are present. Mana / spell book / source law data will
// be added in later milestones.
struct PlayerState {
    PlayerId id = kInvalidPlayerId;

    // Owned by GDPlayerInventory / GDPlayerEquipment in the binding layer.
    // nullptr means the player has no inventory/equipment bound yet.
    gt::Inventory* inventory = nullptr;
    gt::Equipment* equipment = nullptr;

    // Current position used by TickSystem to compute the active chunk set.
    std::string current_dimension = "overworld";
    int current_cx = 0;
    int current_cy = 0;
    int current_cz = 0;

    bool is_valid() const { return id != kInvalidPlayerId; }
};

} // namespace science_and_theology
