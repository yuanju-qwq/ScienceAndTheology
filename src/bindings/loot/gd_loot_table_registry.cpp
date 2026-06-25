#include "gd_loot_table_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <unordered_set>

#include "core/loot/loot_table_registry.hpp"

namespace science_and_theology {

using namespace godot;

namespace {
// Persistent string storage for loot table keys and item keys.
std::unordered_set<std::string> g_string_pool;

const char* intern_string(const std::string& s) {
    auto it = g_string_pool.find(s);
    if (it != g_string_pool.end()) {
        return it->c_str();
    }
    auto result = g_string_pool.insert(s);
    return result.first->c_str();
}
} // namespace

bool GDLootTableRegistry::register_table(const String& table_key,
                                           const Array& entries) {
    if (table_key.is_empty()) return false;

    std::vector<gt::LootEntry> cpp_entries;
    cpp_entries.reserve(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        Variant v = entries[i];
        if (v.get_type() != Variant::DICTIONARY) continue;
        Dictionary ed = v;

        String item_key = ed.get("item_key", "");
        if (item_key.is_empty()) continue;

        gt::LootEntry entry;
        entry.item_key = intern_string(std::string(item_key.utf8().get_data()));
        entry.weight = static_cast<int32_t>(ed.get("weight", 1));
        if (entry.weight <= 0) entry.weight = 1;
        entry.min_count = static_cast<int32_t>(ed.get("min_count", 1));
        entry.max_count = static_cast<int32_t>(ed.get("max_count", 1));
        if (entry.max_count < entry.min_count) {
            entry.max_count = entry.min_count;
        }
        cpp_entries.push_back(entry);
    }

    const char* key_ptr = intern_string(std::string(table_key.utf8().get_data()));
    return gt::LootTableRegistry::register_table(key_ptr, cpp_entries);
}

bool GDLootTableRegistry::has_table(const String& table_key) {
    return gt::LootTableRegistry::get_table(table_key.utf8().get_data()) != nullptr;
}

int64_t GDLootTableRegistry::get_entry_count(const String& table_key) {
    const gt::LootTable* table = gt::LootTableRegistry::get_table(
        table_key.utf8().get_data());
    return table != nullptr ? static_cast<int64_t>(table->entries.size()) : 0;
}

int64_t GDLootTableRegistry::get_table_count() {
    return static_cast<int64_t>(gt::LootTableRegistry::get_table_count());
}

void GDLootTableRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDLootTableRegistry",
        D_METHOD("register_table", "table_key", "entries"),
        &GDLootTableRegistry::register_table);
    ClassDB::bind_static_method("GDLootTableRegistry",
        D_METHOD("has_table", "table_key"),
        &GDLootTableRegistry::has_table);
    ClassDB::bind_static_method("GDLootTableRegistry",
        D_METHOD("get_entry_count", "table_key"),
        &GDLootTableRegistry::get_entry_count);
    ClassDB::bind_static_method("GDLootTableRegistry",
        D_METHOD("get_table_count"),
        &GDLootTableRegistry::get_table_count);
}

} // namespace science_and_theology
