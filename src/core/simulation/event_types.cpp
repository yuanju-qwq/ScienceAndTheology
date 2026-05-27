#include "event_types.hpp"
#include <chrono>

namespace science_and_theology {

static int64_t now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

GameEvent GameEvent::machine_state_changed(
    uint64_t machine_id, int old_state, int new_state) {
    GameEvent ev;
    ev.type = GameEventType::MACHINE_STATE_CHANGED;
    ev.source_id = machine_id;
    ev.int_data["old_state"] = old_state;
    ev.int_data["new_state"] = new_state;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::machine_recipe_completed(
    uint64_t machine_id, const char* recipe_name) {
    GameEvent ev;
    ev.type = GameEventType::MACHINE_RECIPE_COMPLETED;
    ev.source_id = machine_id;
    ev.string_data["recipe"] = recipe_name;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::machine_error(
    uint64_t machine_id, const char* error_code, const char* message) {
    GameEvent ev;
    ev.type = GameEventType::MACHINE_ERROR;
    ev.source_id = machine_id;
    ev.string_data["error_code"] = error_code;
    ev.string_data["message"] = message;
    ev.timestamp = now_ms();
    return ev;
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
    int cx, int cy, const std::string& layer,
    int old_state, int new_state) {
    GameEvent ev;
    ev.type = GameEventType::CHUNK_STATE_CHANGED;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.source_layer = layer;
    ev.int_data["old_state"] = old_state;
    ev.int_data["new_state"] = new_state;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::entity_created(
    uint64_t entity_id, const std::string& type_name,
    int cx, int cy) {
    GameEvent ev;
    ev.type = GameEventType::ENTITY_CREATED;
    ev.source_id = entity_id;
    ev.string_data["type_name"] = type_name;
    ev.chunk_x = cx;
    ev.chunk_y = cy;
    ev.timestamp = now_ms();
    return ev;
}

GameEvent GameEvent::entity_destroyed(
    uint64_t entity_id, const std::string& layer) {
    GameEvent ev;
    ev.type = GameEventType::ENTITY_DESTROYED;
    ev.source_id = entity_id;
    ev.source_layer = layer;
    ev.timestamp = now_ms();
    return ev;
}

} // namespace science_and_theology
