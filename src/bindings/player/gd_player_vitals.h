#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/player/player_vitals.hpp"
#include "player/gd_satiation_data.hpp"
#include "source_law/gd_player_source_law_data.hpp"

namespace science_and_theology {

// ============================================================
// GDPlayerVitals — GDExtension binding for PlayerVitals
// ------------------------------------------------------------
// Sinks the per-frame vitals simulation that used to live in
// PlayerController.gd (atmosphere hazard + source law rejection +
// satiation/source_law 20 TPS tick + starvation damage + health regen +
// health clamp). GD pushes the per-frame context (game mode, gravity
// zero flag, atmosphere type + damage rates from PlanetDescriptor) via
// set_* setters before calling tick(delta). The C++ side owns the
// health/timer state so it is the single source of truth for vitals.
// ============================================================
class GDPlayerVitals : public godot::RefCounted {
    GDCLASS(GDPlayerVitals, godot::RefCounted)

public:
    // Game mode enum mirrors PlayerController.GameMode.
    static constexpr int32_t GAME_MODE_SURVIVAL =
        PlayerVitals::GAME_MODE_SURVIVAL;
    static constexpr int32_t GAME_MODE_CREATIVE =
        PlayerVitals::GAME_MODE_CREATIVE;
    static constexpr int32_t GAME_MODE_OBSERVER =
        PlayerVitals::GAME_MODE_OBSERVER;

    // Atmosphere type enum mirrors PlanetDescriptor.AtmosphereType.
    static constexpr int32_t ATMOSPHERE_NONE =
        PlayerVitals::ATMOSPHERE_NONE;
    static constexpr int32_t ATMOSPHERE_THIN =
        PlayerVitals::ATMOSPHERE_THIN;
    static constexpr int32_t ATMOSPHERE_BREATHABLE =
        PlayerVitals::ATMOSPHERE_BREATHABLE;
    static constexpr int32_t ATMOSPHERE_TOXIC =
        PlayerVitals::ATMOSPHERE_TOXIC;
    static constexpr int32_t ATMOSPHERE_CORROSIVE =
        PlayerVitals::ATMOSPHERE_CORROSIVE;

    // Default damage rates used when no PlanetDescriptor is available.
    // Exposed as static constants so GDScript can read them when building
    // the per-frame atmosphere context.
    static constexpr double DEFAULT_VACUUM_DAMAGE_PER_SEC =
        PlayerVitals::DEFAULT_VACUUM_DAMAGE_PER_SEC;
    static constexpr double DEFAULT_TOXIC_DAMAGE_PER_SEC =
        PlayerVitals::DEFAULT_TOXIC_DAMAGE_PER_SEC;
    static constexpr double DEFAULT_CORROSIVE_DAMAGE_PER_SEC =
        PlayerVitals::DEFAULT_CORROSIVE_DAMAGE_PER_SEC;

    GDPlayerVitals() = default;
    ~GDPlayerVitals() override = default;

    // --- Setup ---
    // Binds the source law + satiation resources that drive vitals.
    // Both refs must be non-null; tick() is a no-op until setup() is called.
    void setup(
        const godot::Ref<GDPlayerSourceLawData>& source_law,
        const godot::Ref<GDSatiationData>& satiation);

    // --- Per-frame context (pushed by GD before tick) ---
    void set_game_mode(int32_t mode);
    void set_flight_enabled(bool enabled);
    // gravity_is_zero gates atmosphere hazard (no hazard in space / fly mode).
    void set_gravity_is_zero(bool is_zero);
    // Sets atmosphere type + per-second damage rates for NONE/THIN, TOXIC,
    // CORROSIVE. BREATHABLE ignores the rates. GD typically pulls these
    // from PlanetDescriptor fields, falling back to DEFAULT_*_PER_SEC.
    void set_atmosphere(
        int32_t atmosphere_type,
        double vacuum_damage_per_sec,
        double toxic_damage_per_sec,
        double corrosive_damage_per_sec);

    // --- Per-frame simulation ---
    // Replaces PlayerController._update_atmosphere_hazard + _process_vitals.
    // Order: atmosphere hazard -> rejection damage -> satiation/source_law
    // tick -> starvation damage -> health regen -> clamp.
    void tick(double delta);

    // --- Health accessors ---
    double get_health_current() const;
    void set_health_current(double value);
    int32_t get_health_max() const;
    double get_health_regen() const;

    // --- Source law passthrough ---
    bool is_rejecting() const;
    godot::Dictionary get_rejection() const;
    godot::Ref<GDPlayerSourceLawData> get_source_law() const;
    godot::Ref<GDSatiationData> get_satiation() const;

    // --- Serialization ---
    // Stores only vitals-owned state (health + timers). Source law and
    // satiation serialize themselves via their own to_dict / from_dict.
    godot::Dictionary to_dict() const;
    void from_dict(const godot::Dictionary& data);

protected:
    static void _bind_methods();

private:
    PlayerVitals vitals_;
    godot::Ref<GDPlayerSourceLawData> source_law_;
    godot::Ref<GDSatiationData> satiation_;
};

} // namespace science_and_theology
