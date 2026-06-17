#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/array.hpp>

#include <vector>

#include "core/simulation/tick_system.hpp"

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

    // Advance simulation by one frame.
    void tick(float delta);

    // Set the player's current chunk position to determine ACTIVE set.
    void set_player_chunk(const godot::String& dimension, int cx, int cy, int cz);

    // Active chunk radius.
    int64_t get_active_radius() const;
    void set_active_radius(int64_t radius);

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

    std::vector<EventBus::HandlerId> event_subscriptions_;

    godot::Resource* player_inventory_ = nullptr;
    godot::Resource* player_equipment_ = nullptr;
};

} // namespace science_and_theology
