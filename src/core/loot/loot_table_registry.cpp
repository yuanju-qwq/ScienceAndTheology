#include "loot_table_registry.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace science_and_theology::gt {

namespace {
std::vector<LootTable> g_loot_tables;
std::unordered_map<std::string, size_t> g_loot_table_index;
} // namespace

std::vector<LootTable>& LootTableRegistry::registry() {
    return g_loot_tables;
}

void LootTableRegistry::initialize() {
    g_loot_tables.clear();
    g_loot_table_index.clear();
}

bool LootTableRegistry::register_table(const char* key,
                                         const LootEntry* entries,
                                         size_t entry_count) {
    if (key == nullptr || key[0] == '\0') return false;

    std::string key_str(key);
    auto it = g_loot_table_index.find(key_str);
    LootTable* table = nullptr;
    if (it != g_loot_table_index.end()) {
        // Replace existing table.
        table = &g_loot_tables[it->second];
        table->entries.clear();
    } else {
        size_t idx = g_loot_tables.size();
        g_loot_tables.push_back({});
        table = &g_loot_tables[idx];
        table->key = key;
        g_loot_table_index[key_str] = idx;
    }

    table->entries.reserve(entry_count);
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].item_key != nullptr && entries[i].item_key[0] != '\0') {
            table->entries.push_back(entries[i]);
        }
    }
    return true;
}

bool LootTableRegistry::register_table(const char* key,
                                         const std::vector<LootEntry>& entries) {
    if (entries.empty()) {
        return register_table(key, nullptr, 0);
    }
    return register_table(key, entries.data(), entries.size());
}

const LootTable* LootTableRegistry::get_table(const char* key) {
    if (key == nullptr || key[0] == '\0') return nullptr;
    auto it = g_loot_table_index.find(key);
    if (it == g_loot_table_index.end()) return nullptr;
    return &g_loot_tables[it->second];
}

size_t LootTableRegistry::get_table_count() {
    return g_loot_tables.size();
}

} // namespace science_and_theology::gt
