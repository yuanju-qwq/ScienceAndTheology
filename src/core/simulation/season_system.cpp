#include "season_system.hpp"

#include "../world/world_data.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void SeasonSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void SeasonSystem::tick_active(const ChunkKey& chunk, float delta) {
    (void)chunk;
    (void)delta;
    if (!world_) return;

    // Read the current tick from WorldData (set by TickSystem each frame).
    const int64_t tick = world_->current_tick();

    const GameplayConfig& gc = world_->gameplay_config();
    const int days_per_season = gc.days_per_season;

    // Derive ticks_per_day from day_length_seconds and TPS.
    constexpr float kTicksPerSecond = 20.0f;
    const int64_t ticks_per_day = static_cast<int64_t>(
        gc.day_length_seconds * kTicksPerSecond);

    current_season_ = season_from_tick(tick, days_per_season, ticks_per_day);
    current_day_in_season_ = day_in_season(tick, days_per_season, ticks_per_day);
    current_game_day_ = total_game_day(tick, ticks_per_day);
}

void SeasonSystem::tick_sleeping(const ChunkKey& chunk, float delta) {
    (void)chunk;
    (void)delta;
    // Season is global; no per-chunk work needed in sleeping chunks.
}

void SeasonSystem::shutdown() {
    // No persistent state.
}

// --- Query ---

SeasonColorMod SeasonSystem::deciduous_color_mod() const {
    const int idx = static_cast<int>(current_season_);
    if (idx < 0 || idx >= static_cast<int>(Season::COUNT)) {
        return {1.0f, 1.0f, 1.0f};
    }
    return kDeciduousSeasonMods[idx];
}

SeasonColorMod SeasonSystem::evergreen_color_mod() const {
    const int idx = static_cast<int>(current_season_);
    if (idx < 0 || idx >= static_cast<int>(Season::COUNT)) {
        return {1.0f, 1.0f, 1.0f};
    }
    return kEvergreenSeasonMods[idx];
}

} // namespace science_and_theology
