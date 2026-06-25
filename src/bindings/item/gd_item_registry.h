#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDItemRegistry — GDScript binding for gt::ItemRegistry
// ============================================================
//
// Allows content packs to register new non-material items at load
// time. Mod items are assigned IDs in a dedicated high range
// (kModItemBase = 0x80000000) that does not collide with builtin
// material/non-material items or encoded patterns, preserving save
// compatibility.
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

    // Register a new mod item. Returns the assigned ItemId, or 0 on
    // failure (duplicate key or registry full).
    // Dictionary fields:
    //   item_key (String, required): globally unique stable key,
    //       e.g. "my_mod:custom_widget".
    //   title_key (String, optional): localization key. Defaults to
    //       item_key if omitted.
    static int64_t register_item(const godot::Dictionary& def);

    // Look up an item by stable key. Returns ItemId or 0 if not found.
    static int64_t get_item_id(const godot::String& key);

    // Look up the stable key for an item id. Returns "" if not found.
    static godot::String get_item_key(int64_t id);

    // Look up the title localization key for an item id.
    static godot::String get_item_title_key(int64_t id);

    // Returns true if the item id refers to a valid registered item.
    static bool is_valid_item(int64_t id);

    // Returns true if the item id is in the mod item range.
    static bool is_mod_item(int64_t id);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
