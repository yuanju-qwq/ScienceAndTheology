#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>

#include <vector>

#include "core/simulation/tick_system.hpp"
#include "core/simulation/day_night_system.hpp"
#include "core/simulation/region_system.hpp"

namespace science_and_theology {

// GDExtension wrapper for the unified simulation TickSystem.
// Owns and drives the entire C++ simulation pipeline.
//
// Usage in GDScript:
//   var tick_sys = GDTickSystem.new()
//   tick_sys.set_world_data(gd_world_data)
//   tick_sys.register_machine_system()
//   tick_sys.set_player_chunk("overworld", 0, 0, 0)
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

    // Register the machine simulation subsystem.
    void register_machine_system();

    // Register the block physics simulation subsystem.
    // Must be called after set_world_data(). Handles gravity fall
    // and cave-in collapse for blocks after mining.
    void register_block_physics_system();

    // Register the tree growth simulation subsystem.
    // Must be called after set_world_data(). Handles tree growth
    // from sapling → young → mature based on tick timing and conditions.
    void register_tree_growth_system();

    // Register the season simulation subsystem.
    // Must be called after set_world_data(). Computes current season
    // from tick counter and exposes it to other systems.
    void register_season_system();

    // Register the day/night cycle simulation subsystem.
    // Must be called after set_world_data(). Computes time-of-day,
    // sun/moon position, and lighting parameters. Should be registered
    // first (before other subsystems) since it runs at priority 0.
    void register_day_night_system();

    // Register the region simulation subsystem.
    // Must be called after set_world_data(). Manages RegionGraphs
    // for power grids, fluid networks, pollution, and temperature.
    // Runs at priority 5 (after Machine, before Season).
    void register_region_system();

    // --- Day/Night query ---

    // Returns the current day/night state as a Dictionary.
    godot::Dictionary get_day_night_state() const;

    // Convenience: returns current time of day [0, 1).
    float get_time_of_day() const;

    // Convenience: returns true if the sun is above the horizon.
    bool get_is_daytime() const;

    // --- Region query ---

    // Returns the total number of regions across all types.
    int64_t get_region_count() const;

    // Returns the number of regions for a specific type.
    // type_index: 0=PowerGrid, 1=Fluid, 2=Connected, 3=Pollution, 4=Temperature.
    int64_t get_region_count_by_type(int64_t type_index) const;

    // Returns region data as a Dictionary for a given region type and ID.
    // type_index: 0=PowerGrid, 1=Fluid, 2=Connected, 3=Pollution, 4=Temperature.
    godot::Dictionary get_region_data(int64_t type_index, int64_t region_id) const;

    // Advance simulation by one frame.
    void tick(float delta);

    // Set the player's current chunk position to determine ACTIVE set.
    void set_player_chunk(const godot::String& dimension, int cx, int cy, int cz);

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

    // Returns the number of currently ACTIVE chunks.
    int64_t get_active_chunk_count() const;

    // Returns active chunk keys as an Array of Dictionaries.
    godot::Array get_active_chunks() const;

    // --- Events ---

    // Deprecated: use signals instead (machine_error, chunk_generated, etc.).
    // Returns an empty array.
    godot::Array poll_events();

    // --- Errors ---

    // Returns active machine errors as an Array of Dictionaries.
    godot::Array get_machine_errors() const;

    // Clear a machine error.
    void clear_machine_error(int64_t machine_id);

    // --- State Sync ---

    // Returns dirty chunk keys as an Array of Dictionaries.
    // Call each frame; GDScript rendering layer uses this to update proxies.
    godot::Array get_dirty_chunks() const;

    // Compute delta for the given chunk list, returns dict:
    //   { "flags": int, "timestamp": int,
    //     "chunks_modified": Array, "entities_created": Array, ... }
    godot::Dictionary compute_delta(const godot::Array& chunk_keys);

    // Create a full snapshot for a chunk.
    godot::Dictionary create_snapshot(
        const godot::String& dimension, int cx, int cy, int cz);

    // Set the GDPlayerInventory reference for event bridging.
    void set_player_inventory(godot::Resource* inventory);
    void set_player_equipment(godot::Resource* equipment);

protected:
    static void _bind_methods();

private:
    godot::Dictionary event_to_dict(const GameEvent& ev) const;
    godot::Dictionary error_to_dict(const MachineError& err) const;
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

    // Raw pointer to the RegionSystem (owned by tick_system_).
    RegionSystem* region_system_ = nullptr;

    std::vector<EventBus::HandlerId> event_subscriptions_;

    godot::Resource* player_inventory_ = nullptr;
    godot::Resource* player_equipment_ = nullptr;
};

} // namespace science_and_theology
