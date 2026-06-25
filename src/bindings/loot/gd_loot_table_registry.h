#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDLootTableRegistry — GDScript binding for gt::LootTableRegistry
// ============================================================
//
// Allows content packs to register weighted drop tables for blocks,
// creatures, and any other drop source.
//
// GDScript usage:
//   GDLootTableRegistry.register_table("my_mod:boss_drops", [
//       {"item_key": "ingot.gold", "weight": 10, "min_count": 1, "max_count": 3},
//       {"item_key": "my_mod:legendary_sword", "weight": 1, "min_count": 1, "max_count": 1},
//   ])
class GDLootTableRegistry : public godot::Object {
    GDCLASS(GDLootTableRegistry, godot::Object)

public:
    GDLootTableRegistry() = default;
    ~GDLootTableRegistry() override = default;

    // Register a loot table. Returns true on success.
    // Each entry Dictionary supports:
    //   item_key (String, required): stable item key.
    //   weight (int, optional, default 1): relative probability.
    //   min_count (int, optional, default 1): inclusive lower bound.
    //   max_count (int, optional, default 1): inclusive upper bound.
    static bool register_table(const godot::String& table_key,
                                const godot::Array& entries);

    // Returns true if a table with the given key exists.
    static bool has_table(const godot::String& table_key);

    // Returns the number of entries in a table, or 0 if not found.
    static int64_t get_entry_count(const godot::String& table_key);

    // Returns the total number of registered tables.
    static int64_t get_table_count();

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
