#pragma once

#include <cstdint>

#include "core/player/satiation_data.hpp"
#include "core/source_law/player_source_law_data.hpp"

namespace science_and_theology {

// ============================================================
// PlayerVitals — player vitals simulation (health + hunger + atmosphere)
// ------------------------------------------------------------
// Pure C++ simulation core that drives per-frame player vitals:
//   * source law rejection damage
//   * satiation + source law 20 TPS tick
//   * starvation damage
//   * health regeneration (modified by satiation)
//   * atmosphere hazard damage (NONE / THIN / TOXIC / CORROSIVE)
//   * health clamp to [0, health_max]
//
// The caller owns the per-frame context (game mode, gravity zero flag,
// atmosphere type + damage rates from PlanetDescriptor) and pushes it
// via set_* setters before calling tick(delta). This class owns the
// health/timer state so it is the single source of truth for vitals.
//
// GDPlayerVitals (binding) wraps this class for GDScript access.
// ============================================================
class PlayerVitals {
public:
    // Game mode enum mirrors PlayerController.GameMode.
    static constexpr int32_t GAME_MODE_SURVIVAL = 0;
    static constexpr int32_t GAME_MODE_CREATIVE = 1;
    static constexpr int32_t GAME_MODE_OBSERVER = 2;

    // Atmosphere type enum mirrors PlanetDescriptor.AtmosphereType.
    static constexpr int32_t ATMOSPHERE_NONE = 0;
    static constexpr int32_t ATMOSPHERE_THIN = 1;
    static constexpr int32_t ATMOSPHERE_BREATHABLE = 2;
    static constexpr int32_t ATMOSPHERE_TOXIC = 3;
    static constexpr int32_t ATMOSPHERE_CORROSIVE = 4;

    // Default damage rates used when no PlanetDescriptor is available.
    static constexpr double DEFAULT_VACUUM_DAMAGE_PER_SEC = 3.0;
    static constexpr double DEFAULT_TOXIC_DAMAGE_PER_SEC = 5.0;
    static constexpr double DEFAULT_CORROSIVE_DAMAGE_PER_SEC = 8.0;

    PlayerVitals() = default;

    // --- Setup ---
    // Binds the source law + satiation data that drive vitals.
    // Both pointers must be non-null; tick_vitals() is a no-op until
    // setup() is called.
    void setup(source_law::PlayerSourceLawData* source_law,
               SatiationData* satiation) {
        source_law_ = source_law;
        satiation_ = satiation;
    }

    // --- Per-frame context (pushed before tick) ---
    void set_game_mode(int32_t mode) { game_mode_ = mode; }
    void set_flight_enabled(bool enabled) { flight_enabled_ = enabled; }
    // gravity_is_zero gates atmosphere hazard (no hazard in space / fly mode).
    void set_gravity_is_zero(bool is_zero) { gravity_is_zero_ = is_zero; }
    // Sets atmosphere type + per-second damage rates for NONE/THIN, TOXIC,
    // CORROSIVE. BREATHABLE ignores the rates. Caller typically pulls these
    // from PlanetDescriptor fields, falling back to DEFAULT_*_PER_SEC.
    void set_atmosphere(int32_t atmosphere_type,
                        double vacuum_damage_per_sec,
                        double toxic_damage_per_sec,
                        double corrosive_damage_per_sec) {
        atmosphere_type_ = atmosphere_type;
        vacuum_damage_per_sec_ = vacuum_damage_per_sec;
        toxic_damage_per_sec_ = toxic_damage_per_sec;
        corrosive_damage_per_sec_ = corrosive_damage_per_sec;
    }

    // --- Per-frame simulation ---
    // Replaces PlayerController._update_atmosphere_hazard + _process_vitals.
    // Order: atmosphere hazard -> rejection damage -> satiation/source_law
    // tick -> starvation damage -> health regen -> clamp.
    void tick(double delta);

    // --- Health accessors ---
    double health_current() const { return health_current_; }
    void set_health_current(double value) { health_current_ = value; }
    int32_t health_max() const;
    double health_regen() const;

    // --- Source law passthrough ---
    bool is_rejecting() const;

    // --- State inspection (for tests) ---
    double atmo_damage_timer() const { return atmo_damage_timer_; }
    double health_regen_timer() const { return health_regen_timer_; }
    double satiation_tick_timer() const { return satiation_tick_timer_; }

    // --- Serialization ---
    // Stores only vitals-owned state (health + timers). Source law and
    // satiation serialize themselves via their own to/from_serialized.
    struct SerializedState {
        double health_current = 100.0;
        double health_regen_timer = 0.0;
        double atmo_damage_timer = 0.0;
        double satiation_tick_timer = 0.0;
    };
    SerializedState to_serialized() const;
    void from_serialized(const SerializedState& data);

private:
    // Constants matching PlayerController.gd.
    static constexpr double kSatiationTickInterval = 0.05;  // 20 TPS
    static constexpr double kHealthRegenInterval = 2.0;     // seconds

    source_law::PlayerSourceLawData* source_law_ = nullptr;
    SatiationData* satiation_ = nullptr;

    // Per-frame context (pushed by caller).
    int32_t game_mode_ = GAME_MODE_SURVIVAL;
    bool flight_enabled_ = false;
    bool gravity_is_zero_ = false;
    int32_t atmosphere_type_ = ATMOSPHERE_BREATHABLE;
    double vacuum_damage_per_sec_ = DEFAULT_VACUUM_DAMAGE_PER_SEC;
    double toxic_damage_per_sec_ = DEFAULT_TOXIC_DAMAGE_PER_SEC;
    double corrosive_damage_per_sec_ = DEFAULT_CORROSIVE_DAMAGE_PER_SEC;

    // Vitals-owned state.
    double health_current_ = 100.0;
    double health_regen_timer_ = 0.0;
    double atmo_damage_timer_ = 0.0;
    double satiation_tick_timer_ = 0.0;

    // Helper: applies atmosphere damage every 1 second.
    void tick_atmosphere_hazard(double delta);
    // Helper: applies rejection damage, satiation/source_law tick,
    // starvation damage, health regen, and final clamp.
    void tick_vitals(double delta);
};

} // namespace science_and_theology
