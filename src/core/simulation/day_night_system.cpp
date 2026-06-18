#include "day_night_system.hpp"

#include "../world/world_data.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void DayNightSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void DayNightSystem::tick_active(const ChunkKey& chunk, float delta,
                                 const TickContext* ctx) {
    (void)delta;
    if (!world_) return;

    // Track the player's current dimension for per-planet config.
    player_dimension_ = chunk.dimension_id;

    // Read the current simulation tick (set by TickSystem).
    const int64_t tick = world_->current_tick();

    const GameplayConfig& gc = world_->gameplay_config();

    // Check if day/night cycle is enabled for this dimension.
    if (!gc.is_day_night_enabled(player_dimension_)) {
        // Permanently noon.
        state_ = compute_day_night_state(0.5f, gc.twilight_fraction);
        return;
    }

    // Derive day length in ticks from day_length_seconds and TPS.
    constexpr float kTicksPerSecond = 20.0f;
    const float day_len_sec = gc.get_day_length_seconds(player_dimension_);
    const int64_t day_length_ticks = static_cast<int64_t>(
        day_len_sec * kTicksPerSecond);

    // Compute time of day.
    const float tod = compute_time_of_day(tick, day_length_ticks);

    // Get twilight fraction for this dimension.
    const float twilight = gc.get_twilight_fraction(player_dimension_);

    // Compute the full day/night state.
    state_ = compute_day_night_state(tod, twilight);
}

void DayNightSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                  const TickContext* ctx) {
    (void)chunk;
    (void)delta;
    // Day/night is global per-dimension; no per-chunk work in sleeping chunks.
}

void DayNightSystem::shutdown() {
    // No persistent state.
}

} // namespace science_and_theology
