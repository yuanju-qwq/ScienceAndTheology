#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDItemRegistry — GDScript binding for gt::ItemRegistry
// ============================================================
//
// Allows content packs (and builtin GD scripts) to register new
// non-material items at load time. All dynamically-registered
// non-material items (builtin compounds, mod items, etc.) share a
// single key→id registry in the dynamic range
// [kDynamicItemBase, kDynamicItemMax) and do not collide with
// builtin material items or encoded patterns.
//
// Saves should reference items by item_key (string) for stability;
// the numeric ItemId is runtime-only and may shift across hot reloads
// or content changes.
//
// GDScript usage:
//   var id = GDItemRegistry.register_item({
//       "item_key": "my_mod:custom_widget",
//       "title_key": "item.my_mod.custom_widget",
//   })
//   var key = GDItemRegistry.get_item_key(id)
class GDItemRegistry : public godot::Object {
    GDCLASS(GDItemRegistry, godot::Object)

public:
    GDItemRegistry() = default;
    ~GDItemRegistry() override = default;

    // Register a non-material item with auto-assigned ID.
    // Returns the assigned ItemId, or 0 on failure.
    // Dictionary fields:
    //   item_key (String, required): globally unique stable key.
    //   title_key (String, optional): localization key.
    //   item_id (int, optional): explicit dynamic-range ID for builtins.
    // Idempotent: returns existing ItemId if item_key already registered.
    static int64_t register_item(const godot::Dictionary& def);

    // Look up an item by stable key. Returns ItemId or 0 if not found.
    static int64_t get_item_id(const godot::String& key);

    // Look up the stable key for an item id. Returns "" if not found.
    static godot::String get_item_key(int64_t id);

    // Look up the title localization key for an item id.
    static godot::String get_item_title_key(int64_t id);

    // Returns true if the item id refers to a valid registered item.
    static bool is_valid_item(int64_t id);

    // Returns true if the item id is in the dynamic (non-material) item range.
    static bool is_dynamic_item(int64_t id);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
