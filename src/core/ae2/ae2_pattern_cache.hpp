#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/resource_key.hpp"
#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// Data stored for an encoded pattern.
struct EncodedPatternData {
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> outputs;
    bool is_crafting = false;
    std::string name;
};

// Cache for encoded pattern data.
// Maps an encoded pattern's ItemId to its pattern data.
// When a recipe is encoded, the data is stored here and the pattern
// gets a unique ItemId (dynamically allocated from ENCODED_PATTERN_BASE).
class PatternDataCache {
public:
    // Register a new pattern. Returns the allocated ItemId.
    // If an identical pattern already exists, returns its existing ID.
    static ItemId register_pattern(
        const std::vector<ResourceStack>& inputs,
        const std::vector<ResourceStack>& outputs,
        bool is_crafting,
        const char* name_hint = nullptr);

    // Look up pattern data by ItemId. Returns nullptr if not found.
    static const EncodedPatternData* get_pattern_data(ItemId id);

    // Check if an ItemId belongs to an encoded pattern.
    static bool is_encoded_pattern(ItemId id);

    // Get the display name for an encoded pattern ItemId.
    // Returns nullptr if not a valid encoded pattern.
    static const char* get_pattern_name(ItemId id);

private:
    struct Impl;
    static Impl& impl();
};

} // namespace science_and_theology::gt
