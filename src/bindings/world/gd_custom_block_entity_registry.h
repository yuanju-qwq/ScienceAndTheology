#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDCustomBlockEntityRegistry — mod custom block entity type registry
// ============================================================
//
// Allows content packs to register custom block entity types. Each
// type is identified by a stable type_key (e.g. "my_mod:custom_furnace")
// and bound to a GDScript handler object with the following methods:
//
//   func _tick(state: Dictionary, delta: float) -> Dictionary
//       Called every simulation tick. Receives the current state and
//       must return the updated state Dictionary.
//   func _serialize(state: Dictionary) -> String
//       Called when saving. Must return a JSON string of the state.
//   func _deserialize(json: String) -> Dictionary
//       Called when loading. Must parse the JSON and return the state.
//   func _on_placed(state: Dictionary) -> Dictionary
//       Called when the entity is first placed. Must return the
//       initial state (may be unchanged).
//   func _on_removed(state: Dictionary) -> void
//       Called when the entity is removed (cleanup hook).
//
// The C++ BlockEntityRegistry stores the opaque state_json blob; this
// binding layer translates between JSON and Dictionary on each tick.
//
// Tick dispatch is driven by GDTickSystem, which calls tick_custom_entities()
// every simulation tick. Crashes in handlers are isolated: the offending
// type is disabled for the rest of the session and a warning is logged.
class GDCustomBlockEntityRegistry : public godot::Object {
    GDCLASS(GDCustomBlockEntityRegistry, godot::Object)

public:
    GDCustomBlockEntityRegistry() = default;
    ~GDCustomBlockEntityRegistry() override = default;

    // Register a custom block entity type.
    // type_key: globally unique key, e.g. "my_mod:custom_furnace".
    // handler: GDScript object with _tick/_serialize/_deserialize methods.
    // Returns true on success, false if the type_key is already registered.
    static bool register_type(const godot::String& type_key,
                               const godot::Callable& tick_callback,
                               const godot::Callable& serialize_callback,
                               const godot::Callable& deserialize_callback);

    // Unregister a custom block entity type.
    static bool unregister_type(const godot::String& type_key);

    // Returns true if the given type_key is registered.
    static bool has_type(const godot::String& type_key);

    // Returns the number of registered custom types.
    static int64_t get_type_count();

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
