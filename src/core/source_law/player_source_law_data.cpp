#include "player_source_law_data.hpp"

#include <algorithm>
#include <cmath>

#include "mutation_system.hpp"

namespace science_and_theology::source_law {

// ============================================================
// Source reserve
// ============================================================

void PlayerSourceLawData::set_source_max(int max_val) {
    source_max_ = max_val;
    if (source_current_ > source_max_) {
        source_current_ = source_max_;
    }
}

void PlayerSourceLawData::add_source(int amount) {
    source_current_ += amount;

    // Auto-complete partial sublimation organs: when source is added,
    // any sublimation (elixir) organ that is partially transformed
    // will consume source reserve until fully paid.
    // Bloodline (devour) organs do NOT auto-complete; they require
    // re-devouring the same organ.
    for (int i = 0; i < kOrganSlotCount && source_current_ > 0; ++i) {
        auto& organ = organs_[i];
        if (!organ.is_sublimated()) continue;
        if (!organ.is_partial()) continue;
        if (organ.transform_type != OrganTransformType::SUBLIMATION) continue;

        int remaining = organ.source_required - organ.source_paid;
        int pay = std::min(source_current_, remaining);
        source_current_ -= pay;
        organ.source_paid += pay;
    }

    if (source_current_ > source_max_) {
        source_current_ = source_max_;
    }
}

bool PlayerSourceLawData::consume_source(int amount) {
    if (source_current_ < amount) return false;
    source_current_ -= amount;
    return true;
}

// ============================================================
// Stability
// ============================================================

void PlayerSourceLawData::set_stability(float val) {
    stability_ = std::clamp(val, kMinStability, kMaxStability);
}

void PlayerSourceLawData::modify_stability(float delta) {
    set_stability(stability_ + delta);
}

// ============================================================
// Mutation
// ============================================================

void PlayerSourceLawData::set_mutation(float val) {
    mutation_ = std::clamp(val, 0.0f, kMaxMutation);
}

void PlayerSourceLawData::modify_mutation(float delta) {
    set_mutation(mutation_ + delta);
}

// ============================================================
// Psionic level
// ============================================================

void PlayerSourceLawData::set_psionic_level(int level) {
    psionic_level_ = std::max(0, std::min(level, 5));
}

// ============================================================
// Mental load
// ============================================================

void PlayerSourceLawData::set_mental_load(int load) {
    mental_load_ = std::max(0, load);
}

void PlayerSourceLawData::modify_mental_load(int delta) {
    set_mental_load(mental_load_ + delta);
}

// ============================================================
// Sublimation
// ============================================================

void PlayerSourceLawData::set_sublimation_level(int level) {
    sublimation_level_ = std::max(0, level);
}

void PlayerSourceLawData::set_path(SublimationPath path) {
    path_ = path;
}

// ============================================================
// Initiation check
// ============================================================

bool PlayerSourceLawData::is_initiated() const {
    if (sublimation_level_ > 0) return true;
    for (const auto& organ : organs_) {
        if (organ.is_sublimated()) return true;
    }
    return false;
}

float PlayerSourceLawData::compute_source_cost_multiplier(
    magic::RuneElement new_element,
    OrganSlot exclude_slot) const {
    float worst = kSourceCostElementNeutral;

    for (int i = 0; i < kOrganSlotCount; ++i) {
        if (i == static_cast<int>(exclude_slot)) continue;
        if (!organs_[i].is_sublimated()) continue;

        auto rel = get_element_relation(new_element, organs_[i].primary_element);
        float factor = kSourceCostElementNeutral;
        switch (rel) {
            case ElementRelation::SAME:
                factor = kSourceCostElementSame;
                break;
            case ElementRelation::GENERATING:
                factor = kSourceCostElementGenerating;
                break;
            case ElementRelation::NEUTRAL:
                factor = kSourceCostElementNeutral;
                break;
            case ElementRelation::CONFLICTING:
                factor = kSourceCostElementConflicting;
                break;
            case ElementRelation::SEVERE_CONFLICT:
                factor = kSourceCostElementSevereConflict;
                break;
            default:
                break;
        }
        if (factor > worst) worst = factor;
    }

    return worst;
}

// ============================================================
// Mortal mana rule
// ============================================================

void PlayerSourceLawData::enforce_mortal_mana_rule() {
    if (!is_initiated()) {
        mana_.current_mana = 0;
        mana_.max_mana = kMortalMaxMana;
        mana_.regen_rate = kMortalManaRegen;
    }
}

// ============================================================
// Organs
// ============================================================

OrganData& PlayerSourceLawData::get_organ(OrganSlot slot) {
    return organs_[static_cast<int>(slot)];
}

const OrganData& PlayerSourceLawData::get_organ(OrganSlot slot) const {
    return organs_[static_cast<int>(slot)];
}

bool PlayerSourceLawData::transform_organ(OrganSlot slot,
                                          magic::RuneElement element,
                                          SublimationPath path,
                                          int source_cost) {
    auto& organ = get_organ(slot);

    // Cannot transform an already sublimated organ (including partial).
    if (organ.is_sublimated()) return false;

    // Apply element cost modifier: conflicting elements increase cost.
    float elem_mult = compute_source_cost_multiplier(element, slot);
    int adjusted_cost = static_cast<int>(source_cost * elem_mult);
    if (adjusted_cost < source_cost) adjusted_cost = source_cost;

    // Consume as much source reserve as possible (partial allowed).
    int paid = std::min(source_current_, adjusted_cost);
    if (paid <= 0) return false;

    source_current_ -= paid;

    organ.sublimation_degree = 1;
    organ.primary_element = element;
    organ.path_id = path;
    organ.transform_type = OrganTransformType::SUBLIMATION;
    organ.power_multiplier = 1.0f;
    organ.bloodline_source = BloodlineSource::NONE;
    organ.source_creature_id = "";
    organ.source_required = adjusted_cost;
    organ.source_paid = paid;
    organ.quality = OrganQuality::COMMON;
    organ.level = 0;

    // Start rejection period (排异掉血).
    // Rejection damage is scaled by source ratio in start_rejection.
    start_rejection(slot, OrganTransformType::SUBLIMATION);

    recalculate_mana_from_organs();
    return true;
}

void PlayerSourceLawData::purify_organ(OrganSlot slot) {
    auto& organ = get_organ(slot);
    organ.reset_to_normal();
    recalculate_mana_from_organs();
}

void PlayerSourceLawData::purify_all_organs() {
    for (auto& organ : organs_) {
        organ.reset_to_normal();
    }
    recalculate_mana_from_organs();
}

void PlayerSourceLawData::tune_organ(OrganSlot slot, int degree_delta) {
    auto& organ = get_organ(slot);
    if (!organ.is_sublimated()) return;

    organ.sublimation_degree = std::max(0, organ.sublimation_degree - degree_delta);

    if (organ.sublimation_degree <= 0) {
        organ.reset_to_normal();
    }

    recalculate_mana_from_organs();
}

bool PlayerSourceLawData::apply_elixir(const ElixirRecipe& recipe) {
    switch (recipe.type) {
        case ElixirType::INITIATION: {
            OrganSlot slot = recipe.target_slot;
            if (slot >= OrganSlot::COUNT) return false;
            if (!transform_organ(slot, recipe.primary_element,
                                 recipe.target_path, recipe.source_cost)) {
                return false;
            }
            // Set sublimation level and path on first initiation.
            if (sublimation_level_ == 0) {
                sublimation_level_ = 1;
                path_ = recipe.target_path;
            }
            modify_stability(recipe.stability_modifier);
            modify_mutation(recipe.mutation_modifier);
            return true;
        }

        case ElixirType::TUNING: {
            // Tuning elixirs target a specific slot or all sublimated organs.
            if (recipe.target_slot < OrganSlot::COUNT) {
                tune_organ(recipe.target_slot, recipe.tuning_degree);
            } else {
                for (int i = 0; i < kOrganSlotCount; ++i) {
                    tune_organ(static_cast<OrganSlot>(i), recipe.tuning_degree);
                }
            }
            modify_stability(recipe.stability_modifier);
            modify_mutation(recipe.mutation_modifier);
            return true;
        }

        case ElixirType::PURIFICATION: {
            if (recipe.target_slot < OrganSlot::COUNT) {
                purify_organ(recipe.target_slot);
            } else {
                purify_all_organs();
            }
            modify_stability(recipe.stability_modifier);
            modify_mutation(recipe.mutation_modifier);
            return true;
        }

        default:
            return false;
    }
}

// ============================================================
// Bloodline (devour dropped organ)
// ============================================================

bool PlayerSourceLawData::devour_organ(const DroppedOrganDef& dropped) {
    OrganSlot slot = dropped.target_slot;

    // Validate slot.
    if (slot >= OrganSlot::COUNT) return false;

    auto& organ = get_organ(slot);

    // Case 1: Re-devour the same bloodline organ (resume partial transform).
    if (organ.is_sublimated()
        && organ.is_bloodline()
        && organ.is_partial()
        && organ.matches_bloodline_source(dropped.source_creature_id, slot)) {
        // Resume: pay as much as possible toward remaining cost.
        int remaining = organ.source_required - organ.source_paid;
        int paid = std::min(source_current_, remaining);
        if (paid <= 0) return false;

        source_current_ -= paid;
        organ.source_paid += paid;

        // Re-trigger rejection for the resumed portion.
        start_rejection(slot, OrganTransformType::BLOODLINE);

        recalculate_mana_from_organs();
        return true;
    }

    // Case 2: Cannot transform an already sublimated organ (non-matching).
    if (organ.is_sublimated()) return false;

    // Case 3: Fresh devour — consume as much source as possible.
    // Apply element cost modifier.
    float elem_mult = compute_source_cost_multiplier(
        dropped.primary_element, slot);
    int adjusted_cost = static_cast<int>(dropped.source_cost * elem_mult);
    if (adjusted_cost < dropped.source_cost) adjusted_cost = dropped.source_cost;

    int paid = std::min(source_current_, adjusted_cost);
    if (paid <= 0) return false;

    source_current_ -= paid;

    // Transform the organ into a bloodline organ.
    organ.sublimation_degree = 1;
    organ.transform_type = OrganTransformType::BLOODLINE;
    organ.power_multiplier = dropped.result_power_multiplier;
    organ.bloodline_source = dropped.source;
    organ.source_creature_id = dropped.source_creature_id;
    organ.primary_element = dropped.primary_element;
    organ.secondary_elements = dropped.secondary_elements;
    organ.path_id = dropped.imitated_path;
    organ.source_required = adjusted_cost;
    organ.source_paid = paid;
    organ.quality = dropped.result_quality;
    organ.level = 0;

    // Apply stability and mutation modifiers from the devoured organ.
    modify_stability(dropped.stability_modifier);
    modify_mutation(dropped.mutation_modifier);

    // Bloodline organs count as initiation.
    if (sublimation_level_ == 0) {
        sublimation_level_ = 1;
        // Bloodline path is set to NONE unless the organ imitates a
        // sublimation path (from aberration source).
        if (dropped.imitated_path != SublimationPath::NONE) {
            path_ = dropped.imitated_path;
        }
    }

    // Start rejection period (排异掉血, bloodline is more painful).
    start_rejection(slot, OrganTransformType::BLOODLINE);

    recalculate_mana_from_organs();
    return true;
}

// ============================================================
// Element affinities
// ============================================================

void PlayerSourceLawData::set_affinity(magic::RuneElement element, int value) {
    affinities_[element] = value;
}

int PlayerSourceLawData::get_affinity(magic::RuneElement element) const {
    auto it = affinities_.find(element);
    if (it != affinities_.end()) return it->second;
    return 0;
}

// ============================================================
// Network affinity report
// ============================================================

OrganNetworkAffinityReport PlayerSourceLawData::compute_network_report() const {
    OrganNetworkAffinityReport report;

    // Step 1: Count element weights from all sublimated organs
    for (const auto& organ : organs_) {
        if (!organ.is_sublimated()) continue;
        report.element_weights[organ.primary_element] += organ.level + 1;
        for (auto elem : organ.secondary_elements) {
            report.element_weights[elem] += 1;
        }
    }

    // Step 2: Compute pairwise relations between sublimated organs
    float stability_mod = 0.0f;
    float mutation_mod = 0.0f;

    for (int i = 0; i < kOrganSlotCount; ++i) {
        if (!organs_[i].is_sublimated()) continue;
        for (int j = i + 1; j < kOrganSlotCount; ++j) {
            if (!organs_[j].is_sublimated()) continue;

            auto rel = get_element_relation(
                organs_[i].primary_element,
                organs_[j].primary_element);

            switch (rel) {
                case ElementRelation::SAME:
                    stability_mod += 2.0f;
                    mutation_mod -= 0.5f;
                    break;
                case ElementRelation::GENERATING:
                    stability_mod += 1.0f;
                    mutation_mod -= 0.3f;
                    break;
                case ElementRelation::CONFLICTING:
                    stability_mod -= 1.5f;
                    mutation_mod += 1.0f;
                    break;
                case ElementRelation::SEVERE_CONFLICT:
                    stability_mod -= 5.0f;
                    mutation_mod += 3.0f;
                    report.has_severe_conflict = true;
                    report.severe_conflict_pairs.push_back({
                        organs_[i].slot, organs_[j].slot
                    });
                    break;
                case ElementRelation::NEUTRAL:
                default:
                    break;
            }
        }
    }

    report.element_stability_modifier = stability_mod;
    report.element_mutation_modifier = mutation_mod;
    return report;
}

// ============================================================
// Derived combat attributes
// ============================================================

CombatAttributes PlayerSourceLawData::compute_combat_attributes() const {
    CombatAttributes attrs;

    for (const auto& organ : organs_) {
        if (!organ.is_sublimated()) continue;

        float qmult = organ_quality_multiplier(organ.quality);
        // Bloodline organs are weaker: apply power_multiplier.
        // Partial transforms: scale by source_paid / source_required.
        float pmult = organ.power_multiplier * organ.source_ratio();
        auto contrib = get_organ_stat_contribution(organ.slot);
        int lv = organ.level;

        attrs.health_max += static_cast<int>(contrib.health * lv * qmult * pmult);
        attrs.mana_max += static_cast<int>(contrib.mana * lv * qmult * pmult);
        attrs.physical_attack += contrib.physical_attack * lv * qmult * pmult;
        attrs.magic_power += contrib.magic_power * lv * qmult * pmult;
        attrs.physical_defense += contrib.physical_defense * lv * qmult * pmult;
        attrs.element_resistance += contrib.element_resistance * lv * qmult * pmult;
        attrs.move_speed *= (1.0f + contrib.move_speed_pct * lv * qmult * pmult / 100.0f);
        attrs.attack_speed *= (1.0f + contrib.attack_speed_pct * lv * qmult * pmult / 100.0f);
        attrs.cast_speed *= (1.0f + contrib.cast_speed_pct * lv * qmult * pmult / 100.0f);
        attrs.crit_rate += contrib.crit_rate_pct * lv * qmult * pmult / 100.0f;
        attrs.crit_damage += contrib.crit_damage_pct * lv * qmult * pmult / 100.0f;
        attrs.dodge_rate += contrib.dodge_rate_pct * lv * qmult * pmult / 100.0f;
        attrs.health_regen += contrib.health_regen * lv * qmult * pmult;
        attrs.mana_regen += contrib.mana_regen * lv * qmult * pmult;
    }

    return attrs;
}

// ============================================================
// Derived source essence cap
// ============================================================

float PlayerSourceLawData::compute_source_essence_cap() const {
    float cap = 0.0f;
    for (const auto& organ : organs_) {
        if (!organ.is_sublimated()) continue;

        float qmult = organ_quality_multiplier(organ.quality);
        float pmult = organ.power_multiplier;
        auto contrib = get_organ_stat_contribution(organ.slot);
        int lv = organ.level;

        cap += contrib.source_essence_cap * lv * qmult * pmult;
    }
    return cap;
}

// ============================================================
// Tick
// ============================================================

void PlayerSourceLawData::tick() {
    // Source reserve regeneration
    if (source_current_ < source_max_ && source_regen_ > 0.0f) {
        source_current_ += static_cast<int>(source_regen_);
        if (source_current_ > source_max_) {
            source_current_ = source_max_;
        }
    }

    // Mana pool tick (only if initiated)
    if (is_initiated()) {
        mana_.tick();

        // Stability/mutation per-tick logic
        MutationSystem::tick_stability_mutation(*this);

        // Check for mutation death
        if (MutationSystem::should_trigger_mutation_death(*this)) {
            MutationSystem::handle_mutation_death(*this);
        }
    }

    // Transform rejection tick (排异掉血)
    // Note: the actual health deduction is handled by the game layer
    // (e.g. GDScript), which calls tick_rejection() and applies the
    // returned damage to the player's health component.
    // If health reaches 0 during rejection, the game layer should
    // call handle_rejection_death() to revert the organ.
    tick_rejection();
}

// ============================================================
// Reset
// ============================================================

void PlayerSourceLawData::reset() {
    sublimation_level_ = kMortalSublimationLevel;
    path_ = SublimationPath::NONE;

    source_current_ = 0;
    source_max_ = kDefaultSourceMax;
    source_regen_ = kDefaultSourceRegen;

    mana_.current_mana = 0;
    mana_.max_mana = kMortalMaxMana;
    mana_.regen_rate = kMortalManaRegen;

    stability_ = kDefaultStability;
    mutation_ = kDefaultMutation;
    psionic_level_ = kDefaultPsionicLevel;
    mental_load_ = kDefaultMentalLoad;

    // Initialize all organ slots to normal organs.
    for (int i = 0; i < kOrganSlotCount; ++i) {
        organs_[i] = OrganData{};
        organs_[i].slot = static_cast<OrganSlot>(i);
    }

    affinities_.clear();

    // Reset rejection state.
    rejection_ = TransformRejection{};
}

// ============================================================
// Transform rejection (排异掉血)
// ============================================================

bool PlayerSourceLawData::is_rejecting() const {
    return rejection_.active;
}

const TransformRejection* PlayerSourceLawData::rejection() const {
    if (!rejection_.active) return nullptr;
    return &rejection_;
}

float PlayerSourceLawData::rejection_progress() const {
    if (!rejection_.active || rejection_.total_ticks <= 0) return 0.0f;
    int elapsed = rejection_.total_ticks - rejection_.ticks_remaining;
    return static_cast<float>(elapsed) / static_cast<float>(rejection_.total_ticks);
}

void PlayerSourceLawData::handle_rejection_death() {
    if (!rejection_.active) return;

    // Revert the rejecting organ to normal (transformation failed).
    purify_organ(rejection_.slot);

    // Clear rejection state.
    rejection_ = TransformRejection{};
}

void PlayerSourceLawData::complete_rejection() {
    if (!rejection_.active) return;

    // Transformation succeeded. Just clear the rejection state.
    rejection_ = TransformRejection{};
}

void PlayerSourceLawData::start_rejection(OrganSlot slot,
                                           OrganTransformType source_type) {
    // Compute rejection damage per tick.
    float source_mult = (source_type == OrganTransformType::BLOODLINE)
                            ? kRejectionBloodlineMult
                            : kRejectionSublimationMult;

    // Stability factor: low stability = more damage.
    // Range: 0.0 (stability=100) to 1.0 (stability=0).
    float stability_factor = (kMaxStability - stability_) / kMaxStability;

    // Mutation factor: high mutation = more damage.
    // Range: 1.0 (mutation=0) to 2.0 (mutation=100).
    float mutation_factor = 1.0f + mutation_ / kMaxMutation;

    // Defense factor: high physical defense = less damage.
    // Uses physical_defense as constitution proxy.
    auto attrs = compute_combat_attributes();
    float defense_factor = 1.0f / (1.0f + attrs.physical_defense / 10.0f);

    // Element factor: worst relation to existing sublimated organs.
    float element_factor = kRejectionElementNeutral;
    auto& new_organ = get_organ(slot);
    for (int i = 0; i < kOrganSlotCount; ++i) {
        if (i == static_cast<int>(slot)) continue;
        if (!organs_[i].is_sublimated()) continue;

        auto rel = get_element_relation(
            new_organ.primary_element,
            organs_[i].primary_element);

        float factor = kRejectionElementNeutral;
        switch (rel) {
            case ElementRelation::SAME:
                factor = kRejectionElementSame;
                break;
            case ElementRelation::GENERATING:
                factor = kRejectionElementGenerating;
                break;
            case ElementRelation::NEUTRAL:
                factor = kRejectionElementNeutral;
                break;
            case ElementRelation::CONFLICTING:
                factor = kRejectionElementConflicting;
                break;
            case ElementRelation::SEVERE_CONFLICT:
                factor = kRejectionElementSevereConflict;
                break;
            default:
                break;
        }
        // Use the worst (highest) factor.
        if (factor > element_factor) {
            element_factor = factor;
        }
    }

    // Compute final damage per tick.
    // Partial transforms: scale rejection damage by source ratio.
    float source_ratio = new_organ.source_ratio();
    float dpt = kRejectionBaseDpt * source_mult * stability_factor
                * mutation_factor * defense_factor * element_factor
                * source_ratio;

    // Compute duration.
    // Partial transforms: duration is also scaled by source ratio.
    int base_duration = (source_type == OrganTransformType::BLOODLINE)
                            ? kRejectionBloodlineDuration
                            : kRejectionSublimationDuration;
    int duration = static_cast<int>(base_duration * source_ratio);
    if (duration < 1) duration = 1;

    rejection_.slot = slot;
    rejection_.source_type = source_type;
    rejection_.ticks_remaining = duration;
    rejection_.total_ticks = duration;
    rejection_.damage_per_tick = dpt;
    rejection_.active = true;
}

float PlayerSourceLawData::tick_rejection() {
    if (!rejection_.active) return 0.0f;

    float damage = rejection_.damage_per_tick;

    rejection_.ticks_remaining--;
    if (rejection_.ticks_remaining <= 0) {
        // Rejection period ended. Transformation is successful.
        complete_rejection();
    }

    return damage;
}

// ============================================================
// Internal: recalculate mana from organs
// ============================================================

void PlayerSourceLawData::recalculate_mana_from_organs() {
    if (!is_initiated()) {
        enforce_mortal_mana_rule();
        return;
    }

    auto attrs = compute_combat_attributes();
    int old_max = mana_.max_mana;
    mana_.max_mana = attrs.mana_max;
    mana_.regen_rate = attrs.mana_regen;

    // If mana max increased, keep current proportional
    if (old_max > 0 && mana_.max_mana > old_max) {
        float ratio = static_cast<float>(mana_.current_mana) / static_cast<float>(old_max);
        mana_.current_mana = static_cast<int>(ratio * mana_.max_mana);
    }
    if (mana_.current_mana > mana_.max_mana) {
        mana_.current_mana = mana_.max_mana;
    }
}

// ============================================================
// Serialization
// ============================================================

PlayerSourceLawData::SerializedData PlayerSourceLawData::to_serialized() const {
    SerializedData data;
    data.sublimation_level = sublimation_level_;
    data.path_id = static_cast<int>(path_);
    data.source_current = source_current_;
    data.source_max = source_max_;
    data.source_regen = source_regen_;
    data.mana_current = mana_.current_mana;
    data.mana_max = mana_.max_mana;
    data.mana_regen = mana_.regen_rate;
    data.stability = stability_;
    data.mutation = mutation_;
    data.psionic_level = psionic_level_;
    data.mental_load = mental_load_;
    data.affinity_count = static_cast<int>(affinities_.size());

    // Rejection state
    data.rejection_slot = static_cast<int>(rejection_.slot);
    data.rejection_source_type = static_cast<int>(rejection_.source_type);
    data.rejection_ticks_remaining = rejection_.ticks_remaining;
    data.rejection_total_ticks = rejection_.total_ticks;
    data.rejection_damage_per_tick = rejection_.damage_per_tick;
    data.rejection_active = rejection_.active ? 1 : 0;

    for (int i = 0; i < kOrganSlotCount; ++i) {
        auto& so = data.organs[i];
        const auto& organ = organs_[i];
        so.slot = static_cast<int>(organ.slot);
        so.sublimation_degree = organ.sublimation_degree;
        so.path_id = static_cast<int>(organ.path_id);
        so.transform_type = static_cast<int>(organ.transform_type);
        so.power_multiplier = organ.power_multiplier;
        so.bloodline_source = static_cast<int>(organ.bloodline_source);
        so.source_required = organ.source_required;
        so.source_paid = organ.source_paid;
        so.primary_element = static_cast<int>(organ.primary_element);
        so.quality = static_cast<int>(organ.quality);
        so.level = organ.level;
        so.stability_modifier = organ.stability_modifier;
        so.mutation_risk = organ.mutation_risk;
        so.psionic_modifier = organ.psionic_modifier;
        so.mental_load_modifier = organ.mental_load_modifier;
    }

    return data;
}

void PlayerSourceLawData::from_serialized(const SerializedData& data) {
    sublimation_level_ = data.sublimation_level;
    path_ = static_cast<SublimationPath>(data.path_id);
    source_current_ = data.source_current;
    source_max_ = data.source_max;
    source_regen_ = data.source_regen;
    mana_.current_mana = data.mana_current;
    mana_.max_mana = data.mana_max;
    mana_.regen_rate = data.mana_regen;
    stability_ = data.stability;
    mutation_ = data.mutation;
    psionic_level_ = data.psionic_level;
    mental_load_ = data.mental_load;

    for (int i = 0; i < kOrganSlotCount; ++i) {
        const auto& so = data.organs[i];
        auto& organ = organs_[i];
        organ.slot = static_cast<OrganSlot>(so.slot);
        organ.sublimation_degree = so.sublimation_degree;
        organ.path_id = static_cast<SublimationPath>(so.path_id);
        organ.transform_type = static_cast<OrganTransformType>(so.transform_type);
        organ.power_multiplier = so.power_multiplier;
        organ.bloodline_source = static_cast<BloodlineSource>(so.bloodline_source);
        organ.source_required = so.source_required;
        organ.source_paid = so.source_paid;
        organ.primary_element = static_cast<magic::RuneElement>(so.primary_element);
        organ.quality = static_cast<OrganQuality>(so.quality);
        organ.level = so.level;
        organ.stability_modifier = so.stability_modifier;
        organ.mutation_risk = so.mutation_risk;
        organ.psionic_modifier = so.psionic_modifier;
        organ.mental_load_modifier = so.mental_load_modifier;
    }

    // Note: affinities need separate deserialization
    // in V0.2+ when full save/load is implemented

    // Rejection state
    rejection_.slot = static_cast<OrganSlot>(data.rejection_slot);
    rejection_.source_type = static_cast<OrganTransformType>(data.rejection_source_type);
    rejection_.ticks_remaining = data.rejection_ticks_remaining;
    rejection_.total_ticks = data.rejection_total_ticks;
    rejection_.damage_per_tick = data.rejection_damage_per_tick;
    rejection_.active = data.rejection_active != 0;
}

} // namespace science_and_theology::source_law
