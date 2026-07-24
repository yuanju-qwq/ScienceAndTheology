#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../world/chunk_data.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

// Bitmask of categories that can be dirty (needing sync).
enum class SyncFlags : uint32_t {
    NONE            = 0,
    TERRAIN         = 1 << 0,
    MACHINE_STATE   = 1 << 1,
    MACHINE_INVENTORY = 1 << 2,
    POWER_GRID      = 1 << 3,
    FLUID_NET       = 1 << 4,
    ITEM_NET        = 1 << 5,
    CONNECTOR       = 1 << 6,
    ENTITY          = 1 << 7,
    SOURCE_LAW      = 1 << 8,
    ALL             = 0xFFFFFFFF,
};

inline SyncFlags operator|(SyncFlags a, SyncFlags b) {
    return static_cast<SyncFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline SyncFlags operator&(SyncFlags a, SyncFlags b) {
    return static_cast<SyncFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline SyncFlags& operator|=(SyncFlags& a, SyncFlags b) {
    a = a | b;
    return a;
}

// A delta record describing what changed since the last sync point.
// Used to efficiently synchronize C++ core state to rendering proxies.
struct StateDelta {
    SyncFlags flags = SyncFlags::NONE;
    int64_t timestamp = 0;

    std::vector<EntityId> entities_created;
    std::vector<EntityId> entities_destroyed;
    std::vector<std::pair<EntityId, uint8_t>> machine_state_changes;
    std::vector<ChunkKey> chunks_modified;
};

} // namespace science_and_theology
