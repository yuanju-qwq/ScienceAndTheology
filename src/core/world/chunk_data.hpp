#pragma once

#include <cstdint>
#include <string>
#include <functional>

#include "terrain_data.hpp"

namespace science_and_theology {

// Lifecycle state of a chunk in the streaming system.
enum class ChunkState : uint8_t {
    UNLOADED   = 0,
    GENERATING = 1,
    GENERATED  = 2,
    ACTIVE     = 3,
    SLEEPING   = 4,
};

// Display name for each chunk state.
constexpr const char* kChunkStateNames[] = {
    "Unloaded", "Generating", "Generated", "Active", "Sleeping",
};

// Identifies a unique chunk in the world across all layers.
struct ChunkKey {
    std::string layer_id;
    int chunk_x = 0;
    int chunk_y = 0;

    bool operator==(const ChunkKey& other) const {
        return chunk_x == other.chunk_x
            && chunk_y == other.chunk_y
            && layer_id == other.layer_id;
    }

    bool operator!=(const ChunkKey& other) const {
        return !(*this == other);
    }
};

// World data shard. One chunk represents a fixed-size region of the world.
// This is a pure data container, not a Godot node.
struct ChunkData {
    // Chunk size in cells. Must match the global ChunkConfig.
    static constexpr int kChunkSize = 32;

    int chunk_x = 0;
    int chunk_y = 0;
    ChunkState state = ChunkState::UNLOADED;

    TerrainData terrain;

    // Future fields per design doc:
    // std::vector<EntityId> entities;
    // std::vector<MachineId> machines;
    // std::vector<ConnectorId> connectors;
};

} // namespace science_and_theology

// std::hash specialization for ChunkKey to use in unordered_map.
template <>
struct std::hash<science_and_theology::ChunkKey> {
    size_t operator()(const science_and_theology::ChunkKey& key) const {
        size_t h = std::hash<std::string>()(key.layer_id);
        h ^= std::hash<int>()(key.chunk_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(key.chunk_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};