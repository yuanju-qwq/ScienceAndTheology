#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "player_handle.hpp"
#include "player_state.hpp"

namespace science_and_theology {

// PlayerManager — registry of all authoritative player states.
//
// Owned by the server (GDGameCommandServer in M1, future snt_server
// module later). The binding layer registers players here when they
// join and unregisters them when they leave.
//
// Single-player mode: exactly one player with id = kSinglePlayerHandle (1).
//
// This class is engine-agnostic: it only stores raw pointers to
// gt::Inventory / gt::Equipment. Lifetime of those objects is managed
// by the caller (typically GDPlayerInventory / GDPlayerEquipment
// Godot Resources).
class PlayerManager {
public:
    PlayerManager() = default;
    ~PlayerManager() = default;

    // Register a new player with the given id. Returns false if the id
    // is already taken or invalid.
    //
    // inventory / equipment are raw pointers owned by the caller.
    // They may be nullptr initially and bound later via bind_inventory /
    // bind_equipment.
    bool register_player(PlayerHandle id,
                         gt::Inventory* inventory = nullptr,
                         gt::Equipment* equipment = nullptr);

    // Remove a player from the registry. Does not delete the inventory
    // or equipment pointers (they are owned by the caller).
    bool unregister_player(PlayerHandle id);

    // Bind / rebind the inventory pointer for a registered player.
    bool bind_inventory(PlayerHandle id, gt::Inventory* inventory);

    // Bind / rebind the equipment pointer for a registered player.
    bool bind_equipment(PlayerHandle id, gt::Equipment* equipment);

    // Update the player's current chunk position.
    bool set_player_chunk(PlayerHandle id,
                          const std::string& dimension,
                          int cx, int cy, int cz);

    // Returns true if a player with the given id is registered.
    bool has_player(PlayerHandle id) const;

    // Returns the player state, or nullptr if not registered.
    PlayerState* get_player(PlayerHandle id);
    const PlayerState* get_player(PlayerHandle id) const;

    // Returns all registered player ids.
    std::vector<PlayerHandle> all_ids() const;

    // Returns the number of registered players.
    size_t player_count() const { return players_.size(); }

    // Clears all registered players. Does not delete owned pointers.
    void clear();

private:
    std::unordered_map<PlayerHandle, std::unique_ptr<PlayerState>> players_;
};

} // namespace science_and_theology
