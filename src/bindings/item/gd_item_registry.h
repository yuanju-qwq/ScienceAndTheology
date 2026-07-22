#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDItemRegistry — GDScript binding for gt::ItemRegistry
// ============================================================
//
// Historical GDScript binding for the retired explicit-ID registry. Current
// content registration is owned by GameContentRegistry and AngelScript.
//
class GDItemRegistry : public godot::Object {
    GDCLASS(GDItemRegistry, godot::Object)

public:
    GDItemRegistry() = default;
    ~GDItemRegistry() override = default;

    // Register a historical non-material item with an explicit ID.
    // Returns the assigned ItemId, or 0 on failure.
    // Dictionary fields:
    //   item_key (String, required): globally unique stable key.
    //   title_key (String, optional): localization key.
    //   item_id (int, required): explicit dynamic-range ID.
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
