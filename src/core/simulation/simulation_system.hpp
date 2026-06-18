#pragma once

#include <memory>
#include "event_bus.hpp"
#include "../world/chunk_data.hpp"

namespace science_and_theology {

class WorldData;

// Abstract base class for subsystems managed by TickSystem.
// Each subsystem handles a specific domain of simulation (machines, power,
// fluids, logistics, etc.) across all chunks.
//
// Subsystems are ordered by priority (lower runs first) to respect
// dependencies, e.g. power distribution before machine tick.
class SimulationSystem {
public:
    virtual ~SimulationSystem() = default;

    // Called once when the subsystem is registered with TickSystem.
    virtual void initialize(WorldData* world, EventBus* bus) = 0;

    // Called once per tick for a chunk in ACTIVE state.
    // Full simulation: every machine, full power resolution, etc.
    virtual void tick_active(const ChunkKey& chunk, float delta) = 0;

    // Called once per N ticks for a chunk in SLEEPING state.
    // Low-frequency or approximate simulation.
    virtual void tick_sleeping(const ChunkKey& chunk, float delta) = 0;

    // Called when the subsystem is being shut down.
    virtual void shutdown() = 0;

    // Human-readable name for debugging.
    virtual const char* name() const = 0;

    // Execution priority within the tick pipeline.
    // 0 = first, higher = later. Default buckets:
    //   0 - DayNight
    //   1 - BlockPhysics
    //   2 - Machine
    //   3 - (reserved: Fluid)
    //   4 - (reserved: Logistics)
    //   5 - Region
    //   6 - Season
    //   7 - TreeGrowth
    virtual int priority() const = 0;

    // Thread safety declaration.
    // Returns true if this subsystem supports concurrent calls to
    // tick_active() / tick_sleeping() from multiple threads.
    //
    // If true, TickSystem may:
    //   a. Run this subsystem in parallel with other subsystems at the
    //      same priority level (priority-group parallelism).
    //   b. Run tick_active() for multiple chunks in parallel (chunk-level
    //      parallelism).
    //
    // If false, TickSystem will only call this subsystem from the main
    // thread, sequentially.
    //
    // Default: false (safe for all existing subsystems).
    virtual bool is_thread_safe() const { return false; }

protected:
    WorldData* world_ = nullptr;
    EventBus* event_bus_ = nullptr;
};

// Macro to simplify declaring the name() override.
#define SIMULATION_SYSTEM_NAME(cls, str) \
    const char* name() const override { return str; }

} // namespace science_and_theology
