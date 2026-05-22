#pragma once

#include <unordered_map>

#include "chunk_data.hpp"

namespace science_and_theology {

// Single source of truth for all world data.
// Manages chunks across all layers. Godot nodes act only as rendering proxies.
//
// Usage:
//   WorldData world;
//   ChunkData chunk;
//   chunk.chunk_x = 0;
//   chunk.chunk_y = 0;
//   world.set_chunk("surface", chunk);
//   if (auto* c = world.get_chunk("surface", 0, 0)) { ... }
class WorldData {
public:
    WorldData() = default;
    ~WorldData() = default;

    // Disallow copy, allow move.
    WorldData(const WorldData&) = delete;
    WorldData& operator=(const WorldData&) = delete;
    WorldData(WorldData&&) = default;
    WorldData& operator=(WorldData&&) = default;

    // --- Chunk access ---

    bool has_chunk(const std::string& layer_id, int chunk_x, int chunk_y) const;

    ChunkData* get_chunk(const std::string& layer_id, int chunk_x, int chunk_y);
    const ChunkData* get_chunk(
        const std::string& layer_id, int chunk_x, int chunk_y) const;

    // Sets or replaces a chunk. Takes ownership.
    void set_chunk(const std::string& layer_id, int chunk_x, int chunk_y,
                   ChunkData chunk);

    // Removes a chunk. Does nothing if the chunk does not exist.
    void remove_chunk(const std::string& layer_id, int chunk_x, int chunk_y);

    // Returns all chunk keys for iterating over the world.
    std::vector<ChunkKey> all_chunk_keys() const;

    // Removes all chunks.
    void clear();

    // Returns the total number of loaded chunks.
    size_t chunk_count() const { return chunks_.size(); }

private:
    ChunkKey make_key(const std::string& layer_id, int chunk_x, int chunk_y) const;

    std::unordered_map<ChunkKey, ChunkData> chunks_;
};

// --- Inline implementations ---

inline ChunkKey WorldData::make_key(
    const std::string& layer_id, int chunk_x, int chunk_y) const {
    return ChunkKey{layer_id, chunk_x, chunk_y};
}

inline bool WorldData::has_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y) const {
    return chunks_.find(make_key(layer_id, chunk_x, chunk_y)) != chunks_.end();
}

inline ChunkData* WorldData::get_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y) {
    auto it = chunks_.find(make_key(layer_id, chunk_x, chunk_y));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline const ChunkData* WorldData::get_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y) const {
    auto it = chunks_.find(make_key(layer_id, chunk_x, chunk_y));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline void WorldData::set_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y, ChunkData chunk) {
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunks_[make_key(layer_id, chunk_x, chunk_y)] = std::move(chunk);
}

inline void WorldData::remove_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y) {
    chunks_.erase(make_key(layer_id, chunk_x, chunk_y));
}

inline std::vector<ChunkKey> WorldData::all_chunk_keys() const {
    std::vector<ChunkKey> keys;
    keys.reserve(chunks_.size());
    for (const auto& pair : chunks_) {
        keys.push_back(pair.first);
    }
    return keys;
}

inline void WorldData::clear() {
    chunks_.clear();
}

} // namespace science_and_theology