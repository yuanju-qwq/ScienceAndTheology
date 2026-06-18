#pragma once

#include <cstdint>
#include <vector>

#include "core/magic/rune_def.hpp"
#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// DroppedOrganDef — template for a source organ dropped by a
// creature or aberration
// ============================================================
//
// When a creature dies, it may drop a source organ item.  A player
// can devour (consume) this organ to transform their own organ in
// the same slot into a bloodline organ — an imitation of the
// dropped organ's properties.
//
// If the source is an ABERRATION, the dropped organ mimics a
// sublimation path organ.  The resulting bloodline organ will be
// weaker (power_multiplier < 1.0) than the genuine sublimation
// organ.

struct DroppedOrganDef {
    const char* id = "";
    const char* title_key = "";

    // Which body slot this organ targets when devoured.
    OrganSlot target_slot = OrganSlot::HEART;

    // Origin category: creature or aberration.
    BloodlineSource source = BloodlineSource::NONE;

    // Identifier of the source creature (e.g. "rock_lizard").
    const char* source_creature_id = "";

    // Primary element of this organ.
    magic::RuneElement primary_element = magic::RuneElement::EARTH;

    // Secondary elements (optional).
    std::vector<magic::RuneElement> secondary_elements;

    // If source == ABERRATION, which sublimation path this organ
    // imitates.  Set to NONE for creature-sourced organs.
    SublimationPath imitated_path = SublimationPath::NONE;

    // Source reserve cost to devour this organ.
    int source_cost = 0;

    // Stability modifier applied on devouring.
    float stability_modifier = 0.0f;

    // Mutation modifier applied on devouring.
    float mutation_modifier = 0.0f;

    // Quality of the resulting bloodline organ.
    OrganQuality result_quality = OrganQuality::COMMON;

    // Power multiplier for the resulting bloodline organ.
    // Typically 0.5 ~ 0.7 (weaker than genuine sublimation).
    float result_power_multiplier = 0.6f;
};

} // namespace science_and_theology::source_law
