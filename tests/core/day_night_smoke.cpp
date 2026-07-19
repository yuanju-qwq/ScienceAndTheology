// Legacy simulation reference for day/night behavior not yet in the new runtime.

#include "simulation/day_night_def.hpp"
#include "world/gameplay_config.hpp"

#include <cmath>
#include <iostream>

using namespace science_and_theology;

namespace {

bool expect(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::cerr << "Day/night smoke failed: " << message << '\n';
    return false;
}

}  // namespace

int main() {
    const GameplayConfig config;
    const float time_of_day = compute_time_of_day(
        0, 12000, config.day_start_time);
    const DayNightState state = compute_day_night_state(
        time_of_day, config.twilight_fraction);

    return expect(std::abs(time_of_day - 0.5f) < 0.0001f,
                  "new world does not start at noon")
            && expect(state.is_daytime && state.sun_light_energy > 2.0f,
                      "new world default lighting is not daytime")
            && expect(std::abs(compute_time_of_day(
                          6000, 12000, config.day_start_time)) < 0.0001f,
                      "day phase offset does not wrap to midnight")
        ? 0
        : 1;
}
