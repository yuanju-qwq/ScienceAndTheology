#pragma once

#include <unordered_map>

#include "chunk_data.hpp"
#include "gameplay_config.hpp"

namespace science_and_theology {

// Single source of truth for all world data.
// Manages chunks across all dimensions. Godot nodes act only as rendering proxies.
//
// Usage:
//   WorldData world;
//   ChunkData chunk;
//   chunk.chunk_x = 0;
//   chunk.chunk_y = 0;
//   chunk.chunk_z = 0;
//   world.set_chunk("overworld", 0, 0, 0, chunk);
//   if (auto* c = world.get_chunk("overworld", 0, 0, 0)) { ... }
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

    bool has_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z) const;

    ChunkData* get_chunk(const std::string& dimension_id,
                         int chunk_x, int chunk_y, int chunk_z);
    const ChunkData* get_chunk(
        const std::string& dimension_id,
        int chunk_x, int chunk_y, int chunk_z) const;

    // Sets or replaces a chunk. Takes ownership.
    void set_chunk(const std::string& dimension_id,
                   int chunk_x, int chunk_y, int chunk_z, ChunkData chunk);

    // Removes a chunk. Does nothing if the chunk does not exist.
    void remove_chunk(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z);

    // Returns all chunk keys for iterating over the world.
    std::vector<ChunkKey> all_chunk_keys() const;

    // Removes all chunks.
    void clear();

    // Returns the total number of loaded chunks.
    size_t chunk_count() const { return chunks_.size(); }

    // --- Gameplay config ---

    // Runtime gameplay configuration (collapse, gravity fall, etc.).
    // Mutable at runtime, separate from frozen WorldGenConfigSnapshot.
    GameplayConfig& gameplay_config() { return gameplay_config_; }
    const GameplayConfig& gameplay_config() const { return gameplay_config_; }

    void set_gameplay_config(const GameplayConfig& config) {
        gameplay_config_ = config;
    }

private:
    ChunkKey make_key(const std::string& dimension_id,
                      int chunk_x, int chunk_y, int chunk_z) const;

    std::unordered_map<ChunkKey, ChunkData> chunks_;

    // Runtime gameplay configuration.
    GameplayConfig gameplay_config_;
};

// --- Inline implementations ---

inline ChunkKey WorldData::make_key(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    return ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z};
}

inline bool WorldData::has_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    return chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z)) != chunks_.end();
}

inline ChunkData* WorldData::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline const ChunkData* WorldData::get_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) const {
    auto it = chunks_.find(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

inline void WorldData::set_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    ChunkData chunk) {
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunks_[make_key(dimension_id, chunk_x, chunk_y, chunk_z)] = std::move(chunk);
}

inline void WorldData::remove_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    chunks_.erase(make_key(dimension_id, chunk_x, chunk_y, chunk_z));
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
