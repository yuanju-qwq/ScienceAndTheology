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

    const int64_t tick = world_->gameplay_config().days_per_season;  // just trigger
    (void)tick;

    // Use the TickSystem's tick counter to compute the season.
    // We read the current tick from the world's state sync or
    // compute from our own counter. Since we don't have direct
    // access to TickSystem's counter here, we use an internal counter.
    // The TickSystem drives all subsystems, so our tick count
    // stays in sync with the main simulation.
    static int64_t internal_tick = 0;
    ++internal_tick;

    const GameplayConfig& gc = world_->gameplay_config();
    const int days_per_season = gc.days_per_season;

    current_season_ = season_from_tick(internal_tick, days_per_season);
    current_day_in_season_ = day_in_season(internal_tick, days_per_season);
    current_game_day_ = total_game_day(internal_tick);
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
