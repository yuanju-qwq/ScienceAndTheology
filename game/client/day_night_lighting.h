// Client-side adapter from game day/night state to renderer lighting.
//
// The game simulation never depends on this header. It is deliberately kept
// at the presentation boundary so a headless server has no renderer linkage.

#pragma once

#include "game/simulation/day_night_cycle.h"
#include "render/render_components.h"

namespace snt::game {

[[nodiscard]] snt::render::EnvironmentLighting make_environment_lighting(
    const DayNightState& state) noexcept;

}  // namespace snt::game
