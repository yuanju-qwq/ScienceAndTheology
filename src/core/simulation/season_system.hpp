#pragma once

#include "simulation_system.hpp"
#include "season_def.hpp"

namespace science_and_theology {

// Season simulation subsystem.
// Computes the current season from the game tick counter and exposes
// it to other systems (tree growth, visual tinting, fruit drops).
//
// Design:
//   - Runs at priority 4 (before TreeGrowthSystem at 5) so that
//     the current season is up-to-date when trees check fruit drops.
//   - tick_active() is lightweight: just computes the current season
//     from the tick counter and caches the result.
//   - Other systems query the cached season via current_season().
//   - Season color tinting is exposed via get_season_color_mod() which
//     returns the appropriate color modifier for deciduous/evergreen trees.
//
// Thread safety: main thread only. Not thread-safe.

class SeasonSystem : public SimulationSystem {
public:
    SeasonSystem() = default;

    SIMULATION_SYSTEM_NAME(SeasonSystem, "SeasonSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta,
                     const TickContext* ctx = nullptr) override;
    void tick_sleeping(const ChunkKey& chunk, float delta,
                       const TickContext* ctx = nullptr) override;
    void shutdown() override;

    // Runs before tree growth (priority 7) so season is current.
    int priority() const override { return 6; }

    // --- Query ---

    // Returns the current season (cached each tick).
    Season current_season() const { return current_season_; }

    // Returns the day within the current season (0-based).
    int current_day_in_season() const { return current_day_in_season_; }

    // Returns the total game day (0-based).
    int64_t current_game_day() const { return current_game_day_; }

    // Returns the color modifier for deciduous trees in the current season.
    SeasonColorMod deciduous_color_mod() const;

    // Returns the color modifier for evergreen trees in the current season.
    SeasonColorMod evergreen_color_mod() const;

private:
    Season current_season_ = Season::SPRING;
    int current_day_in_season_ = 0;
    int64_t current_game_day_ = 0;
};

} // namespace science_and_theology
