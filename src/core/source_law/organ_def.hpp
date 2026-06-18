#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/magic/rune_def.hpp"
#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// OrganData — data for a single body organ
// ============================================================
//
// Organs are always present in the body (7 slots). They start as
// normal (sublimation_degree == 0). Through source law sublimation
// (e.g. drinking an elixir), the body's source reserve transforms
// a normal organ into a source law organ (sublimation_degree > 0).
//
// Purification methods:
// - Mutation death: all organs revert to normal (purify_all)
// - Purification ritual/potion: single organ reverts to normal
// - Tuning potion: partially reduce sublimation degree

struct OrganData {
    OrganSlot slot = OrganSlot::HEART;

    // Sublimation degree: 0 = normal organ, >0 = source law organ.
    // Higher degree means deeper transformation.
    int sublimation_degree = 0;

    // The sublimation path that guided this organ's transformation.
    SublimationPath path_id = SublimationPath::NONE;

    // How this organ was transformed (sublimation or bloodline).
    OrganTransformType transform_type = OrganTransformType::NONE;

    // Power multiplier: sublimation organs = 1.0, bloodline organs < 1.0.
    // For partial transforms, this is scaled by source_paid / source_required.
    float power_multiplier = 1.0f;

    // Bloodline source: where the devoured organ came from.
    // Only meaningful when transform_type == BLOODLINE.
    BloodlineSource bloodline_source = BloodlineSource::NONE;

    // Source creature identifier: which creature the devoured organ came from.
    // Only meaningful when transform_type == BLOODLINE.
    // E.g. "rock_lizard", "sand_armor_aberrant".
    const char* source_creature_id = "";

    // --- Partial transform (部分转化) ---
    // Total source reserve required for full transformation.
    int source_required = 0;

    // Source reserve already paid toward transformation.
    int source_paid = 0;

    // Primary element acquired through sublimation.
    // Only meaningful when sublimation_degree > 0.
    magic::RuneElement primary_element = magic::RuneElement::FIRE;
    std::vector<magic::RuneElement> secondary_elements;

    // Quality of the source law transformation.
    OrganQuality quality = OrganQuality::COMMON;

    // Growth level of the source law organ (0 = just transformed).
    int level = 0;

    // Modifiers applied by this organ to the network.
    float stability_modifier = 0.0f;
    float mutation_risk = 0.0f;
    int psionic_modifier = 0;
    int mental_load_modifier = 0;

    // Check if this organ has been sublimated (is a source law organ).
    bool is_sublimated() const {
        return sublimation_degree > 0;
    }

    // Check if this organ is a bloodline organ.
    bool is_bloodline() const {
        return transform_type == OrganTransformType::BLOODLINE;
    }

    // Check if this organ is partially transformed (not fully paid).
    bool is_partial() const {
        return sublimation_degree > 0
            && source_required > 0
            && source_paid < source_required;
    }

    // Get the effective ratio of source paid (0.0 ~ 1.0).
    float source_ratio() const {
        if (source_required <= 0) return 1.0f;
        return static_cast<float>(source_paid) / static_cast<float>(source_required);
    }

    // Check if this organ matches the given dropped organ for
    // re-devour (same slot, same source creature).
    bool matches_bloodline_source(const char* creature_id,
                                   OrganSlot target_slot) const {
        return transform_type == OrganTransformType::BLOODLINE
            && slot == target_slot
            && source_creature_id != nullptr
            && creature_id != nullptr
            && std::string(source_creature_id) == std::string(creature_id);
    }

    // Reset this organ to normal (remove all source law properties).
    void reset_to_normal() {
        sublimation_degree = 0;
        path_id = SublimationPath::NONE;
        transform_type = OrganTransformType::NONE;
        power_multiplier = 1.0f;
        bloodline_source = BloodlineSource::NONE;
        source_creature_id = "";
        source_required = 0;
        source_paid = 0;
        primary_element = magic::RuneElement::FIRE;
        secondary_elements.clear();
        quality = OrganQuality::COMMON;
        level = 0;
        stability_modifier = 0.0f;
        mutation_risk = 0.0f;
        psionic_modifier = 0;
        mental_load_modifier = 0;
    }
};

// ============================================================
// OrganNetworkAffinityReport — computed from all sublimated organs
// ============================================================
struct OrganNetworkAffinityReport {
    float element_stability_modifier = 0.0f;
    float element_mutation_modifier = 0.0f;
    bool has_severe_conflict = false;
    std::vector<std::pair<OrganSlot, OrganSlot>> severe_conflict_pairs;
    std::unordered_map<magic::RuneElement, int> element_weights;
};

// Fixed-size organ array: one entry per OrganSlot.
using OrganArray = std::array<OrganData, kOrganSlotCount>;

} // namespace science_and_theology::source_law
