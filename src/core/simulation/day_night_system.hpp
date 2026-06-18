#pragma once

#include <string>

#include "simulation_system.hpp"
#include "day_night_def.hpp"

namespace science_and_theology {

// Day/night cycle simulation subsystem.
// Computes the current time-of-day, sun/moon position, and lighting
// parameters from the game tick counter and GameplayConfig.
//
// Design:
//   - Runs at priority 0 (first subsystem) so that all other systems
//     (Power, Machine, etc.) can query is_daytime() in the same tick.
//   - tick_active() is lightweight: reads WorldData::current_tick(),
//     computes the DayNightState, and caches it.
//   - Other systems query the cached state via current_state().
//   - The GDScript rendering layer reads the state each frame to
//     rotate the DirectionalLight3D nodes and adjust the Environment.
//   - Per-planet day_length_seconds is supported via GameplayConfig
//     planet_overrides. The system uses the player's current dimension
//     to resolve the effective day length.
//
// Thread safety: main thread only. Not thread-safe.

class DayNightSystem : public SimulationSystem {
public:
    DayNightSystem() = default;

    SIMULATION_SYSTEM_NAME(DayNightSystem, "DayNightSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    // First subsystem to run so others can read is_daytime().
    int priority() const override { return 0; }

    // --- Query ---

    // Returns the current day/night state (cached each tick).
    const DayNightState& current_state() const { return state_; }

    // Convenience: is the sun above the horizon?
    bool is_daytime() const { return state_.is_daytime; }

    // Convenience: current time of day [0, 1).
    float time_of_day() const { return state_.time_of_day; }

private:
    DayNightState state_;
    std::string player_dimension_;
};

} // namespace science_and_theology
