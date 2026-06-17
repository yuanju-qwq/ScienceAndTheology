#pragma once

#include <memory>
#include <vector>

#include "simulation_system.hpp"
#include "event_bus.hpp"
#include "error_handler.hpp"
#include "state_sync_server.hpp"
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
//      b. SLEEPING chunks (every N ticks): tick_sleeping()
//   3. EventBus::process_queue() — drain deferred events.
//   4. StateSyncServer::set_tick_counter() — advance tick.
class TickSystem {
public:
    static constexpr int kTicksPerSecond = 20;
    static constexpr int kSleepTickInterval = 20; // sleep chunks tick every 1s

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

    // Update the player's current chunk position (drives ACTIVE/SLEEPING set).
    void set_player_chunk(const std::string& dimension, int cx, int cy, int cz);

    // How many chunks around the player are ACTIVE (radius, Manhattan).
    void set_active_radius(int radius) { active_radius_ = radius; }
    int active_radius() const { return active_radius_; }

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

private:
    // Rebuild the active/sleeping chunk sets.
    void rebuild_chunk_sets();

    // Returns true if a chunk should tick this frame (sleeping cadence).
    bool should_tick_sleeping(int cx, int cy, int cz) const;

    WorldData* world_data_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<ErrorHandler> error_handler_;
    std::unique_ptr<StateSyncServer> state_sync_;

    std::vector<std::unique_ptr<SimulationSystem>> subsystems_;

    std::string player_dimension_ = "overworld";
    int player_cx_ = 0;
    int player_cy_ = 0;
    int player_cz_ = 0;
    int active_radius_ = 4;

    std::vector<ChunkKey> active_chunks_;
    std::vector<ChunkKey> sleeping_chunks_;
    int64_t tick_counter_ = 0;
};

} // namespace science_and_theology
