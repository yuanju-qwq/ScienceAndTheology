#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/resource_key.hpp"

namespace science_and_theology::gt {

// A flat resource identity for fast map lookups.
// Converts to/from ResourceKey without polymorphic dispatch on hot paths.
// For items, secondary carries the ItemKey secondary data (durability, etc.).
struct ResourceId {
    const ResourceKeyType* type = nullptr;
    uint32_t raw_id = 0;
    uint64_t secondary = 0;

    ResourceId() = default;
    ResourceId(const ResourceKeyType* t, uint32_t rid, uint64_t sec = 0)
        : type(t), raw_id(rid), secondary(sec) {}

    // Convert from a ResourceKey.
    static ResourceId from_key(const ResourceKey& key);

    // Convert back to a ResourceKey.
    std::unique_ptr<ResourceKey> to_key() const;

    bool operator==(const ResourceId& other) const {
        return type == other.type
            && raw_id == other.raw_id
            && secondary == other.secondary;
    }

    struct Hash {
        size_t operator()(const ResourceId& rid) const {
            size_t h = reinterpret_cast<size_t>(rid.type);
            h ^= static_cast<size_t>(rid.raw_id) * 0x9e3779b9;
            h ^= static_cast<size_t>(rid.secondary) * 0x100000001b3;
            return h;
        }
    };
};

} // namespace science_and_theology::gt
