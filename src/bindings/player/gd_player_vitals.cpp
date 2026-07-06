#include "player/gd_player_vitals.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace science_and_theology {

// ============================================================
// Setup
// ============================================================

void GDPlayerVitals::setup(
        const Ref<GDPlayerSourceLawData>& source_law,
        const Ref<GDSatiationData>& satiation) {
    source_law_ = source_law;
    satiation_ = satiation;
    if (source_law_.is_valid() && satiation_.is_valid()) {
        vitals_.setup(&source_law_->data(), &satiation_->data());
    } else {
        vitals_.setup(nullptr, nullptr);
    }
}

// ============================================================
// Per-frame context setters
// ============================================================

void GDPlayerVitals::set_game_mode(int32_t mode) {
    vitals_.set_game_mode(mode);
}

void GDPlayerVitals::set_flight_enabled(bool enabled) {
    vitals_.set_flight_enabled(enabled);
}

void GDPlayerVitals::set_gravity_is_zero(bool is_zero) {
    vitals_.set_gravity_is_zero(is_zero);
}

void GDPlayerVitals::set_atmosphere(
        int32_t atmosphere_type,
        double vacuum_damage_per_sec,
        double toxic_damage_per_sec,
        double corrosive_damage_per_sec) {
    vitals_.set_atmosphere(
        atmosphere_type,
        vacuum_damage_per_sec,
        toxic_damage_per_sec,
        corrosive_damage_per_sec);
}

// ============================================================
// Per-frame simulation
// ============================================================

void GDPlayerVitals::tick(double delta) {
    vitals_.tick(delta);
}

// ============================================================
// Health accessors
// ============================================================

double GDPlayerVitals::get_health_current() const {
    return vitals_.health_current();
}

void GDPlayerVitals::set_health_current(double value) {
    vitals_.set_health_current(value);
}

int32_t GDPlayerVitals::get_health_max() const {
    return vitals_.health_max();
}

double GDPlayerVitals::get_health_regen() const {
    return vitals_.health_regen();
}

// ============================================================
// Source law passthrough
// ============================================================

bool GDPlayerVitals::is_rejecting() const {
    return vitals_.is_rejecting();
}

Dictionary GDPlayerVitals::get_rejection() const {
    if (source_law_.is_null()) {
        return Dictionary{};
    }
    return source_law_->get_rejection();
}

Ref<GDPlayerSourceLawData> GDPlayerVitals::get_source_law() const {
    return source_law_;
}

Ref<GDSatiationData> GDPlayerVitals::get_satiation() const {
    return satiation_;
}

// ============================================================
// Serialization
// ============================================================

Dictionary GDPlayerVitals::to_dict() const {
    auto s = vitals_.to_serialized();
    Dictionary dict;
    dict["health_current"] = s.health_current;
    dict["health_regen_timer"] = s.health_regen_timer;
    dict["atmo_damage_timer"] = s.atmo_damage_timer;
    dict["satiation_tick_timer"] = s.satiation_tick_timer;
    return dict;
}

void GDPlayerVitals::from_dict(const Dictionary& data) {
    PlayerVitals::SerializedState s;
    s.health_current = static_cast<double>(data.get("health_current", 100.0));
    s.health_regen_timer = static_cast<double>(data.get("health_regen_timer", 0.0));
    s.atmo_damage_timer = static_cast<double>(data.get("atmo_damage_timer", 0.0));
    s.satiation_tick_timer = static_cast<double>(data.get("satiation_tick_timer", 0.0));
    vitals_.from_serialized(s);
}

// ============================================================
// Bind methods
// ============================================================

void GDPlayerVitals::_bind_methods() {
    using B = GDPlayerVitals;

    // Setup
    ClassDB::bind_method(D_METHOD("setup", "source_law", "satiation"),
                         &B::setup);

    // Per-frame context
    ClassDB::bind_method(D_METHOD("set_game_mode", "mode"),
                         &B::set_game_mode);
    ClassDB::bind_method(D_METHOD("set_flight_enabled", "enabled"),
                         &B::set_flight_enabled);
    ClassDB::bind_method(D_METHOD("set_gravity_is_zero", "is_zero"),
                         &B::set_gravity_is_zero);
    ClassDB::bind_method(D_METHOD(
        "set_atmosphere",
        "atmosphere_type",
        "vacuum_damage_per_sec",
        "toxic_damage_per_sec",
        "corrosive_damage_per_sec"),
        &B::set_atmosphere);

    // Per-frame tick
    ClassDB::bind_method(D_METHOD("tick", "delta"), &B::tick);

    // Health accessors
    ClassDB::bind_method(D_METHOD("get_health_current"),
                         &B::get_health_current);
    ClassDB::bind_method(D_METHOD("set_health_current", "value"),
                         &B::set_health_current);
    ClassDB::bind_method(D_METHOD("get_health_max"),
                         &B::get_health_max);
    ClassDB::bind_method(D_METHOD("get_health_regen"),
                         &B::get_health_regen);

    // Source law passthrough
    ClassDB::bind_method(D_METHOD("is_rejecting"), &B::is_rejecting);
    ClassDB::bind_method(D_METHOD("get_rejection"), &B::get_rejection);
    ClassDB::bind_method(D_METHOD("get_source_law"), &B::get_source_law);
    ClassDB::bind_method(D_METHOD("get_satiation"), &B::get_satiation);

    // Serialization
    ClassDB::bind_method(D_METHOD("to_dict"), &B::to_dict);
    ClassDB::bind_method(D_METHOD("from_dict", "data"), &B::from_dict);

    // Default damage rate constants.
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "DEFAULT_VACUUM_DAMAGE_PER_SEC",
        static_cast<int64_t>(DEFAULT_VACUUM_DAMAGE_PER_SEC));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "DEFAULT_TOXIC_DAMAGE_PER_SEC",
        static_cast<int64_t>(DEFAULT_TOXIC_DAMAGE_PER_SEC));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "DEFAULT_CORROSIVE_DAMAGE_PER_SEC",
        static_cast<int64_t>(DEFAULT_CORROSIVE_DAMAGE_PER_SEC));

    // Game mode constants.
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "GAME_MODE_SURVIVAL",
        static_cast<int64_t>(GAME_MODE_SURVIVAL));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "GAME_MODE_CREATIVE",
        static_cast<int64_t>(GAME_MODE_CREATIVE));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "GAME_MODE_OBSERVER",
        static_cast<int64_t>(GAME_MODE_OBSERVER));

    // Atmosphere type constants.
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "ATMOSPHERE_NONE",
        static_cast<int64_t>(ATMOSPHERE_NONE));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "ATMOSPHERE_THIN",
        static_cast<int64_t>(ATMOSPHERE_THIN));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "ATMOSPHERE_BREATHABLE",
        static_cast<int64_t>(ATMOSPHERE_BREATHABLE));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "ATMOSPHERE_TOXIC",
        static_cast<int64_t>(ATMOSPHERE_TOXIC));
    godot::ClassDB::bind_integer_constant(
        "GDPlayerVitals", "", "ATMOSPHERE_CORROSIVE",
        static_cast<int64_t>(ATMOSPHERE_CORROSIVE));
}

} // namespace science_and_theology
