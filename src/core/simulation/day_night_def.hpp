#pragma once

#include <cmath>

namespace science_and_theology {

// ============================================================
// DayNight — data structures and utilities for the day/night cycle
// ============================================================

// Cached state of the day/night cycle, updated once per tick by DayNightSystem.
// GDScript reads this to rotate the sun/moon and adjust lighting.
struct DayNightState {
    // Time of day in [0, 1). 0.0 = midnight, 0.25 = sunrise,
    // 0.5 = noon, 0.75 = sunset.
    float time_of_day = 0.5f;

    // Sun elevation in radians. Positive = above horizon, negative = below.
    float sun_elevation = 1.5708f;  // pi/2 at noon

    // Sun azimuth in radians (0 = north, pi/2 = east, etc.).
    float sun_azimuth = 0.0f;

    // Sun directional light energy (0 at night, peaks at noon).
    float sun_light_energy = 1.0f;

    // Sun directional light color (warm at sunrise/sunset, white at noon).
    float sun_color_r = 1.0f;
    float sun_color_g = 1.0f;
    float sun_color_b = 1.0f;

    // Ambient light energy (low at night, moderate during day).
    float ambient_energy = 0.5f;

    // Ambient light color (cool blue at night, warm during day).
    float ambient_color_r = 0.5f;
    float ambient_color_g = 0.5f;
    float ambient_color_b = 0.6f;

    // Whether the sun is above the horizon (useful for solar panels, etc.).
    bool is_daytime = true;

    // Moon elevation in radians (opposite phase from sun).
    float moon_elevation = -1.5708f;

    // Moon azimuth in radians.
    float moon_azimuth = 3.14159f;

    // Moon directional light energy (0 when below horizon).
    float moon_light_energy = 0.0f;

    // Moon light color (cool blue-white).
    float moon_color_r = 0.6f;
    float moon_color_g = 0.65f;
    float moon_color_b = 0.8f;
};

// Compute time_of_day from tick counter and day length. start_time specifies
// the phase at tick 0; new worlds default to noon rather than midnight.
inline float compute_time_of_day(int64_t tick, int64_t day_length_ticks,
                                 float start_time = 0.5f) {
    if (day_length_ticks <= 0) day_length_ticks = 12000;
    int64_t wrapped = tick % day_length_ticks;
    if (wrapped < 0) wrapped += day_length_ticks;
    float time = static_cast<float>(wrapped)
        / static_cast<float>(day_length_ticks) + start_time;
    time -= std::floor(time);
    if (time < 0.0f) time += 1.0f;
    return time;
}

// Compute sun elevation from time_of_day.
// 0.5 (noon) = +pi/2, 0.0/1.0 (midnight) = -pi/2.
inline float compute_sun_elevation(float time_of_day) {
    constexpr float kPi = 3.14159265f;
    return -cosf(time_of_day * 2.0f * kPi) * (kPi * 0.5f);
}

// Compute moon elevation (opposite phase from sun).
inline float compute_moon_elevation(float time_of_day) {
    return -compute_sun_elevation(time_of_day);
}

// Compute sun azimuth (simple east-to-west arc).
// At sunrise (0.25) azimuth = pi/2 (east), at sunset (0.75) = 3*pi/2 (west).
inline float compute_sun_azimuth(float time_of_day) {
    return time_of_day * 2.0f * 3.14159265f;
}

// Compute moon azimuth (opposite the sun).
inline float compute_moon_azimuth(float time_of_day) {
    float az = compute_sun_azimuth(time_of_day) + 3.14159265f;
    if (az >= 2.0f * 3.14159265f) az -= 2.0f * 3.14159265f;
    return az;
}

// Smooth interpolation helper for twilight transitions.
// Returns 0.0 when elevation <= -threshold, 1.0 when elevation >= threshold,
// smooth cosine interpolation in between.
inline float smooth_horizon_factor(float elevation, float threshold) {
    if (elevation >= threshold) return 1.0f;
    if (elevation <= -threshold) return 0.0f;
    float t = (elevation + threshold) / (2.0f * threshold);
    return 0.5f - 0.5f * cosf(t * 3.14159265f);
}

// Compute the full DayNightState from time_of_day and twilight_fraction.
// twilight_fraction controls how wide the sunrise/sunset transition is.
inline DayNightState compute_day_night_state(float time_of_day,
                                             float twilight_fraction) {
    DayNightState state;
    state.time_of_day = time_of_day;

    // Sun position.
    state.sun_elevation = compute_sun_elevation(time_of_day);
    state.sun_azimuth = compute_sun_azimuth(time_of_day);

    // Moon position (opposite phase).
    state.moon_elevation = compute_moon_elevation(time_of_day);
    state.moon_azimuth = compute_moon_azimuth(time_of_day);

    // Twilight threshold in radians derived from twilight_fraction.
    // twilight_fraction = 0.1 means 10% of the day is transition.
    // At noon the sun is at pi/2, so the transition zone in elevation
    // is approximately twilight_fraction * pi.
    float twilight_rad = twilight_fraction * 3.14159265f;
    if (twilight_rad < 0.01f) twilight_rad = 0.01f;
    if (twilight_rad > 1.5f) twilight_rad = 1.5f;

    // Daytime factor: 0 at night, 1 at full day, smooth in between.
    float day_factor = smooth_horizon_factor(state.sun_elevation, twilight_rad);
    state.is_daytime = state.sun_elevation > 0.0f;

    // Sun light energy: 0 at night, peaks at noon.
    // At noon (elevation = pi/2), day_factor = 1.0.
    // Scale by a sine curve for more realistic intensity.
    float noon_factor = 1.0f;
    if (state.sun_elevation > 0.0f) {
        noon_factor = sinf(state.sun_elevation);
    }
    state.sun_light_energy = day_factor * noon_factor * 2.2f;

    // Sun color: warm orange at horizon, white at noon.
    // At low elevation, shift toward warm (increase R, decrease B).
    float horizon_warmth = 1.0f - day_factor;
    state.sun_color_r = 1.0f;
    state.sun_color_g = 1.0f - horizon_warmth * 0.2f;
    state.sun_color_b = 1.0f - horizon_warmth * 0.5f;

    // Ambient light: cool blue at night, warm during day.
    state.ambient_energy = 0.15f + day_factor * 0.47f;
    state.ambient_color_r = 0.1f + day_factor * 0.4f;
    state.ambient_color_g = 0.1f + day_factor * 0.4f;
    state.ambient_color_b = 0.2f + day_factor * 0.3f;

    // Moon light: only visible when moon is above horizon.
    float moon_factor = smooth_horizon_factor(state.moon_elevation, twilight_rad);
    state.moon_light_energy = moon_factor * 0.15f;

    return state;
}

} // namespace science_and_theology
