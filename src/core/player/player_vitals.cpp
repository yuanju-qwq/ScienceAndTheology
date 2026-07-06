#include "core/player/player_vitals.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// Per-frame simulation
// ============================================================

void PlayerVitals::tick(double delta) {
    // Atmosphere hazard only checks SURVIVAL + non-zero gravity; it is
    // independent of source_law/satiation availability.
    tick_atmosphere_hazard(delta);

    // Vitals tick is gated by source_law/satiation availability and
    // also skips CREATIVE/OBSERVER modes (matches GD _process_vitals).
    if (game_mode_ == GAME_MODE_CREATIVE || game_mode_ == GAME_MODE_OBSERVER) {
        return;
    }
    if (source_law_ == nullptr || satiation_ == nullptr) {
        return;
    }
    tick_vitals(delta);
}

void PlayerVitals::tick_atmosphere_hazard(double delta) {
    // No hazard in non-survival modes.
    if (game_mode_ != GAME_MODE_SURVIVAL) {
        return;
    }
    // No hazard in space (zero-G, no active planet).
    if (gravity_is_zero_) {
        return;
    }

    double damage_rate = 0.0;
    switch (atmosphere_type_) {
        case ATMOSPHERE_NONE:
        case ATMOSPHERE_THIN:
            damage_rate = vacuum_damage_per_sec_;
            break;
        case ATMOSPHERE_TOXIC:
            damage_rate = toxic_damage_per_sec_;
            break;
        case ATMOSPHERE_CORROSIVE:
            damage_rate = corrosive_damage_per_sec_;
            break;
        case ATMOSPHERE_BREATHABLE:
        default:
            damage_rate = 0.0;
            break;
    }

    if (damage_rate <= 0.0) {
        atmo_damage_timer_ = 0.0;
        return;
    }

    atmo_damage_timer_ += delta;
    // Apply damage to player health every 1 second.
    if (atmo_damage_timer_ >= 1.0) {
        atmo_damage_timer_ -= 1.0;
        health_current_ -= damage_rate;
    }
}

void PlayerVitals::tick_vitals(double delta) {
    // --- Source law rejection damage ---
    if (source_law_->is_rejecting()) {
        const auto* rej = source_law_->rejection();
        if (rej != nullptr) {
            health_current_ -= static_cast<double>(rej->damage_per_tick);
        }
    }

    // --- Satiation + source law 20 TPS tick ---
    satiation_tick_timer_ += delta;
    while (satiation_tick_timer_ >= kSatiationTickInterval) {
        satiation_tick_timer_ -= kSatiationTickInterval;
        satiation_->tick();
        source_law_->tick();

        // Starvation damage applied per satiation tick.
        const double starve_dmg =
            static_cast<double>(satiation_->starvation_damage_per_tick());
        if (starve_dmg > 0.0) {
            health_current_ = std::max(0.0, health_current_ - starve_dmg);
        }
    }

    // --- Health regeneration (modified by satiation) ---
    const int32_t h_max = health_max();
    const double regen_rate = health_regen();
    if (health_current_ < static_cast<double>(h_max) && regen_rate > 0.0) {
        health_regen_timer_ += delta;
        const double regen_mod =
            static_cast<double>(satiation_->health_regen_modifier());
        if (regen_mod > 0.0) {
            while (health_regen_timer_ >= kHealthRegenInterval) {
                health_regen_timer_ -= kHealthRegenInterval;
                health_current_ = std::min(
                    health_current_ + regen_rate * regen_mod,
                    static_cast<double>(h_max));
            }
        }
    } else {
        health_regen_timer_ = 0.0;
    }

    // --- Clamp health to valid range ---
    health_current_ = std::clamp(
        health_current_, 0.0, static_cast<double>(h_max));
}

// ============================================================
// Health accessors
// ============================================================

int32_t PlayerVitals::health_max() const {
    if (source_law_ == nullptr) {
        return 100;
    }
    return source_law_->compute_combat_attributes().health_max;
}

double PlayerVitals::health_regen() const {
    if (source_law_ == nullptr) {
        return 0.0;
    }
    return static_cast<double>(
        source_law_->compute_combat_attributes().health_regen);
}

// ============================================================
// Source law passthrough
// ============================================================

bool PlayerVitals::is_rejecting() const {
    if (source_law_ == nullptr) {
        return false;
    }
    return source_law_->is_rejecting();
}

// ============================================================
// Serialization
// ============================================================

PlayerVitals::SerializedState PlayerVitals::to_serialized() const {
    SerializedState s;
    s.health_current = health_current_;
    s.health_regen_timer = health_regen_timer_;
    s.atmo_damage_timer = atmo_damage_timer_;
    s.satiation_tick_timer = satiation_tick_timer_;
    return s;
}

void PlayerVitals::from_serialized(const SerializedState& data) {
    health_current_ = data.health_current;
    health_regen_timer_ = data.health_regen_timer;
    atmo_damage_timer_ = data.atmo_damage_timer;
    satiation_tick_timer_ = data.satiation_tick_timer;
}

} // namespace science_and_theology
