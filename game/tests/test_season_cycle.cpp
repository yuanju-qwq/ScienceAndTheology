// Coverage for the game-owned deterministic seasonal cycle.

#include "game/simulation/season_cycle.h"

#include <gtest/gtest.h>

#include <limits>

namespace {

constexpr float kEpsilon = 0.0001f;

}  // namespace

TEST(GameSeasonCycleTest, TracksDaysSeasonsAndYearWrapFromFixedTicks) {
    snt::game::GameplayConfig config;
    config.days_per_season = 2;
    snt::game::SeasonCycle cycle;

    cycle.update(0, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().source_tick, 0u);
    EXPECT_EQ(cycle.state().game_day, 0u);
    EXPECT_EQ(cycle.state().day_in_season, 0);
    EXPECT_EQ(cycle.state().season, snt::game::Season::SPRING);

    cycle.update(11999, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().game_day, 0u);
    EXPECT_EQ(cycle.state().day_in_season, 0);
    EXPECT_EQ(cycle.state().season, snt::game::Season::SPRING);

    cycle.update(12000, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().game_day, 1u);
    EXPECT_EQ(cycle.state().day_in_season, 1);
    EXPECT_EQ(cycle.state().season, snt::game::Season::SPRING);

    cycle.update(24000, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().game_day, 2u);
    EXPECT_EQ(cycle.state().day_in_season, 0);
    EXPECT_EQ(cycle.state().season, snt::game::Season::SUMMER);

    cycle.update(48000, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().season, snt::game::Season::AUTUMN);
    EXPECT_NEAR(cycle.state().deciduous_color_mod.r, 1.1f, kEpsilon);
    EXPECT_NEAR(cycle.state().deciduous_color_mod.g, 0.85f, kEpsilon);
    EXPECT_NEAR(cycle.state().deciduous_color_mod.b, 0.5f, kEpsilon);

    cycle.update(96000, 0.05f, config, "overworld");
    EXPECT_EQ(cycle.state().season, snt::game::Season::SPRING);
    EXPECT_EQ(cycle.state().day_in_season, 0);
}

TEST(GameSeasonCycleTest, ResolvesDimensionDayLengthAndColorPolicy) {
    snt::game::GameplayConfig config;
    config.days_per_season = 1;
    snt::game::GameplayConfig::PlanetOverride quick_day;
    quick_day.has_day_length_seconds = true;
    quick_day.day_length_seconds = 1.0f;
    config.planet_overrides.emplace("quick", quick_day);

    snt::game::SeasonCycle cycle;
    cycle.update(20, 0.05f, config, "quick");
    EXPECT_EQ(cycle.state().source_tick, 20u);
    EXPECT_EQ(cycle.state().game_day, 1u);
    EXPECT_EQ(cycle.state().season, snt::game::Season::SUMMER);

    cycle.update(40, 0.05f, config, "quick");
    EXPECT_EQ(cycle.state().season, snt::game::Season::AUTUMN);
    EXPECT_LT(cycle.state().evergreen_color_mod.b, 1.0f);

    config.enable_season_colors = false;
    cycle.update(40, 0.05f, config, "quick");
    EXPECT_NEAR(cycle.state().deciduous_color_mod.r, 1.0f, kEpsilon);
    EXPECT_NEAR(cycle.state().deciduous_color_mod.g, 1.0f, kEpsilon);
    EXPECT_NEAR(cycle.state().deciduous_color_mod.b, 1.0f, kEpsilon);
}

TEST(GameSeasonCycleTest, SanitizesInvalidDurationsAndSeasonCounts) {
    const snt::game::SeasonState state = snt::game::compute_season_state(
        16u * 12000u,
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
        0,
        true);

    EXPECT_EQ(state.source_tick, 16u * 12000u);
    EXPECT_EQ(state.game_day, 16u);
    EXPECT_EQ(state.day_in_season, 0);
    EXPECT_EQ(state.season, snt::game::Season::SUMMER);
}
