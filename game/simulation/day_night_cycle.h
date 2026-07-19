// Deterministic game-owned day/night state.
//
// This module owns the celestial phase and light values consumed by gameplay
// and presentation adapters. It does not depend on rendering, SDL, Vulkan, or
// network transport, so the server and graphical client evaluate the same
// state from an authoritative tick clock.

#pragma once

#include "game/world/defs/gameplay_config.h"

#include <array>
#include <cstdint>
#include <string>

namespace snt::game {

struct DayNightState {
    // The deterministic simulation tick that produced this value. Future
    // replication can use it to distinguish authoritative state from a
    // locally extrapolated presentation value.
    uint64_t source_tick = 0;

    // [0, 1): midnight, sunrise, noon, sunset are 0.0, 0.25, 0.5, 0.75.
    float time_of_day = 0.5f;

    // World-up celestial angles in radians. Presentation code may map these
    // to a local planetary frame without changing gameplay time semantics.
    float sun_elevation_radians = 1.5707963f;
    float sun_azimuth_radians = 0.0f;
    float moon_elevation_radians = -1.5707963f;
    float moon_azimuth_radians = 3.1415927f;

    std::array<float, 3> sun_color = {1.0f, 1.0f, 1.0f};
    float sun_intensity = 2.2f;
    std::array<float, 3> moon_color = {0.6f, 0.65f, 0.8f};
    float moon_intensity = 0.0f;
    std::array<float, 3> ambient_color = {0.5f, 0.5f, 0.5f};
    float ambient_intensity = 0.62f;
    bool is_daytime = true;
};

// Resolves the normalized phase from the deterministic runtime clock. Invalid
// configuration values fall back to the default ten-minute day without
// emitting per-tick diagnostics.
[[nodiscard]] float compute_day_night_time_of_day(uint64_t tick_index,
                                                   float fixed_delta_seconds,
                                                   float day_length_seconds,
                                                   float day_start_time) noexcept;

// Resolves colors and intensities for a normalized phase. This is intentionally
// presentation-neutral: it describes emitters and ambient light, not Godot
// nodes, a Vulkan descriptor layout, fog, or a sky material.
[[nodiscard]] DayNightState compute_day_night_state(float time_of_day,
                                                     float twilight_fraction) noexcept;

class DayNightCycle {
public:
    // Updates the value snapshot for one active world dimension. The caller
    // supplies the session's authoritative fixed tick rather than allowing a
    // separate wall clock to drift between simulation and rendering.
    void update(uint64_t tick_index,
                float fixed_delta_seconds,
                const GameplayConfig& config,
                const std::string& dimension_id) noexcept;

    [[nodiscard]] const DayNightState& state() const noexcept { return state_; }

private:
    DayNightState state_{};
};

}  // namespace snt::game
