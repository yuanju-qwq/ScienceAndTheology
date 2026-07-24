#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

#include "core/simulation/tick_system.hpp"
#include "core/simulation/day_night_system.hpp"

namespace science_and_theology {

// GDExtension wrapper for the unified simulation TickSystem.
// Owns and drives the entire C++ simulation pipeline.
//
// Usage in GDScript:
//   var tick_sys = GDTickSystem.new()
//   tick_sys.set_world_data(gd_world_data)
//   tick_sys.add_player_chunk(1, "overworld", 0, 0, 0)
//
//   func _process(delta):
//       tick_sys.tick(delta)
class GDTickSystem : public godot::Node {
    GDCLASS(GDTickSystem, godot::Node)

public:
    GDTickSystem();
    ~GDTickSystem() override;

    void _ready() override;
    void _process(double delta) override;

    // Attach a GDWorldData (which owns the C++ WorldData).
    void set_world_data(godot::Resource* gd_world);

    // Register the season simulation subsystem.
    // Must be called after set_world_data(). Computes current season
    // from tick counter and exposes it to other systems.
    void register_season_system();

    // Register the day/night cycle simulation subsystem.
    // Must be called after set_world_data(). Computes time-of-day,
    // sun/moon position, and lighting parameters. Should be registered
    // first (before other subsystems) since it runs at priority 0.
    void register_day_night_system();

    // --- Day/Night query ---

    // Returns the current day/night state as a Dictionary.
    godot::Dictionary get_day_night_state() const;

    // Convenience: returns current time of day [0, 1).
    float get_time_of_day() const;

    // Convenience: returns true if the sun is above the horizon.
    bool get_is_daytime() const;

    // Advance simulation by one frame.
    void tick(float delta);

    // --- Multi-player active set API ---
    // Register/update a player's chunk position for active set computation.
    // Single-player mode uses player_handle = 1 (kSinglePlayerHandle).
    void add_player_chunk(int64_t player_handle,
                          const godot::String& dimension,
                          int cx, int cy, int cz);

    // Remove a player from the active set computation.
    void remove_player_chunk(int64_t player_handle);

    // Remove all players from the active set computation.
    void clear_player_chunks();

    // Returns the number of registered players driving the active set.
    int64_t get_player_count() const;

    // Returns the dimension the player is currently in, or empty string
    // if the player is not registered. Used by the network layer to
    // filter deltas by player dimension (M5: multi-planet concurrent).
    godot::String get_player_dimension(int64_t player_handle) const;

    // Active chunk radius.
    int64_t get_active_radius() const;
    void set_active_radius(int64_t radius);

    // --- Sleep interval configuration ---

    // Sleep interval for NEAR tier (in ticks).
    int64_t get_sleep_near_interval() const;
    void set_sleep_near_interval(int64_t interval);

    // Sleep interval for MID tier (in ticks).
    int64_t get_sleep_mid_interval() const;
    void set_sleep_mid_interval(int64_t interval);

    // Sleep interval for FAR tier (in ticks).
    int64_t get_sleep_far_interval() const;
    void set_sleep_far_interval(int64_t interval);

    // --- Parallel execution control ---

    // Enable or disable parallel subsystem execution.
    void set_parallel_enabled(bool enabled);
    bool get_parallel_enabled() const;

    // Maximum number of worker threads for chunk-level parallelism.
    // 0 = auto (hardware_concurrency - 1).
    void set_max_worker_threads(int64_t count);
    int64_t get_max_worker_threads() const;

    // Current tick count.
    int64_t get_tick_count() const;

    // --- Tick profiler command API ---

    void set_perf_profiler_enabled(bool enabled);
    bool get_perf_profiler_enabled() const;
    void set_perf_profiler_tick_budget_ms(double budget_ms);
    double get_perf_profiler_tick_budget_ms() const;
    void set_perf_profiler_slow_scope_ms(double threshold_ms);
    double get_perf_profiler_slow_scope_ms() const;
    void set_perf_profiler_log_interval_ticks(int64_t ticks);
    int64_t get_perf_profiler_log_interval_ticks() const;
    godot::String get_perf_profiler_summary(int64_t top_n) const;
    godot::Array get_perf_profiler_top(int64_t top_n) const;
    void clear_perf_profiler();

    // Returns the number of currently ACTIVE chunks.
    int64_t get_active_chunk_count() const;

    // Returns active chunk keys as an Array of Dictionaries.
    godot::Array get_active_chunks() const;

    // --- State Sync ---

    // Returns dirty chunk keys as an Array of Dictionaries.
    // Call each frame; GDScript rendering layer uses this to update proxies.
    godot::Array get_dirty_chunks() const;

    // Compute delta for a specific observer (player_handle) over the given
    // chunk list. Returns dict:
    //   { "flags": int, "timestamp": int,
    //     "chunks_modified": Array, "entities_created": Array, ... }
    // Single-player mode uses player_handle = 1 (kSinglePlayerHandle).
    godot::Dictionary compute_delta_for(int64_t player_handle,
                                        const godot::Array& chunk_keys);

    // M5: Compute deltas for multiple observers in batch. Dirty flags
    // are only cleared after ALL observers have been processed, so
    // multiple observers in the same dimension all see the same dirty
    // state.
    // observer_views: Array of Dictionaries, each with:
    //   { "player_handle": int, "chunks": Array of chunk Dictionaries }
    // Returns: Array of Dictionaries, each with:
    //   { "player_handle": int, "delta": Dictionary }
    godot::Array compute_deltas_batch(const godot::Array& observer_views);

    // Create a full snapshot for a chunk.
    godot::Dictionary create_snapshot(
        const godot::String& dimension, int cx, int cy, int cz);

protected:
    static void _bind_methods();

private:
    godot::Dictionary event_to_dict(const GameEvent& ev) const;
    godot::Dictionary chunk_key_to_dict(const ChunkKey& key) const;
    godot::Dictionary delta_to_dict(const StateDelta& delta) const;

    // Helper to extract WorldData from the GDWorldData resource.
    WorldData* get_world_data_ptr() const;

    void subscribe_to_event_bus();
    void unsubscribe_from_event_bus();

    std::unique_ptr<TickSystem> tick_system_;
    godot::Resource* gd_world_data_ = nullptr;
    bool world_set = false;

    // Raw pointer to the DayNightSystem (owned by tick_system_).
    DayNightSystem* day_night_system_ = nullptr;

    std::vector<EventBus::HandlerId> event_subscriptions_;
};

} // namespace science_and_theology
