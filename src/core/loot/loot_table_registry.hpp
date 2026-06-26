#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// ============================================================
// LootTable — weighted drop table for blocks, creatures, etc.
// ============================================================
//
// Each loot table has a stable key (e.g. "builtin:stone", "my_mod:boss")
// and a list of entries. Each entry specifies an item key, a weight,
// and optional min/max count bounds.
//
// Roll semantics: pick one entry weighted by weight, then produce a
// random count in [min, max]. Empty tables roll nothing.

struct LootEntry {
    const char* item_key = "";   // stable item key, e.g. "ingot.iron"
    int32_t weight = 1;          // relative probability weight
    int32_t min_count = 1;       // inclusive lower bound
    int32_t max_count = 1;       // inclusive upper bound
};

struct LootTable {
    const char* key = "";
    std::vector<LootEntry> entries;
};

class LootTableRegistry {
public:
    static void initialize();
    static void reset();

    // Register a loot table. Replaces an existing table with the same key.
    // Returns true on success. The key and entry item_key strings must
    // outlive the registry (caller-managed lifetime, typically via a
    // persistent string pool in the GD binding layer).
    static bool register_table(const char* key, const LootEntry* entries,
                                size_t entry_count);

    // Convenience: register a table from a std::vector.
    static bool register_table(const char* key,
                                const std::vector<LootEntry>& entries);

    // Look up a table by key. Returns nullptr if not found.
    static const LootTable* get_table(const char* key);

    // Returns the total number of registered tables.
    static size_t get_table_count();

private:
    static std::vector<LootTable>& registry();
};

} // namespace science_and_theology::gt
