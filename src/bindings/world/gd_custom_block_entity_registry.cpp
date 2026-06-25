#include "gd_custom_block_entity_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <unordered_map>

namespace science_and_theology {

using namespace godot;

namespace {
struct CustomTypeHandler {
    Callable tick_callback;
    Callable serialize_callback;
    Callable deserialize_callback;
    bool disabled = false;  // set to true on crash for isolation
};

std::unordered_map<std::string, CustomTypeHandler> g_custom_handlers;
} // namespace

bool GDCustomBlockEntityRegistry::register_type(
    const String& type_key,
    const Callable& tick_callback,
    const Callable& serialize_callback,
    const Callable& deserialize_callback) {
    if (type_key.is_empty()) return false;
    std::string key = type_key.utf8().get_data();
    if (g_custom_handlers.count(key) > 0) return false;
    g_custom_handlers[key] = {
        tick_callback, serialize_callback, deserialize_callback, false
    };
    return true;
}

bool GDCustomBlockEntityRegistry::unregister_type(const String& type_key) {
    std::string key = type_key.utf8().get_data();
    return g_custom_handlers.erase(key) > 0;
}

bool GDCustomBlockEntityRegistry::has_type(const String& type_key) {
    return g_custom_handlers.count(type_key.utf8().get_data()) > 0;
}

int64_t GDCustomBlockEntityRegistry::get_type_count() {
    return static_cast<int64_t>(g_custom_handlers.size());
}

void GDCustomBlockEntityRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDCustomBlockEntityRegistry",
        D_METHOD("register_type", "type_key", "tick_callback",
                 "serialize_callback", "deserialize_callback"),
        &GDCustomBlockEntityRegistry::register_type);
    ClassDB::bind_static_method("GDCustomBlockEntityRegistry",
        D_METHOD("unregister_type", "type_key"),
        &GDCustomBlockEntityRegistry::unregister_type);
    ClassDB::bind_static_method("GDCustomBlockEntityRegistry",
        D_METHOD("has_type", "type_key"),
        &GDCustomBlockEntityRegistry::has_type);
    ClassDB::bind_static_method("GDCustomBlockEntityRegistry",
        D_METHOD("get_type_count"),
        &GDCustomBlockEntityRegistry::get_type_count);
}

} // namespace science_and_theology
