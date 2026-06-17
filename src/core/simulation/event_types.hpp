#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology {

enum class GameEventType : uint32_t {
    NONE = 0,

    // Machine events
    MACHINE_STATE_CHANGED   = 1,
    MACHINE_RECIPE_STARTED  = 2,
    MACHINE_RECIPE_COMPLETED = 3,
    MACHINE_RECIPE_ABORTED  = 4,
    MACHINE_ERROR           = 5,

    // Power events
    POWER_OVERLOAD     = 100,
    POWER_RESTORED     = 101,
    POWER_NETWORK_CHANGED = 102,

    // Chunk events
    CHUNK_LOADED       = 200,
    CHUNK_UNLOADED     = 201,
    CHUNK_STATE_CHANGED = 202,
    CHUNK_GENERATED    = 203,

    // Entity events
    ENTITY_CREATED     = 300,
    ENTITY_DESTROYED   = 301,
    ENTITY_MOVED       = 302,
    ENTITY_DAMAGED     = 303,

    // Connector events
    CONNECTOR_ACTIVATED = 400,
    CONNECTOR_LOCKED    = 401,
    CONNECTOR_UNLOCKED  = 402,

    // Network topology events
    NETWORK_TOPO_CHANGED = 500,

    // World events
    WORLD_SAVED  = 600,
    WORLD_LOADED = 601,

    // Terrain events
    TERRAIN_CHANGED = 604,

    // Item events
    ITEM_DROPPED    = 610,
    ITEM_PICKED_UP  = 611,

    // Player events
    PLAYER_INVENTORY_CHANGED = 620,
    PLAYER_EQUIPMENT_CHANGED = 621,

    // Custom / user-defined slot
    CUSTOM = 0x80000000,
};

struct GameEvent {
    GameEventType type = GameEventType::NONE;
    uint64_t source_id = 0;
    std::string source_dimension;
    int32_t cell_x = 0;
    int32_t cell_y = 0;
    int32_t cell_z = 0;
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    int32_t chunk_z = 0;
    int64_t timestamp = 0;

    std::unordered_map<std::string, std::string> string_data;
    std::unordered_map<std::string, int64_t> int_data;
    std::unordered_map<std::string, double> float_data;

    static GameEvent machine_state_changed(
        uint64_t machine_id, int old_state, int new_state);

    static GameEvent machine_recipe_completed(
        uint64_t machine_id, const char* recipe_name);

    static GameEvent machine_error(
        uint64_t machine_id, const char* error_code, const char* message);

    static GameEvent power_overload(
        uint64_t node_id, int64_t demand, int64_t capacity);

    static GameEvent chunk_state_changed(
        const std::string& dimension, int cx, int cy, int cz,
        int old_state, int new_state);

    static GameEvent entity_created(
        uint64_t entity_id, const std::string& type_name,
        const std::string& dimension, int cx, int cy, int cz);

    static GameEvent entity_destroyed(
        uint64_t entity_id, const std::string& dimension);

    static GameEvent entity_damaged(
        uint64_t entity_id, float damage, const std::string& source_dimension);

    static GameEvent terrain_changed(
        const std::string& dimension,
        int cx, int cy, int cz,
        int local_x, int local_y, int local_z,
        int old_material, int new_material);

    static GameEvent item_dropped(
        uint64_t item_id, int32_t count, const std::string& dimension,
        int cx, int cy, int cz,
        int local_x, int local_y, int local_z);

    static GameEvent item_picked_up(
        uint64_t item_id, int32_t count, const std::string& dimension);

    static GameEvent player_inventory_changed();

    static GameEvent player_equipment_changed(
        int slot_type, uint64_t old_item_id, uint64_t new_item_id);
};

} // namespace science_and_theology
