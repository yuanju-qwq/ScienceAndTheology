#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/resource_key.hpp"

namespace science_and_theology::gt {

// A flat resource identity for fast map lookups.
// Converts to/from ResourceKey without polymorphic dispatch on hot paths.
struct ResourceId {
    const ResourceKeyType* type = nullptr;
    uint32_t raw_id = 0;

    ResourceId() = default;
    ResourceId(const ResourceKeyType* t, uint32_t rid) : type(t), raw_id(rid) {}

    // Convert from a ResourceKey.
    static ResourceId from_key(const ResourceKey& key);

    // Convert back to a ResourceKey.
    std::unique_ptr<ResourceKey> to_key() const;

    bool operator==(const ResourceId& other) const {
        return type == other.type && raw_id == other.raw_id;
    }

    struct Hash {
        size_t operator()(const ResourceId& rid) const {
            return reinterpret_cast<size_t>(rid.type)
                 ^ (static_cast<size_t>(rid.raw_id) * 0x9e3779b9);
        }
    };
};

} // namespace science_and_theology::gt
