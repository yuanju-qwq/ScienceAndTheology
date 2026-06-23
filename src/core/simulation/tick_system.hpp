#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

#include "simulation_system.hpp"
#include "event_bus.hpp"
#include "error_handler.hpp"
#include "state_sync_server.hpp"
#include "../player/player_id.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology {

// Unified simulation orchestrator.
// Owns the event bus, error handler, and state synchronizer.
// Manages subsystems and drives the tick loop with per-chunk scheduling.
//
// Tick phases per frame:
//   1. Update active/sleeping chunk sets based on player position.
//   2. For each subsystem (priority-ordered):
//      a. ACTIVE chunks: tick_active()
//      b. SLEEPING chunks (every N ticks, tiered by distance): tick_sleeping()
//   3. EventBus::process_queue() — drain deferred events.
//   4. StateSyncServer::set_tick_counter() — advance tick.
//
// Sleep chunk tiers (distance from player, Manhattan):
//   NEAR  — active_radius+1  ~ active_radius*2  — interval: sleep_near_interval_
//   MID   — active_radius*2+1 ~ active_radius*3  — interval: sleep_mid_interval_
//   FAR   — active_radius*3+1 ~ active_radius*4  — interval: sleep_far_interval_

// Distance tier for sleeping chunks.
enum class SleepTier : uint8_t {
    NEAR = 0,
    MID  = 1,
    FAR  = 2,
    COUNT = 3,
};

class TickSystem {
public:
    static constexpr int kTicksPerSecond = 20;

    // Default sleep intervals (in ticks).
    static constexpr int kDefaultSleepNearInterval = 20;   // 1 second
    static constexpr int kDefaultSleepMidInterval  = 60;   // 3 seconds
    static constexpr int kDefaultSleepFarInterval  = 120;  // 6 seconds

    explicit TickSystem(WorldData* world_data);
    ~TickSystem();

    // Disallow copy and move.
    TickSystem(const TickSystem&) = delete;
    TickSystem& operator=(const TickSystem&) = delete;
    TickSystem(TickSystem&&) = delete;
    TickSystem& operator=(TickSystem&&) = delete;

    // Register a simulation subsystem. Subsystems are sorted by priority.
    // Ownership transfers to TickSystem.
    void register_subsystem(std::unique_ptr<SimulationSystem> subsystem);

    // Advance the simulation by one game frame.
    // delta = real-time seconds since last frame.
    void tick(float delta);

    // --- Multi-player active set API ---
    //
    // The active/sleeping chunk sets are computed as the union of all
    // registered players' neighborhoods. A chunk is ACTIVE if it is
    // within active_radius_ of ANY player. Sleep tier is determined by
    // the minimum distance to any player.
    //
    // Single-player mode uses exactly one player (kSinglePlayerId = 1).

    // Register a player's chunk position. Adds the player to the active
    // set computation. If the player is already registered, updates its
    // position. Idempotent.
    void add_player_chunk(PlayerId id,
                          const std::string& dimension,
                          int cx, int cy, int cz);

    // Remove a player from the active set computation.
    void remove_player_chunk(PlayerId id);

    // Remove all players from the active set computation.
    void clear_player_chunks();

    // Returns the number of registered players driving the active set.
    size_t player_count() const { return player_chunks_.size(); }

    // Returns the dimension the player is currently in, or empty string
    // if the player is not registered. Used by the network layer to
    // filter deltas by player dimension (M5: multi-planet concurrent).
    std::string get_player_dimension(PlayerId id) const;

    // How many chunks around the player are ACTIVE (radius, Chebyshev).
    void set_active_radius(int radius) { active_radius_ = radius; }
    int active_radius() const { return active_radius_; }

    // If enabled, ChunkData.state can override the distance-only scheduler:
    // ACTIVE chunks are kept in active_chunks_, while SLEEPING chunks outside
    // the immediate interaction radius are scheduled only in sleeping tiers.
    void set_respect_external_chunk_state(bool enabled) {
        respect_external_chunk_state_ = enabled;
        player_chunks_dirty_ = true;
    }
    bool respect_external_chunk_state() const { return respect_external_chunk_state_; }

    // --- Sleep interval configuration ---

    // Sleep interval for NEAR tier (ticks between sleep ticks).
    void set_sleep_near_interval(int interval) {
        sleep_near_interval_ = std::max(1, interval);
    }
    int sleep_near_interval() const { return sleep_near_interval_; }

    // Sleep interval for MID tier.
    void set_sleep_mid_interval(int interval) {
        sleep_mid_interval_ = std::max(1, interval);
    }
    int sleep_mid_interval() const { return sleep_mid_interval_; }

    // Sleep interval for FAR tier.
    void set_sleep_far_interval(int interval) {
        sleep_far_interval_ = std::max(1, interval);
    }
    int sleep_far_interval() const { return sleep_far_interval_; }

    // Returns the sleep tier for a chunk based on its distance to the
    // nearest registered player. Returns FAR if no players are registered.
    SleepTier classify_sleep_tier(int cx, int cy, int cz) const;

    // --- Parallel execution control ---

    // Enable or disable parallel execution of subsystems.
    // When enabled, subsystems at the same priority level may run
    // concurrently (if all declare is_thread_safe()), and chunk-level
    // parallelism is used within each thread-safe subsystem.
    void set_parallel_enabled(bool enabled) { parallel_enabled_ = enabled; }
    bool parallel_enabled() const { return parallel_enabled_; }

    // Set the maximum number of worker threads for chunk-level parallelism.
    // Default: 0 = auto (hardware_concurrency - 1, minimum 1).
    void set_max_worker_threads(int count) { max_worker_threads_ = count; }
    int max_worker_threads() const { return max_worker_threads_; }

    // Access to shared services.
    EventBus* event_bus() { return event_bus_.get(); }
    const EventBus* event_bus() const { return event_bus_.get(); }
    ErrorHandler* error_handler() { return error_handler_.get(); }
    const ErrorHandler* error_handler() const { return error_handler_.get(); }
    StateSyncServer* state_sync() { return state_sync_.get(); }
    const StateSyncServer* state_sync() const { return state_sync_.get(); }
    WorldData* world_data() { return world_data_; }
    const WorldData* world_data() const { return world_data_; }

    // Returns the current tick counter.
    int64_t tick_count() const { return tick_counter_; }

    // Returns the set of chunk keys currently considered ACTIVE.
    const std::vector<ChunkKey>& active_chunks() const { return active_chunks_; }

    // Returns the sleeping chunk sets currently scheduled by distance tier.
    const std::vector<ChunkKey>& sleep_near_chunks() const { return sleep_near_chunks_; }
    const std::vector<ChunkKey>& sleep_mid_chunks() const { return sleep_mid_chunks_; }
    const std::vector<ChunkKey>& sleep_far_chunks() const { return sleep_far_chunks_; }

    // Rebuild diagnostics for state-aware shell streaming.
    int64_t external_active_chunk_count() const { return external_active_chunk_count_; }
    int64_t external_sleeping_chunk_count() const { return external_sleeping_chunk_count_; }
    int64_t skipped_external_sleeping_chunk_count() const { return skipped_external_sleeping_chunk_count_; }

private:
    // Rebuild the active/sleeping chunk sets.
    void rebuild_chunk_sets();

    // Returns true if a chunk should tick this frame (sleeping cadence).
    bool should_tick_sleeping(int cx, int cy, int cz, SleepTier tier) const;

    // Run a subsystem over a set of chunks (active or sleeping).
    // If the subsystem is thread-safe and parallel is enabled,
    // chunks are distributed across worker threads.
    void run_subsystem_chunks(
        SimulationSystem& sys,
        const std::vector<ChunkKey>& chunks,
        float delta,
        bool is_active,
        const TickContext* ctx);

    // Run all subsystems at a given priority level.
    // If all subsystems in the group are thread-safe and parallel
    // is enabled, they run concurrently.
    void run_priority_group(
        const std::vector<SimulationSystem*>& group,
        const std::vector<ChunkKey>& chunks,
        float delta,
        bool is_active,
        const TickContext* ctx);

    // Run all subsystems grouped by priority over a set of chunks.
    // Each priority group runs in order; within a group, subsystems
    // may run in parallel if all are thread-safe.
    void run_chunks_by_priority_groups(
        const std::vector<ChunkKey>& chunks,
        float delta,
        bool is_active,
        const TickContext* ctx);

    // Compute effective worker thread count.
    int effective_worker_threads() const;

    // Build a TickContext from currently registered subsystems.
    // Called once per tick, before any subsystem runs.
    TickContext build_tick_context() const;

    WorldData* world_data_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<ErrorHandler> error_handler_;
    std::unique_ptr<StateSyncServer> state_sync_;

    std::vector<std::unique_ptr<SimulationSystem>> subsystems_;

    // Multi-player active set tracking.
    // Each entry is a player's current chunk position.
    // Single-player mode: one entry with id = kSinglePlayerId.
    struct PlayerChunkPos {
        std::string dimension = "overworld";
        int cx = 0;
        int cy = 0;
        int cz = 0;
    };
    std::unordered_map<PlayerId, PlayerChunkPos> player_chunks_;

    // Cached flag: set when any player position changes, cleared after
    // rebuild_chunk_sets(). Avoids rebuilding every tick when stationary.
    bool player_chunks_dirty_ = true;

    int active_radius_ = 4;

    std::vector<ChunkKey> active_chunks_;

    // Sleep chunk sets, tiered by distance.
    std::vector<ChunkKey> sleep_near_chunks_;
    std::vector<ChunkKey> sleep_mid_chunks_;
    std::vector<ChunkKey> sleep_far_chunks_;

    // Sleep intervals (in ticks). Configurable at runtime.
    int sleep_near_interval_ = kDefaultSleepNearInterval;
    int sleep_mid_interval_ = kDefaultSleepMidInterval;
    int sleep_far_interval_ = kDefaultSleepFarInterval;

    // External chunk state integration. PlanetShellChunkRendererBridge marks
    // chunks ACTIVE/SLEEPING based on shell visibility and SurfaceColumnIndex.
    bool respect_external_chunk_state_ = true;
    int64_t external_active_chunk_count_ = 0;
    int64_t external_sleeping_chunk_count_ = 0;
    int64_t skipped_external_sleeping_chunk_count_ = 0;

    // Parallel execution control.
    bool parallel_enabled_ = false;
    int max_worker_threads_ = 0;  // 0 = auto

    int64_t tick_counter_ = 0;
};

} // namespace science_and_theology
