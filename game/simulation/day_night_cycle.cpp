// Deterministic game-owned day/night state implementation.

#include "game/simulation/day_night_cycle.h"

#include <algorithm>
#include <cmath>

namespace snt::game {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFallbackFixedDeltaSeconds = 0.05f;
constexpr float kFallbackDayLengthSeconds = 600.0f;

float normalize_phase(float phase, float fallback) noexcept {
    if (!std::isfinite(phase)) return fallback;
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

float sanitize_positive(float value, float fallback) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
}

float smooth_horizon_factor(float elevation, float threshold) noexcept {
    if (elevation >= threshold) return 1.0f;
    if (elevation <= -threshold) return 0.0f;
    const float interpolation = (elevation + threshold) / (2.0f * threshold);
    return 0.5f - 0.5f * std::cos(interpolation * kPi);
}

}  // namespace

float compute_day_night_time_of_day(uint64_t tick_index,
                                    float fixed_delta_seconds,
                                    float day_length_seconds,
                                    float day_start_time) noexcept {
    const double fixed_delta = static_cast<double>(sanitize_positive(
        fixed_delta_seconds, kFallbackFixedDeltaSeconds));
    const double day_length = static_cast<double>(sanitize_positive(
        day_length_seconds, kFallbackDayLengthSeconds));
    const double elapsed_fraction = std::fmod(
        static_cast<double>(tick_index) * fixed_delta / day_length, 1.0);
    return normalize_phase(static_cast<float>(elapsed_fraction) + day_start_time, 0.5f);
}

DayNightState compute_day_night_state(float time_of_day, float twilight_fraction) noexcept {
    DayNightState state;
    state.time_of_day = normalize_phase(time_of_day, 0.5f);

    state.sun_elevation_radians =
        -std::cos(state.time_of_day * 2.0f * kPi) * (kPi * 0.5f);
    state.sun_azimuth_radians = state.time_of_day * 2.0f * kPi;
    state.moon_elevation_radians = -state.sun_elevation_radians;
    state.moon_azimuth_radians = state.sun_azimuth_radians + kPi;
    if (state.moon_azimuth_radians >= 2.0f * kPi) {
        state.moon_azimuth_radians -= 2.0f * kPi;
    }

    const float sanitized_twilight = std::clamp(
        std::isfinite(twilight_fraction) ? twilight_fraction : 0.1f, 0.0f, 0.5f);
    const float twilight_radians = std::clamp(sanitized_twilight * kPi, 0.01f, 1.5f);
    const float day_factor = smooth_horizon_factor(
        state.sun_elevation_radians, twilight_radians);
    state.is_daytime = state.sun_elevation_radians > 0.0f;

    const float noon_factor = state.is_daytime
        ? std::sin(state.sun_elevation_radians)
        : 0.0f;
    state.sun_intensity = day_factor * noon_factor * 2.2f;

    const float horizon_warmth = 1.0f - day_factor;
    state.sun_color = {
        1.0f,
        1.0f - horizon_warmth * 0.2f,
        1.0f - horizon_warmth * 0.5f,
    };

    state.ambient_intensity = 0.15f + day_factor * 0.47f;
    state.ambient_color = {
        0.1f + day_factor * 0.4f,
        0.1f + day_factor * 0.4f,
        0.2f + day_factor * 0.3f,
    };
    const float moon_factor = smooth_horizon_factor(
        state.moon_elevation_radians, twilight_radians);
    state.moon_intensity = moon_factor * 0.15f;
    return state;
}

void DayNightCycle::update(uint64_t tick_index,
                           float fixed_delta_seconds,
                           const GameplayConfig& config,
                           const std::string& dimension_id) noexcept {
    const float twilight_fraction = config.get_twilight_fraction(dimension_id);
    if (!config.is_day_night_enabled(dimension_id)) {
        state_ = compute_day_night_state(0.5f, twilight_fraction);
    } else {
        const float time_of_day = compute_day_night_time_of_day(
            tick_index, fixed_delta_seconds, config.get_day_length_seconds(dimension_id),
            config.get_day_start_time(dimension_id));
        state_ = compute_day_night_state(time_of_day, twilight_fraction);
    }
    state_.source_tick = tick_index;
}

}  // namespace snt::game
