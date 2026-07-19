// Deterministic game-owned seasonal state.
//
// This module derives one immutable seasonal snapshot from the authoritative
// fixed tick. Gameplay and future presentation adapters consume this state;
// neither needs a wall clock, legacy WorldData, or a renderer dependency.

#pragma once

#include "game/world/defs/gameplay_config.h"

#include <cstdint>
#include <string>

namespace snt::game {

struct SeasonState {
    // The deterministic fixed tick that produced this snapshot. This keeps
    // future replication and cached presentation values tied to simulation.
    uint64_t source_tick = 0;

    Season season = Season::SPRING;
    int day_in_season = 0;
    uint64_t game_day = 0;

    // Resolved once with the current color policy so tree presentation does
    // not need to reach back into mutable GameplayConfig during a frame.
    SeasonColorMod deciduous_color_mod{};
    SeasonColorMod evergreen_color_mod{};
};

// Derives a season snapshot from resolved runtime values. Invalid durations
// and season counts use the same ten-minute/16-day defaults as GameplayConfig.
[[nodiscard]] SeasonState compute_season_state(uint64_t tick_index,
                                                float fixed_delta_seconds,
                                                float day_length_seconds,
                                                int days_per_season,
                                                bool season_colors_enabled) noexcept;

class SeasonCycle {
public:
    // Updates one dimension's value snapshot from the session tick. The
    // dimension determines the resolved day duration; the seasonal cadence
    // and color policy remain game-wide until GameplayConfig adds overrides.
    void update(uint64_t tick_index,
                float fixed_delta_seconds,
                const GameplayConfig& config,
                const std::string& dimension_id) noexcept;

    [[nodiscard]] const SeasonState& state() const noexcept { return state_; }

private:
    SeasonState state_{};
};

}  // namespace snt::game
