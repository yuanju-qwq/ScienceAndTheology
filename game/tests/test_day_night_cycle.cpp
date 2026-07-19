// Coverage for the game-owned day/night cycle and its renderer adapter.

#include "game/client/day_night_lighting.h"
#include "game/client/game_session_config.h"
#include "game/simulation/day_night_cycle.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr float kEpsilon = 0.0001f;

}  // namespace

TEST(GameDayNightCycleTest, StartsAtNoonAndWrapsAtMidnight) {
    snt::game::GameplayConfig config;
    snt::game::DayNightCycle cycle;

    cycle.update(0, 0.05f, config, "overworld");
    const snt::game::DayNightState noon = cycle.state();
    EXPECT_NEAR(noon.time_of_day, 0.5f, kEpsilon);
    EXPECT_TRUE(noon.is_daytime);
    EXPECT_GT(noon.sun_intensity, 2.0f);
    EXPECT_EQ(noon.source_tick, 0u);

    cycle.update(6000, 0.05f, config, "overworld");
    const snt::game::DayNightState midnight = cycle.state();
    EXPECT_NEAR(midnight.time_of_day, 0.0f, kEpsilon);
    EXPECT_FALSE(midnight.is_daytime);
    EXPECT_NEAR(midnight.sun_intensity, 0.0f, kEpsilon);
    EXPECT_GT(midnight.moon_intensity, 0.1f);
    EXPECT_EQ(midnight.source_tick, 6000u);
}

TEST(GameDayNightCycleTest, AppliesDimensionOverridesAndDisabledCycle) {
    snt::game::GameplayConfig config;
    snt::game::GameplayConfig::PlanetOverride quick_day;
    quick_day.has_day_length_seconds = true;
    quick_day.day_length_seconds = 1.0f;
    config.planet_overrides.emplace("quick", quick_day);

    snt::game::DayNightCycle cycle;
    cycle.update(10, 0.05f, config, "quick");
    EXPECT_NEAR(cycle.state().time_of_day, 0.0f, kEpsilon);
    EXPECT_FALSE(cycle.state().is_daytime);

    config.enable_day_night = false;
    cycle.update(1000, 0.05f, config, "overworld");
    EXPECT_NEAR(cycle.state().time_of_day, 0.5f, kEpsilon);
    EXPECT_TRUE(cycle.state().is_daytime);
    EXPECT_GT(cycle.state().sun_intensity, 2.0f);
}

TEST(GameDayNightLightingTest, ConvertsCelestialStateToRendererContract) {
    const snt::game::DayNightState noon = snt::game::compute_day_night_state(0.5f, 0.1f);
    const snt::render::EnvironmentLighting noon_lighting =
        snt::game::make_environment_lighting(noon);

    EXPECT_NEAR(noon_lighting.sun.direction_to_light[0], 0.0f, kEpsilon);
    EXPECT_NEAR(noon_lighting.sun.direction_to_light[1], 1.0f, kEpsilon);
    EXPECT_NEAR(noon_lighting.sun.direction_to_light[2], 0.0f, kEpsilon);
    EXPECT_NEAR(noon_lighting.sun.intensity, noon.sun_intensity, kEpsilon);
    EXPECT_NEAR(noon_lighting.ambient_intensity, noon.ambient_intensity, kEpsilon);
    EXPECT_EQ(noon_lighting.sky_color[3], 1.0f);

    const snt::game::DayNightState midnight =
        snt::game::compute_day_night_state(0.0f, 0.1f);
    const snt::render::EnvironmentLighting midnight_lighting =
        snt::game::make_environment_lighting(midnight);
    EXPECT_NEAR(midnight_lighting.moon.direction_to_light[1], 1.0f, kEpsilon);
    EXPECT_GT(midnight_lighting.moon.intensity, 0.1f);
    EXPECT_LT(midnight_lighting.sky_color[2], noon_lighting.sky_color[2]);
}

TEST(GameSessionConfigTest, LoadsDayNightConfigurationAndDimensionOverrides) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("snt_day_night_config_" + std::to_string(
                          std::chrono::steady_clock::now().time_since_epoch().count()) +
                       ".json");
    {
        std::ofstream output(path);
        ASSERT_TRUE(output.is_open());
        output << R"json({
            "gameplay": {
                "day_length_seconds": 480.0,
                "twilight_fraction": 0.2,
                "day_start_time": 0.35,
                "days_per_season": 24,
                "enable_season_colors": false,
                "planet_overrides": {
                    "short_day": {
                        "has_day_length_seconds": true,
                        "day_length_seconds": 20.0,
                        "has_day_start_time": true,
                        "day_start_time": 0.25
                    }
                }
            }
        })json";
    }

    const auto config = snt::game::load_game_session_config(path.string());
    ASSERT_TRUE(config) << config.error().format();
    EXPECT_NEAR(config->gameplay.day_length_seconds, 480.0f, kEpsilon);
    EXPECT_NEAR(config->gameplay.twilight_fraction, 0.2f, kEpsilon);
    EXPECT_EQ(config->gameplay.days_per_season, 24);
    EXPECT_FALSE(config->gameplay.enable_season_colors);
    EXPECT_NEAR(config->gameplay.get_day_length_seconds("short_day"), 20.0f, kEpsilon);
    EXPECT_NEAR(config->gameplay.get_day_start_time("short_day"), 0.25f, kEpsilon);

    std::error_code error;
    std::filesystem::remove(path, error);
    EXPECT_FALSE(error) << error.message();
}
