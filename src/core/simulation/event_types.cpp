#include "event_types.hpp"
#include <chrono>

namespace science_and_theology {

static int64_t now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

GameEvent GameEvent::power_overload(
    uint64_t node_id, int64_t demand, int64_t capacity) {
    GameEvent ev;
    ev.type = GameEventType::POWER_OVERLOAD;
    ev.source_id = node_id;
    ev.int_data["demand"] = demand;
    ev.int_data["capacity"] = capacity;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::chunk_state_changed(
    const std::string& dimension, int cx, int cy, int cz,
    int old_state, int new_state) {
    GameEvent ev;
    ev.type = GameEventType::CHUNK_STATE_CHANGED;
    ev.source_dimension = dimension;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.int_data["old_state"] = old_state;
    ev.int_data["new_state"] = new_state;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::entity_created(
    uint64_t entity_id, const std::string& type_name,
    const std::string& dimension, int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::ENTITY_CREATED;
    ev.source_id = entity_id;
    ev.source_dimension = dimension;
    ev.string_data["type_name"] = type_name;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::entity_destroyed(
    uint64_t entity_id, const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::ENTITY_DESTROYED;
    ev.source_id = entity_id;
    ev.source_dimension = dimension;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::entity_damaged(
    uint64_t entity_id, float damage, const std::string& source_dimension) {
    GameEvent ev;
    ev.type = GameEventType::ENTITY_DAMAGED;
    ev.source_id = entity_id;
    ev.source_dimension = source_dimension;
    ev.float_data["damage"] = static_cast<double>(damage);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::terrain_changed(
    const std::string& dimension,
    int cx, int cy, int cz,
    int local_x, int local_y, int local_z,
    int old_material, int new_material) {
    GameEvent ev;
    ev.type = GameEventType::TERRAIN_CHANGED;
    ev.source_dimension = dimension;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.cell_x = local_x;
    ev.cell_y = local_y;
    ev.cell_z = local_z;
    ev.int_data["old_material"] = old_material;
    ev.int_data["new_material"] = new_material;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::item_dropped(
    uint64_t item_id, int32_t count, const std::string& dimension,
    int cx, int cy, int cz,
    int local_x, int local_y, int local_z) {
    GameEvent ev;
    ev.type = GameEventType::ITEM_DROPPED;
    ev.source_id = item_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.cell_x = local_x;
    ev.cell_y = local_y;
    ev.cell_z = local_z;
    ev.int_data["count"] = count;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::item_picked_up(
    uint64_t item_id, int32_t count, const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::ITEM_PICKED_UP;
    ev.source_id = item_id;
    ev.source_dimension = dimension;
    ev.int_data["count"] = count;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::player_inventory_changed() {
    GameEvent ev;
    ev.type = GameEventType::PLAYER_INVENTORY_CHANGED;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::player_equipment_changed(
    int slot_type, uint64_t old_item_id, uint64_t new_item_id) {
    GameEvent ev;
    ev.type = GameEventType::PLAYER_EQUIPMENT_CHANGED;
    ev.int_data["slot_type"] = slot_type;
    ev.int_data["old_item_id"] = static_cast<int64_t>(old_item_id);
    ev.int_data["new_item_id"] = static_cast<int64_t>(new_item_id);
    ev.timestamp = now_ms();
    return ev;
}

// --- Region event factories ---

GameEvent GameEvent::region_created(
    uint64_t region_id, const std::string& region_type,
    const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_CREATED;
    ev.source_id = region_id;
    ev.source_dimension = dimension;
    ev.string_data["region_type"] = region_type;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::region_destroyed(
    uint64_t region_id, const std::string& region_type,
    const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_DESTROYED;
    ev.source_id = region_id;
    ev.source_dimension = dimension;
    ev.string_data["region_type"] = region_type;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::region_merged(
    uint64_t merged_id, uint64_t absorbed_id,
    const std::string& region_type, const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_MERGED;
    ev.source_id = merged_id;
    ev.source_dimension = dimension;
    ev.string_data["region_type"] = region_type;
    ev.int_data["absorbed_id"] = static_cast<int64_t>(absorbed_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::region_split(
    uint64_t original_id, uint64_t new_id,
    const std::string& region_type, const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_SPLIT;
    ev.source_id = original_id;
    ev.source_dimension = dimension;
    ev.string_data["region_type"] = region_type;
    ev.int_data["new_id"] = static_cast<int64_t>(new_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::region_pollution_changed(
    uint64_t region_id, double old_level, double new_level,
    const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_POLLUTION_CHANGED;
    ev.source_id = region_id;
    ev.source_dimension = dimension;
    ev.float_data["old_level"] = old_level;
    ev.float_data["new_level"] = new_level;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::region_temperature_changed(
    uint64_t region_id, double old_temp, double new_temp,
    const std::string& dimension) {
    GameEvent ev;
    ev.type = GameEventType::REGION_TEMPERATURE_CHANGED;
    ev.source_id = region_id;
    ev.source_dimension = dimension;
    ev.float_data["old_temp"] = old_temp;
    ev.float_data["new_temp"] = new_temp;
    ev.timestamp = now_ms();
    return ev;
}

// --- Source law event factories ---

GameEvent GameEvent::source_law_changed(uint64_t player_handle) {
    GameEvent ev;
    ev.type = GameEventType::SOURCE_LAW_CHANGED;
    ev.source_id = player_handle;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::organ_transformed(
    uint64_t player_handle, int slot, int element) {
    GameEvent ev;
    ev.type = GameEventType::ORGAN_TRANSFORMED;
    ev.source_id = player_handle;
    ev.int_data["slot"] = slot;
    ev.int_data["element"] = element;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::organ_purified(
    uint64_t player_handle, int slot) {
    GameEvent ev;
    ev.type = GameEventType::ORGAN_PURIFIED;
    ev.source_id = player_handle;
    ev.int_data["slot"] = slot;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::stability_changed(
    uint64_t player_handle, float old_val, float new_val) {
    GameEvent ev;
    ev.type = GameEventType::STABILITY_CHANGED;
    ev.source_id = player_handle;
    ev.float_data["old_val"] = static_cast<double>(old_val);
    ev.float_data["new_val"] = static_cast<double>(new_val);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::mutation_changed(
    uint64_t player_handle, float old_val, float new_val) {
    GameEvent ev;
    ev.type = GameEventType::MUTATION_CHANGED;
    ev.source_id = player_handle;
    ev.float_data["old_val"] = static_cast<double>(old_val);
    ev.float_data["new_val"] = static_cast<double>(new_val);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::mana_changed(
    uint64_t player_handle, int old_val, int new_val) {
    GameEvent ev;
    ev.type = GameEventType::MANA_CHANGED;
    ev.source_id = player_handle;
    ev.int_data["old_val"] = old_val;
    ev.int_data["new_val"] = new_val;
    ev.timestamp = now_ms();
    return ev;
}

// --- Satiation event factories ---

GameEvent GameEvent::satiation_changed(
    uint64_t player_handle, float old_val, float new_val) {
    GameEvent ev;
    ev.type = GameEventType::SATIATION_CHANGED;
    ev.source_id = player_handle;
    ev.float_data["old_val"] = static_cast<double>(old_val);
    ev.float_data["new_val"] = static_cast<double>(new_val);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::hunger_level_changed(
    uint64_t player_handle, int old_level, int new_level) {
    GameEvent ev;
    ev.type = GameEventType::HUNGER_LEVEL_CHANGED;
    ev.source_id = player_handle;
    ev.int_data["old_level"] = old_level;
    ev.int_data["new_level"] = new_level;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::source_essence_changed(
    uint64_t player_handle, float old_total, float new_total) {
    GameEvent ev;
    ev.type = GameEventType::SOURCE_ESSENCE_CHANGED;
    ev.source_id = player_handle;
    ev.float_data["old_total"] = static_cast<double>(old_total);
    ev.float_data["new_total"] = static_cast<double>(new_total);
    ev.timestamp = now_ms();
    return ev;
}

// --- Ecosystem event factories ---

GameEvent GameEvent::ecosystem_population_changed(
    const std::string& dimension,
    int cx, int cy, int cz,
    float old_veg, float new_veg,
    float old_herb, float new_herb,
    float old_pred, float new_pred) {
    GameEvent ev;
    ev.type = GameEventType::ECOSYSTEM_POPULATION_CHANGED;
    ev.source_dimension = dimension;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.float_data["old_veg"] = static_cast<double>(old_veg);
    ev.float_data["new_veg"] = static_cast<double>(new_veg);
    ev.float_data["old_herb"] = static_cast<double>(old_herb);
    ev.float_data["new_herb"] = static_cast<double>(new_herb);
    ev.float_data["old_pred"] = static_cast<double>(old_pred);
    ev.float_data["new_pred"] = static_cast<double>(new_pred);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_spawned(
    uint64_t creature_id, const std::string& species_key,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_SPAWNED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.string_data["species_key"] = species_key;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_despawned(
    uint64_t creature_id, const std::string& species_key,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_DESPAWNED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.string_data["species_key"] = species_key;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_damaged(
    uint64_t creature_id, uint16_t species_id,
    float damage, float remaining_health,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_DAMAGED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.float_data["damage"] = static_cast<double>(damage);
    ev.float_data["remaining_health"] = static_cast<double>(remaining_health);
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_killed(
    uint64_t creature_id, uint16_t species_id,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_KILLED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_moved(
    uint64_t creature_id, const std::string& species_key,
    float pos_x, float pos_y, float pos_z) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_MOVED;
    ev.source_id = creature_id;
    ev.string_data["species_key"] = species_key;
    ev.float_data["pos_x"] = static_cast<double>(pos_x);
    ev.float_data["pos_y"] = static_cast<double>(pos_y);
    ev.float_data["pos_z"] = static_cast<double>(pos_z);
    ev.timestamp = now_ms();
    return ev;
}

// --- Captive / husbandry event factories ---

GameEvent GameEvent::creature_captured(
    uint64_t creature_id, uint16_t species_id,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_CAPTURED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_tamed(
    uint64_t creature_id, uint16_t species_id,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_TAMED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_bred(
    uint64_t creature_id, uint16_t species_id,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_BRED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::creature_grown(
    uint64_t creature_id, uint16_t species_id,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CREATURE_GROWN;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::captive_creature_added(
    uint64_t creature_id, const std::string& species_key,
    uint16_t species_id, uint8_t age_stage, bool is_tamed,
    float pos_x, float pos_y, float pos_z,
    const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CAPTIVE_CREATURE_ADDED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.string_data["species_key"] = species_key;
    ev.int_data["species_id"] = static_cast<int64_t>(species_id);
    ev.int_data["age_stage"] = static_cast<int64_t>(age_stage);
    ev.int_data["is_tamed"] = is_tamed ? 1 : 0;
    ev.float_data["pos_x"] = static_cast<double>(pos_x);
    ev.float_data["pos_y"] = static_cast<double>(pos_y);
    ev.float_data["pos_z"] = static_cast<double>(pos_z);
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::captive_creature_removed(
    uint64_t creature_id, const std::string& dimension,
    int cx, int cy, int cz) {
    GameEvent ev;
    ev.type = GameEventType::CAPTIVE_CREATURE_REMOVED;
    ev.source_id = creature_id;
    ev.source_dimension = dimension;
    ev.chunk_x = cx; ev.chunk_y = cy; ev.chunk_z = cz;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::captive_creature_moved(
    uint64_t creature_id, const std::string& species_key,
    float pos_x, float pos_y, float pos_z) {
    GameEvent ev;
    ev.type = GameEventType::CAPTIVE_CREATURE_MOVED;
    ev.source_id = creature_id;
    ev.string_data["species_key"] = species_key;
    ev.float_data["pos_x"] = static_cast<double>(pos_x);
    ev.float_data["pos_y"] = static_cast<double>(pos_y);
    ev.float_data["pos_z"] = static_cast<double>(pos_z);
    ev.timestamp = now_ms();
    return ev;
}

} // namespace science_and_theology
