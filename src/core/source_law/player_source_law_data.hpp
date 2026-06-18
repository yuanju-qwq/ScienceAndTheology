#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/magic/rune_def.hpp"
#include "core/magic/mana_pool.hpp"
#include "dropped_organ_def.hpp"
#include "elixir_def.hpp"
#include "organ_def.hpp"
#include "source_law_constants.hpp"
#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// TransformRejection — ongoing rejection damage after organ transform
// ============================================================
//
// When an organ is transformed (via elixir or devour), the body
// enters a rejection period.  The player takes damage over time.
// If the player dies during rejection, the organ reverts to normal
// (transformation failed).  If the player survives, the organ
// transformation succeeds.
//
// Damage per tick formula:
//   damage = base_dpt * source_mult * stability_factor * mutation_factor
//            * defense_factor * element_factor

struct TransformRejection {
    // Which slot is being rejected.
    OrganSlot slot = OrganSlot::HEART;

    // How the organ was transformed.
    OrganTransformType source_type = OrganTransformType::NONE;

    // Remaining ticks of rejection damage.
    int ticks_remaining = 0;

    // Total ticks of this rejection (for progress reporting).
    int total_ticks = 0;

    // Pre-computed damage per tick (all factors applied at start).
    float damage_per_tick = 0.0f;

    // Is this rejection currently active?
    bool active = false;
};

// ============================================================
// CombatAttributes — derived combat stats computed from organs
// ============================================================
struct CombatAttributes {
    int health_max = kBaseHealth;
    int mana_max = kBaseMana;
    float physical_attack = kBasePhysicalAttack;
    float magic_power = kBaseMagicPower;
    float physical_defense = kBasePhysicalDefense;
    float element_resistance = kBaseElementResistance;
    float move_speed = kBaseMoveSpeed;
    float attack_speed = kBaseAttackSpeed;
    float cast_speed = kBaseCastSpeed;
    float crit_rate = kBaseCritRate;
    float crit_damage = kBaseCritDamage;
    float dodge_rate = kBaseDodgeRate;
    float health_regen = kBaseHealthRegen;
    float mana_regen = kBaseManaRegen;
};

// ============================================================
// PlayerSourceLawData — unified player source law data
// ============================================================
//
// This class holds all source law related data for one player:
// - Source reserve (面板数值, not HUD bar)
// - Stability, mutation, psionic level, mental load
// - Mana pool (mortal = 0 until initiated)
// - Fixed 7-slot organ array (always present, transform via sublimation)
// - Element affinities
// - Derived combat attributes
//
// Organ lifecycle:
// - All 7 organs exist from the start as normal organs (sublimation_degree=0).
// - Drinking an elixir consumes source reserve to transform an organ
//   (sublimation_degree increases, organ gains element/path/quality).
// - Devouring a dropped source organ consumes source reserve to
//   transform an organ into a bloodline organ (weaker imitation).
// - Purification methods revert organs to normal:
//   1. Mutation death → purify_all_organs() (all revert)
//   2. Purification ritual/potion → purify_organ(slot) (single revert)
//   3. Tuning potion → tune_organ(slot, degree_delta) (partial revert)

class PlayerSourceLawData {
public:
    PlayerSourceLawData() { reset(); }

    // --- Source reserve (面板数值) ---
    int source_current() const { return source_current_; }
    int source_max() const { return source_max_; }
    float source_regen() const { return source_regen_; }

    void set_source_max(int max_val);
    void add_source(int amount);
    bool consume_source(int amount);

    // --- Stability ---
    float stability() const { return stability_; }
    void set_stability(float val);
    void modify_stability(float delta);

    // --- Mutation ---
    float mutation() const { return mutation_; }
    void set_mutation(float val);
    void modify_mutation(float delta);

    // --- Psionic level ---
    int psionic_level() const { return psionic_level_; }
    void set_psionic_level(int level);

    // --- Mental load ---
    int mental_load() const { return mental_load_; }
    void set_mental_load(int load);
    void modify_mental_load(int delta);

    // --- Sublimation ---
    int sublimation_level() const { return sublimation_level_; }
    SublimationPath path() const { return path_; }
    void set_sublimation_level(int level);
    void set_path(SublimationPath path);

    // --- Is initiated (has at least one sublimated organ) ---
    bool is_initiated() const;

    // --- Source cost element modifier (元素损耗) ---
    // Compute the source cost multiplier based on the element
    // relation between the new organ's element and existing organs.
    // Returns the worst (highest) multiplier.
    float compute_source_cost_multiplier(magic::RuneElement new_element,
                                          OrganSlot exclude_slot) const;

    // --- Mana pool ---
    magic::ManaPool& mana_pool() { return mana_; }
    const magic::ManaPool& mana_pool() const { return mana_; }

    // Mortal rule: uninitiated players have zero personal mana
    void enforce_mortal_mana_rule();

    // --- Organs (fixed 7-slot array, always present) ---
    OrganArray& organs() { return organs_; }
    const OrganArray& organs() const { return organs_; }
    OrganData& get_organ(OrganSlot slot);
    const OrganData& get_organ(OrganSlot slot) const;

    // Transform a normal organ into a source law organ.
    // Consumes source reserve. Returns false if not enough source
    // or organ is already sublimated.
    bool transform_organ(OrganSlot slot, magic::RuneElement element,
                         SublimationPath path, int source_cost);

    // Purify a single organ: fully revert to normal.
    // Used by purification ritual/potion.
    void purify_organ(OrganSlot slot);

    // Purify all organs: fully revert all to normal.
    // Used when mutation death occurs.
    void purify_all_organs();

    // Tune a single organ: partially reduce sublimation degree.
    // If degree reaches 0, organ reverts to normal.
    // Used by tuning potion.
    void tune_organ(OrganSlot slot, int degree_delta);

    // Apply an elixir to this player.
    // Handles initiation, tuning, and purification elixirs.
    // Returns true if the elixir was successfully applied.
    bool apply_elixir(const ElixirRecipe& recipe);

    // --- Bloodline (devour dropped organ) ---

    // Devour a dropped source organ, transforming the player's organ
    // in the target slot into a bloodline organ.
    // Consumes source reserve. Returns false if not enough source,
    // organ is already transformed, or slot mismatch.
    bool devour_organ(const DroppedOrganDef& dropped);

    // --- Transform rejection (排异掉血) ---

    // Is the player currently in a rejection period?
    bool is_rejecting() const;

    // Get current rejection state (null if not rejecting).
    const TransformRejection* rejection() const;

    // Get rejection progress as a ratio [0.0, 1.0]. 0 = just started, 1 = done.
    float rejection_progress() const;

    // Called when the player dies during rejection.
    // Reverts the rejecting organ to normal (transformation failed).
    void handle_rejection_death();

    // Called when the player survives the full rejection period.
    // The organ transformation is confirmed as successful.
    void complete_rejection();

    // --- Element affinities ---
    const std::unordered_map<magic::RuneElement, int>& affinities() const {
        return affinities_;
    }
    void set_affinity(magic::RuneElement element, int value);
    int get_affinity(magic::RuneElement element) const;

    // --- Network affinity report ---
    OrganNetworkAffinityReport compute_network_report() const;

    // --- Derived combat attributes ---
    CombatAttributes compute_combat_attributes() const;

    // --- Derived source essence cap ---
    // Computes the total source essence capacity from all sublimated organs.
    // Each organ contributes source_essence_cap * level * quality_multiplier * power_multiplier.
    float compute_source_essence_cap() const;

    // --- Tick (called each game tick) ---
    void tick();

    // --- Reset to mortal defaults ---
    void reset();

    // --- Serialization helpers ---
    struct SerializedOrganData {
        int slot = 0;
        int sublimation_degree = 0;
        int path_id = 0;
        int transform_type = 0;
        float power_multiplier = 1.0f;
        int bloodline_source = 0;
        int source_required = 0;
        int source_paid = 0;
        int primary_element = 0;
        int quality = 0;
        int level = 0;
        float stability_modifier = 0.0f;
        float mutation_risk = 0.0f;
        int psionic_modifier = 0;
        int mental_load_modifier = 0;
    };

    struct SerializedData {
        int sublimation_level = 0;
        int path_id = 0;
        int source_current = 0;
        int source_max = 0;
        float source_regen = 0.0f;
        int mana_current = 0;
        int mana_max = 0;
        float mana_regen = 0.0f;
        float stability = kDefaultStability;
        float mutation = kDefaultMutation;
        int psionic_level = 0;
        int mental_load = 0;
        std::array<SerializedOrganData, kOrganSlotCount> organs = {};
        int affinity_count = 0;

        // Rejection state
        int rejection_slot = 0;
        int rejection_source_type = 0;
        int rejection_ticks_remaining = 0;
        int rejection_total_ticks = 0;
        float rejection_damage_per_tick = 0.0f;
        int rejection_active = 0;
    };

    SerializedData to_serialized() const;
    void from_serialized(const SerializedData& data);

private:
    int sublimation_level_ = kMortalSublimationLevel;
    SublimationPath path_ = SublimationPath::NONE;

    int source_current_ = 0;
    int source_max_ = kDefaultSourceMax;
    float source_regen_ = kDefaultSourceRegen;

    magic::ManaPool mana_;

    float stability_ = kDefaultStability;
    float mutation_ = kDefaultMutation;
    int psionic_level_ = kDefaultPsionicLevel;
    int mental_load_ = kDefaultMentalLoad;

    // Fixed 7-slot organ array, always initialized.
    OrganArray organs_;

    std::unordered_map<magic::RuneElement, int> affinities_;

    // Transform rejection state (at most one active at a time).
    TransformRejection rejection_;

    void recalculate_mana_from_organs();

    // Start a rejection period for the given slot and transform type.
    // Computes the damage per tick based on current stats.
    void start_rejection(OrganSlot slot, OrganTransformType source_type);

    // Process one tick of rejection. Returns damage dealt this tick.
    // Returns 0.0f if not rejecting.
    float tick_rejection();
};

} // namespace science_and_theology::source_law
