#include "ae2_pattern_cache.hpp"
#include "material/tool_items.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace science_and_theology::gt {

struct PatternDataCache::Impl {
    // Next ID to allocate.
    ItemId next_id = ENCODED_PATTERN_BASE;

    // Pattern data lookup by ID.
    std::unordered_map<ItemId, EncodedPatternData> data_by_id;

    // For deduplication: hash -> ItemId.
    // The hash is computed from inputs, outputs, and is_crafting.
    std::unordered_map<size_t, ItemId> hash_to_id;

    // Compute a hash for pattern data.
    size_t compute_hash(const std::vector<ResourceStack>& inputs,
                         const std::vector<ResourceStack>& outputs,
                         bool is_crafting) const {
        size_t h = is_crafting ? 12345 : 67890;
        for (const auto& s : inputs) {
            h ^= static_cast<size_t>(s.item_id()) * 0x9e3779b9 + 0xbf58476d;
            h ^= static_cast<size_t>(s.amount) * 0x9e3779b9;
        }
        for (const auto& s : outputs) {
            h ^= static_cast<size_t>(s.item_id()) * 0x9e3779b9 + 0xbf58476d;
            h ^= static_cast<size_t>(s.amount) * 0x9e3779b9;
        }
        return h;
    }
};

PatternDataCache::Impl& PatternDataCache::impl() {
    static Impl i;
    return i;
}

ItemId PatternDataCache::register_pattern(
        const std::vector<ResourceStack>& inputs,
        const std::vector<ResourceStack>& outputs,
        bool is_crafting,
        const char* name_hint) {
    auto& i = impl();
    size_t hash = i.compute_hash(inputs, outputs, is_crafting);

    // Check for existing pattern.
    auto existing = i.hash_to_id.find(hash);
    if (existing != i.hash_to_id.end()) {
        return existing->second;
    }

    // Allocate new ID.
    ItemId id = i.next_id++;

    // Store data.
    EncodedPatternData data;
    data.inputs = inputs;
    data.outputs = outputs;
    data.is_crafting = is_crafting;

    if (name_hint && name_hint[0] != '\0') {
        data.name = "Pattern: ";
        data.name += name_hint;
    } else {
        data.name = "Encoded Pattern #";
        data.name += std::to_string(static_cast<uint32_t>(id));
    }

    i.data_by_id[id] = std::move(data);
    i.hash_to_id[hash] = id;

    return id;
}

const EncodedPatternData* PatternDataCache::get_pattern_data(ItemId id) {
    auto& i = impl();
    auto it = i.data_by_id.find(id);
    if (it != i.data_by_id.end()) {
        return &it->second;
    }
    return nullptr;
}

bool PatternDataCache::is_encoded_pattern(ItemId id) {
    auto& i = impl();
    return i.data_by_id.find(id) != i.data_by_id.end();
}

const char* PatternDataCache::get_pattern_name(ItemId id) {
    auto* data = get_pattern_data(id);
    if (data) {
        return data->name.c_str();
    }
    return nullptr;
}

} // namespace science_and_theology::gt
