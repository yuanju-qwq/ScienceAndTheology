// Client-side adapter from deterministic day/night state to renderer values.

#include "game/client/day_night_lighting.h"

#include <algorithm>
#include <cmath>

namespace snt::game {

namespace {

std::array<float, 3> world_up_direction_to_light(float elevation_radians,
                                                  float azimuth_radians) noexcept {
    const float horizontal = std::cos(elevation_radians);
    return {
        std::sin(azimuth_radians) * horizontal,
        std::sin(elevation_radians),
        -std::cos(azimuth_radians) * horizontal,
    };
}

}  // namespace

snt::render::EnvironmentLighting make_environment_lighting(const DayNightState& state) noexcept {
    snt::render::EnvironmentLighting lighting;
    lighting.sun.direction_to_light = world_up_direction_to_light(
        state.sun_elevation_radians, state.sun_azimuth_radians);
    lighting.sun.color = state.sun_color;
    lighting.sun.intensity = state.sun_intensity;
    lighting.moon.direction_to_light = world_up_direction_to_light(
        state.moon_elevation_radians, state.moon_azimuth_radians);
    lighting.moon.color = state.moon_color;
    lighting.moon.intensity = state.moon_intensity;
    lighting.ambient_color = state.ambient_color;
    lighting.ambient_intensity = state.ambient_intensity;

    // The current renderer clears to this value. It intentionally remains a
    // compact sky approximation until the dedicated sky and atmosphere pass
    // replaces it; this adapter does not emulate Godot's fog or sky shader.
    const float daylight = std::clamp((state.ambient_intensity - 0.15f) / 0.47f,
                                      0.0f, 1.0f);
    lighting.sky_color = {
        0.01f + daylight * 0.24f,
        0.02f + daylight * 0.42f,
        0.06f + daylight * 0.74f,
        1.0f,
    };
    return lighting;
}

}  // namespace snt::game
