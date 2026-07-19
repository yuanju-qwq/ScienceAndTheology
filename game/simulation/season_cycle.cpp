// Deterministic game-owned seasonal state implementation.

#include "game/simulation/season_cycle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace snt::game {

namespace {

constexpr float kFallbackFixedDeltaSeconds = 0.05f;
constexpr float kFallbackDayLengthSeconds = 600.0f;
constexpr int kFallbackDaysPerSeason = 16;

float sanitize_positive(float value, float fallback) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
}

uint64_t ticks_per_game_day(float fixed_delta_seconds, float day_length_seconds) noexcept {
    const double fixed_delta = static_cast<double>(sanitize_positive(
        fixed_delta_seconds, kFallbackFixedDeltaSeconds));
    const double day_length = static_cast<double>(sanitize_positive(
        day_length_seconds, kFallbackDayLengthSeconds));
    const double requested_ticks = day_length / fixed_delta;
    if (!std::isfinite(requested_ticks) || requested_ticks <= 1.0) return 1;

    // The runtime can only change seasons on a fixed-tick boundary. Rounding
    // prevents float representation of 0.05 seconds from shortening the
    // default 600-second day to 11,999 ticks.
    const double rounded_ticks = std::round(requested_ticks);
    const double max_ticks = static_cast<double>((std::numeric_limits<uint64_t>::max)());
    if (rounded_ticks >= max_ticks) return (std::numeric_limits<uint64_t>::max)();
    return std::max<uint64_t>(1, static_cast<uint64_t>(rounded_ticks));
}

int sanitize_days_per_season(int value) noexcept {
    return value > 0 ? value : kFallbackDaysPerSeason;
}

SeasonColorMod color_mod_for(Season season,
                             const SeasonColorMod* modifiers,
                             bool season_colors_enabled) noexcept {
    const int index = static_cast<int>(season);
    if (!season_colors_enabled || index < 0 || index >= static_cast<int>(Season::COUNT)) {
        return {};
    }
    return modifiers[index];
}

}  // namespace

SeasonState compute_season_state(uint64_t tick_index,
                                 float fixed_delta_seconds,
                                 float day_length_seconds,
                                 int days_per_season,
                                 bool season_colors_enabled) noexcept {
    SeasonState state;
    state.source_tick = tick_index;

    const uint64_t ticks_per_day = ticks_per_game_day(
        fixed_delta_seconds, day_length_seconds);
    const uint64_t resolved_days_per_season = static_cast<uint64_t>(
        sanitize_days_per_season(days_per_season));
    state.game_day = tick_index / ticks_per_day;
    state.day_in_season = static_cast<int>(state.game_day % resolved_days_per_season);
    state.season = static_cast<Season>(
        (state.game_day / resolved_days_per_season) % static_cast<uint64_t>(Season::COUNT));
    state.deciduous_color_mod = color_mod_for(
        state.season, kDeciduousSeasonMods, season_colors_enabled);
    state.evergreen_color_mod = color_mod_for(
        state.season, kEvergreenSeasonMods, season_colors_enabled);
    return state;
}

void SeasonCycle::update(uint64_t tick_index,
                         float fixed_delta_seconds,
                         const GameplayConfig& config,
                         const std::string& dimension_id) noexcept {
    state_ = compute_season_state(tick_index,
                                  fixed_delta_seconds,
                                  config.get_day_length_seconds(dimension_id),
                                  config.days_per_season,
                                  config.enable_season_colors);
}

}  // namespace snt::game
