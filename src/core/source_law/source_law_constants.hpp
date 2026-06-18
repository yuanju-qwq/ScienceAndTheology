#pragma once

#include <cstdint>

namespace science_and_theology::source_law {

// ============================================================
// V0.1 constants for the Source Law Sublimation system
// ============================================================

// Source reserve (面板数值, not HUD bar)
inline constexpr int kDefaultSourceMax = 0;
inline constexpr float kDefaultSourceRegen = 0.0f;

// Stability & mutation
inline constexpr float kDefaultStability = 100.0f;
inline constexpr float kDefaultMutation = 0.0f;
inline constexpr float kMinStability = 0.0f;
inline constexpr float kMaxStability = 100.0f;
inline constexpr float kMaxMutation = 100.0f;

// ============================================================
// Stability & mutation target-convergence model
// ============================================================
//
// Instead of fixed per-tick drift, stability and mutation converge
// toward a target value determined by the organ network.
//
// Target stability = 100 + element_stability_modifier * scale
// Target mutation  =   0 + element_mutation_modifier  * scale
//
// Per tick: delta = (target - current) * rate
// This creates exponential convergence toward the target.

// Convergence rate per tick (20 TPS).
// Half-life ≈ 69 ticks ≈ 3.5 seconds.  95% in ≈ 15 seconds.
inline constexpr float kStabilityConvergenceRate = 0.01f;
inline constexpr float kMutationConvergenceRate = 0.01f;

// Scale factor: amplifies the organ network modifier when computing
// the target value.  Raw modifier values are small (designed for
// per-tick drift), so we scale them up for target calculation.
inline constexpr float kStabilityTargetScale = 1.0f;
inline constexpr float kMutationTargetScale = 5.0f;

// Additional rules (applied after convergence):
// Low stability (< 30) adds extra mutation drift.
inline constexpr float kLowStabilityThreshold = 30.0f;
inline constexpr float kLowStabilityMutationRate = 0.002f;

// High stability (> 70) adds extra mutation reduction.
inline constexpr float kHighStabilityThreshold = 70.0f;
inline constexpr float kHighStabilityMutationReductionRate = 0.001f;

// Severe conflict extra mutation drift (per tick, additive).
inline constexpr float kSevereConflictMutationBonus = 0.05f;

// Mortal mana rule: uninitiated players have zero personal mana
inline constexpr int kMortalMaxMana = 0;
inline constexpr float kMortalManaRegen = 0.0f;

// Psionic
inline constexpr int kDefaultPsionicLevel = 0;

// Mental load
inline constexpr int kDefaultMentalLoad = 0;

// Sublimation
inline constexpr int kMortalSublimationLevel = 0;

// ============================================================
// Source cost element modifier (元素损耗) constants
// ============================================================
//
// When transforming an organ, the actual source cost is modified by
// the element relation between the new organ's element and each
// existing sublimated organ's element.  The worst relation determines
// the cost multiplier.
//
// Same/generating elements are compatible → lower cost.
// Conflicting/severe conflict → higher cost (more waste).

inline constexpr float kSourceCostElementSame = 0.8f;
inline constexpr float kSourceCostElementGenerating = 0.9f;
inline constexpr float kSourceCostElementNeutral = 1.0f;
inline constexpr float kSourceCostElementConflicting = 1.3f;
inline constexpr float kSourceCostElementSevereConflict = 1.8f;

// ============================================================
// Transform rejection (排异掉血) constants
// ============================================================
//
// When an organ is transformed (via elixir or devour), the body
// rejects the new source law organ, causing damage over time.
//
// Formula (per tick):
//   damage = base_dpt * source_mult * stability_factor * mutation_factor
//            * defense_factor * element_factor
//
// Where:
//   source_mult:     SUBLIMATION = 1.0, BLOODLINE = 1.5
//   stability_factor: (100 - stability) / 100  (0.0 ~ 1.0)
//   mutation_factor:  1.0 + mutation / 100     (1.0 ~ 2.0)
//   defense_factor:   1.0 / (1.0 + phys_def / 10)
//   element_factor:   worst element relation to existing organs

// Base damage per tick before modifiers.
inline constexpr float kRejectionBaseDpt = 1.0f;

// Source multiplier: bloodline (devour) is more painful than sublimation.
inline constexpr float kRejectionSublimationMult = 1.0f;
inline constexpr float kRejectionBloodlineMult = 1.5f;

// Duration in ticks (20 TPS).
inline constexpr int kRejectionSublimationDuration = 100;  // 5 seconds
inline constexpr int kRejectionBloodlineDuration = 160;    // 8 seconds

// Element relation multipliers for rejection damage.
inline constexpr float kRejectionElementSame = 0.8f;
inline constexpr float kRejectionElementGenerating = 0.9f;
inline constexpr float kRejectionElementNeutral = 1.0f;
inline constexpr float kRejectionElementConflicting = 1.3f;
inline constexpr float kRejectionElementSevereConflict = 1.8f;

// Combat attribute base values (before organ bonuses)
inline constexpr int kBaseHealth = 100;
inline constexpr int kBaseMana = 50;
inline constexpr float kBasePhysicalAttack = 10.0f;
inline constexpr float kBaseMagicPower = 5.0f;
inline constexpr float kBasePhysicalDefense = 5.0f;
inline constexpr float kBaseElementResistance = 0.0f;
inline constexpr float kBaseMoveSpeed = 100.0f;
inline constexpr float kBaseAttackSpeed = 1.0f;
inline constexpr float kBaseCastSpeed = 1.0f;
inline constexpr float kBaseCritRate = 0.0f;
inline constexpr float kBaseCritDamage = 1.5f;
inline constexpr float kBaseDodgeRate = 0.0f;
inline constexpr float kBaseHealthRegen = 0.0f;
inline constexpr float kBaseManaRegen = 0.0f;

// Per-level organ stat contributions (from design doc 20.3.4)
struct OrganStatContribution {
    int health = 0;
    int mana = 0;
    float physical_attack = 0.0f;
    float magic_power = 0.0f;
    float physical_defense = 0.0f;
    float element_resistance = 0.0f;
    float move_speed_pct = 0.0f;
    float attack_speed_pct = 0.0f;
    float cast_speed_pct = 0.0f;
    float crit_rate_pct = 0.0f;
    float crit_damage_pct = 0.0f;
    float dodge_rate_pct = 0.0f;
    float health_regen = 0.0f;
    float mana_regen = 0.0f;
    // Source essence capacity bonus per organ level.
    // All organs contribute to the total source essence cap.
    float source_essence_cap = 0.0f;
};

inline OrganStatContribution get_organ_stat_contribution(OrganSlot slot) {
    OrganStatContribution c;
    switch (slot) {
        case OrganSlot::HEART:
            c.mana = 12;
            c.physical_attack = 1.5f;
            c.magic_power = 2.5f;
            c.cast_speed_pct = 1.5f;
            c.crit_damage_pct = 8.0f;
            c.mana_regen = 0.6f;
            c.source_essence_cap = 5.0f;
            break;
        case OrganSlot::BONE:
            c.health = 15;
            c.physical_attack = 3.0f;
            c.physical_defense = 2.0f;
            c.source_essence_cap = 3.0f;
            break;
        case OrganSlot::BLOOD:
            c.health = 8;
            c.mana = 2;
            c.element_resistance = 1.0f;
            c.health_regen = 0.5f;
            c.mana_regen = 0.9f;
            c.source_essence_cap = 8.0f;
            break;
        case OrganSlot::LUNG:
            c.element_resistance = 0.7f;
            c.source_essence_cap = 2.0f;
            break;
        case OrganSlot::EYE:
            c.crit_rate_pct = 1.5f;
            c.source_essence_cap = 1.0f;
            break;
        case OrganSlot::NERVE:
            c.mana = 2;
            c.physical_attack = 0.5f;
            c.magic_power = 2.5f;
            c.move_speed_pct = 3.0f;
            c.attack_speed_pct = 3.0f;
            c.cast_speed_pct = 2.5f;
            c.dodge_rate_pct = 1.5f;
            c.source_essence_cap = 2.0f;
            break;
        case OrganSlot::SKIN:
            c.health = 3;
            c.physical_defense = 2.0f;
            c.element_resistance = 1.7f;
            c.source_essence_cap = 2.0f;
            break;
        default:
            break;
    }
    return c;
}

} // namespace science_and_theology::source_law
