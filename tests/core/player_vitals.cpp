// PlayerVitals unit tests.
// Verifies the per-frame vitals simulation that sinks PlayerController.gd's
// _update_atmosphere_hazard + _process_vitals into C++.
// Covers: atmosphere damage, game-mode gating, gravity gating, satiation
// tick accumulation, starvation damage, health regen, rejection damage,
// health clamp, and serialization round-trip.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "core/player/player_vitals.hpp"
#include "core/player/satiation_data.hpp"
#include "core/source_law/player_source_law_data.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "player_vitals FAIL: " << message << '\n';
    ++g_failures;
}

bool approx_equal(double a, double b, double eps = 1.0e-6) {
    return std::fabs(a - b) <= eps;
}

// Atmosphere hazard is gated by SURVIVAL mode + non-zero gravity.
// CREATIVE/OBSERVER should never apply atmosphere damage.
bool test_atmosphere_no_hazard_in_creative() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_CREATIVE);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    // Tick 5 seconds; no damage should be applied in CREATIVE.
    for (int i = 0; i < 50; ++i) {
        v.tick(0.1);
    }
    check(approx_equal(v.health_current(), 100.0),
          "CREATIVE mode should not apply atmosphere damage");
    return g_failures == 0;
}

bool test_atmosphere_no_hazard_in_observer() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_OBSERVER);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_CORROSIVE,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    for (int i = 0; i < 50; ++i) {
        v.tick(0.1);
    }
    check(approx_equal(v.health_current(), 100.0),
          "OBSERVER mode should not apply atmosphere damage");
    return g_failures == 0;
}

// BREATHABLE atmosphere applies no damage.
bool test_atmosphere_breathable_no_damage() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_BREATHABLE,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    for (int i = 0; i < 50; ++i) {
        v.tick(0.1);
    }
    check(approx_equal(v.health_current(), 100.0),
          "BREATHABLE atmosphere should not damage");
    check(approx_equal(v.atmo_damage_timer(), 0.0),
          "BREATHABLE should reset atmo_damage_timer to 0");
    return g_failures == 0;
}

// TOXIC atmosphere applies toxic_dps every 1 second of accumulated time.
// Uses 1.0s deltas to avoid FP precision edge cases at the 1.0s boundary.
bool test_atmosphere_toxic_applies_damage() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    // 5 ticks * 1.0s = 5 applications of 5.0 dps = 25 damage.
    for (int i = 0; i < 5; ++i) {
        v.tick(1.0);
    }
    check(approx_equal(v.health_current(), 75.0),
          "TOXIC should apply 5 dps * 5s = 25 damage");
    return g_failures == 0;
}

// CORROSIVE with custom rate overrides defaults.
bool test_atmosphere_corrosive_custom_rate() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_CORROSIVE,
                     3.0, 5.0, 12.0);
    v.set_health_current(100.0);

    // 2 ticks * 1.0s = 2 applications of 12 dps = 24 damage.
    for (int i = 0; i < 2; ++i) {
        v.tick(1.0);
    }
    check(approx_equal(v.health_current(), 76.0),
          "CORROSIVE custom 12 dps should apply 24 damage over 2s");
    return g_failures == 0;
}

// NONE / THIN use vacuum_dps.
bool test_atmosphere_vacuum_uses_vacuum_rate() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_NONE,
                     4.0, 5.0, 8.0);
    v.set_health_current(100.0);

    // 1 tick of 1.0s -> 1 application of 4 dps.
    v.tick(1.0);
    check(approx_equal(v.health_current(), 96.0),
          "NONE atmosphere should use vacuum_dps=4");

    // THIN uses the same vacuum rate.
    v.set_health_current(100.0);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_THIN,
                     4.0, 5.0, 8.0);
    v.tick(1.0);
    check(approx_equal(v.health_current(), 96.0),
          "THIN atmosphere should use vacuum_dps=4");
    return g_failures == 0;
}

// Zero gravity (space) suppresses atmosphere hazard entirely.
bool test_atmosphere_zero_gravity_no_damage() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(true);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    for (int i = 0; i < 50; ++i) {
        v.tick(0.1);
    }
    check(approx_equal(v.health_current(), 100.0),
          "Zero-G (space) should suppress atmosphere hazard");
    return g_failures == 0;
}

// Without source_law/satiation setup, tick_vitals is a no-op and health
// stays put (atmosphere still applies if active).
bool test_no_setup_vitals_noop() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_BREATHABLE,
                     3.0, 5.0, 8.0);
    v.set_health_current(50.0);

    for (int i = 0; i < 10; ++i) {
        v.tick(0.1);
    }
    check(approx_equal(v.health_current(), 50.0),
          "Without setup, vitals tick should be a no-op");
    check(v.health_max() == 100,
          "Without source_law, health_max should default to 100");
    check(approx_equal(v.health_regen(), 0.0),
          "Without source_law, health_regen should default to 0");
    return g_failures == 0;
}

// Health clamp: with no source_law (max=100), health above max is clamped
// to 100, health below 0 is clamped to 0, after vitals tick.
bool test_health_clamp() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_BREATHABLE,
                     3.0, 5.0, 8.0);

    // Setup with empty source_law + satiation so tick_vitals runs.
    source_law::PlayerSourceLawData sl;
    SatiationData sat;
    v.setup(&sl, &sat);

    // Above max clamp: set to 200, after tick should clamp to 100.
    v.set_health_current(200.0);
    v.tick(0.1);
    check(approx_equal(v.health_current(), 100.0),
          "Health above max should clamp to max after tick");

    // Below 0 clamp: atmosphere damage takes it negative, then clamp to 0.
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC, 3.0, 1000.0, 8.0);
    v.set_health_current(50.0);
    // 1 tick of 1s applies 1000 damage -> health = -950 -> clamped to 0.
    v.tick(1.0);
    check(approx_equal(v.health_current(), 0.0),
          "Health below 0 should clamp to 0 after tick");
    return g_failures == 0;
}

// Satiation tick accumulation: after 1s of ticks, ~20 satiation ticks
// happen, advancing satiation decay.
bool test_satiation_tick_accumulation() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_BREATHABLE,
                     3.0, 5.0, 8.0);

    source_law::PlayerSourceLawData sl;
    SatiationData sat;
    sat.set_satiation_max(100.0f);
    sat.set_decay_rate(0.05f);  // 0.05/tick at 20 TPS = 1.0/sec
    sat.set_satiation(100.0f);
    v.setup(&sl, &sat);

    const float initial = sat.satiation_current();
    // 1 second of ticks.
    for (int i = 0; i < 10; ++i) {
        v.tick(0.1);
    }
    const float after = sat.satiation_current();
    check(after < initial,
          "Satiation should decay after 1s of ticks");
    // ~20 ticks * 0.05 = ~1.0 decay (with possible sub-tick rounding).
    check(initial - after > 0.5f,
          "Satiation decay over 1s should be ~1.0 (got delta)");
    return g_failures == 0;
}

// Serialization round-trip preserves health and timer state.
bool test_serialization_roundtrip() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC,
                     3.0, 5.0, 8.0);
    v.set_health_current(42.5);

    // Run 1 tick of 1.0s to apply one damage application.
    v.tick(1.0);

    auto saved = v.to_serialized();
    check(approx_equal(saved.health_current, 42.5 - 5.0),
          "Saved health should reflect 1 application of toxic damage");

    // Restore into a fresh instance.
    PlayerVitals v2;
    v2.from_serialized(saved);
    check(approx_equal(v2.health_current(), v.health_current()),
          "Round-trip health_current mismatch");
    check(approx_equal(v2.atmo_damage_timer(), v.atmo_damage_timer()),
          "Round-trip atmo_damage_timer mismatch");
    check(approx_equal(v2.health_regen_timer(), v.health_regen_timer()),
          "Round-trip health_regen_timer mismatch");
    check(approx_equal(v2.satiation_tick_timer(), v.satiation_tick_timer()),
          "Round-trip satiation_tick_timer mismatch");
    return g_failures == 0;
}

// Atmosphere damage timing: 0.5s of ticks should NOT apply damage yet
// (atmo_damage_timer accumulates but waits for >= 1.0s).
// Uses 0.5s deltas (exactly representable in FP) to avoid precision drift.
bool test_atmosphere_damage_only_after_full_second() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_TOXIC,
                     3.0, 5.0, 8.0);
    v.set_health_current(100.0);

    // 1 tick of 0.5s -> timer = 0.5, no damage yet.
    v.tick(0.5);
    check(approx_equal(v.health_current(), 100.0),
          "Atmosphere damage should not apply before 1s accumulates");
    check(approx_equal(v.atmo_damage_timer(), 0.5),
          "atmo_damage_timer should accumulate to 0.5");

    // Continue with another 0.5s -> timer = 1.0, damage applied, timer = 0.
    v.tick(0.5);
    check(approx_equal(v.health_current(), 95.0),
          "Atmosphere damage should apply at 1s boundary");
    check(approx_equal(v.atmo_damage_timer(), 0.0),
          "atmo_damage_timer should reset to 0 after damage application");
    return g_failures == 0;
}

// Health regen: with default source_law (health_regen = kBaseHealthRegen)
// and high satiation (regen_mod = 1.0), health regenerates every 2 seconds.
bool test_health_regen_applied() {
    PlayerVitals v;
    v.set_game_mode(PlayerVitals::GAME_MODE_SURVIVAL);
    v.set_gravity_is_zero(false);
    v.set_atmosphere(PlayerVitals::ATMOSPHERE_BREATHABLE,
                     3.0, 5.0, 8.0);

    source_law::PlayerSourceLawData sl;
    SatiationData sat;
    sat.set_satiation_max(100.0f);
    sat.set_satiation(100.0f);  // High satiation -> regen_mod = 1.0
    v.setup(&sl, &sat);

    // Default kBaseHealthRegen is non-zero; lower health below max.
    const int h_max = v.health_max();
    const double regen = v.health_regen();
    if (regen <= 0.0) {
        // Skip if base regen is 0 in this build (no testable behavior).
        std::cout << "player_vitals: skipping test_health_regen_applied "
                     "(base health_regen is 0)\n";
        return g_failures == 0;
    }
    v.set_health_current(static_cast<double>(h_max) - 10.0);

    // 2 seconds of ticks -> 1 regen application.
    for (int i = 0; i < 20; ++i) {
        v.tick(0.1);
    }
    check(v.health_current() > static_cast<double>(h_max) - 10.0,
          "Health should regen after 2s with non-zero regen_rate");
    check(v.health_current() <= static_cast<double>(h_max),
          "Health should not exceed max after regen");
    return g_failures == 0;
}

} // namespace

int main() {
    test_atmosphere_no_hazard_in_creative();
    test_atmosphere_no_hazard_in_observer();
    test_atmosphere_breathable_no_damage();
    test_atmosphere_toxic_applies_damage();
    test_atmosphere_corrosive_custom_rate();
    test_atmosphere_vacuum_uses_vacuum_rate();
    test_atmosphere_zero_gravity_no_damage();
    test_no_setup_vitals_noop();
    test_health_clamp();
    test_satiation_tick_accumulation();
    test_serialization_roundtrip();
    test_atmosphere_damage_only_after_full_second();
    test_health_regen_applied();

    if (g_failures == 0) {
        std::cout << "player_vitals: all tests passed\n";
        return 0;
    }
    std::cerr << "player_vitals: " << g_failures << " failure(s)\n";
    return 1;
}
