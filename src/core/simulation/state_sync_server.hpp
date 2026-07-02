#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "state_sync_common.hpp"
#include "../player/player_handle.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology {

// Server-side state synchronizer.
// Tracks dirty chunks and computes deltas for observers/clients.
// In single-player: C++ Core = server, Godot rendering = client.
// In multiplayer: C++ server sends deltas over network.
//
// Observer model:
//   Each observer (identified by PlayerHandle) has its own view of the
//   world. compute_delta_for(observer, chunks) returns the delta for
//   that observer's visible chunks. In M1 (single-player), there is
//   exactly one observer (kSinglePlayerHandle) and the per-observer
//   tracking degenerates to the legacy compute_delta() behavior.
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

    // --- Per-observer API (multi-player) ---

    // Register an observer. Idempotent.
    void register_observer(PlayerHandle id);

    // Unregister an observer.
    void unregister_observer(PlayerHandle id);

    // Compute a delta for a specific observer over the given observed
    // chunks. In M1, this delegates to the shared dirty map (same as
    // compute_delta). Per-observer dirty tracking is M3+ scope.
    StateDelta compute_delta_for(PlayerHandle observer,
                                 const std::vector<ChunkKey>& observed_chunks);

    // M5: Compute deltas for multiple observers in batch. Each observer
    // gets a delta for their respective observed chunks. Dirty flags
    // are only cleared after ALL observers have been processed, so
    // multiple observers in the same dimension all see the same dirty
    // state (the single-observer compute_delta_for would clear flags
    // after the first observer, starving the rest).
    // observer_views: list of (observer_id, observed_chunks) pairs.
    // Returns: list of (observer_id, delta) pairs in the same order.
    std::vector<std::pair<PlayerHandle, StateDelta>> compute_deltas_batch(
        const std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>>& observer_views);

    // Returns true if the observer is registered.
    bool has_observer(PlayerHandle id) const;

    // --- Snapshot / dirty query API ---

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

    // Registered observers. In M1 this is informational; per-observer
    // dirty tracking is M3+ scope. Kept as a set for API completeness.
    std::unordered_map<PlayerHandle, bool> observers_;

    // Shared dirty-map delta computation used by compute_delta_for.
    // Clears dirty flags for the returned categories.
    StateDelta compute_delta(const std::vector<ChunkKey>& observed_chunks);
};

} // namespace science_and_theology
