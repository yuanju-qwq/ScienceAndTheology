#pragma once

#include <unordered_map>
#include <vector>

#include "state_sync_common.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology {

// Server-side state synchronizer.
// Tracks dirty chunks and computes deltas for observers/clients.
// In single-player: C++ Core = server, Godot rendering = client.
// In multiplayer: C++ server sends deltas over network.
class StateSyncServer {
public:
    StateSyncServer() = default;

    // Attach a world data reference for delta computation.
    void set_world_data(WorldData* world) { world_data_ = world; }

    // Mark a chunk as dirty in the specified categories.
    void mark_dirty(const ChunkKey& key, SyncFlags flags);

    // Mark a chunk as dirty by coordinate + dimension (convenience).
    void mark_dirty(const std::string& dimension,
                    int cx, int cy, int cz,
                    SyncFlags flags);

    // Compute a delta since the last sync for the observed set of chunks.
    // After calling this, dirty flags for the returned categories are
    // auto-cleared. Call clear_dirty() to force-clear specific flags.
    StateDelta compute_delta(const std::vector<ChunkKey>& observed_chunks);

    // Create a full snapshot for a single chunk (for new observers).
    StateDelta create_snapshot(const ChunkKey& key) const;

    // Clear specific dirty flags for a chunk without computing a delta.
    void clear_dirty(const ChunkKey& key, SyncFlags flags);

    // Returns true if a chunk has any dirty flags set.
    bool is_dirty(const ChunkKey& key) const;

    // Returns the dirty flags for a chunk.
    SyncFlags get_dirty_flags(const ChunkKey& key) const;

    // Clears all dirty tracking.
    void clear_all();

    // Returns all currently dirty chunk keys.
    std::vector<ChunkKey> dirty_chunks() const;

    // Sets the current tick counter (called by TickSystem).
    void set_tick_counter(int64_t tick) { tick_counter_ = tick; }

private:
    WorldData* world_data_ = nullptr;
    std::unordered_map<ChunkKey, SyncFlags> dirty_chunks_;
    int64_t tick_counter_ = 0;
};

} // namespace science_and_theology
