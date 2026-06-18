#pragma once

#include <cstdint>

#include "core/magic/rune_def.hpp"

namespace science_and_theology::source_law {

// ============================================================
// OrganSlot — body slot where an organ is implanted
// ============================================================
enum class OrganSlot : uint8_t {
    HEART = 0,
    BONE,
    BLOOD,
    LUNG,
    EYE,
    NERVE,
    SKIN,
    COUNT
};

inline const char* organ_slot_name(OrganSlot slot) {
    switch (slot) {
        case OrganSlot::HEART: return "heart";
        case OrganSlot::BONE:  return "bone";
        case OrganSlot::BLOOD: return "blood";
        case OrganSlot::LUNG:  return "lung";
        case OrganSlot::EYE:   return "eye";
        case OrganSlot::NERVE: return "nerve";
        case OrganSlot::SKIN:  return "skin";
        default:               return "unknown";
    }
}

inline constexpr int kOrganSlotCount = static_cast<int>(OrganSlot::COUNT);

// ============================================================
// ElementRelation — five-phase relationship between two elements
// ============================================================
//
// SAME:          identical element
// GENERATING:    the first element generates the second (相生)
// NEUTRAL:       no special relationship
// CONFLICTING:   the first element conflicts with the second (相克)
// SEVERE_CONFLICT: strong opposition (强冲突)

enum class ElementRelation : uint8_t {
    SAME = 0,
    GENERATING,
    NEUTRAL,
    CONFLICTING,
    SEVERE_CONFLICT
};

// ============================================================
// OrganQuality — quality tier of an organ, affects stat multiplier
// ============================================================
enum class OrganQuality : uint8_t {
    FLAWED = 0,
    COMMON,
    GOOD,
    PURE,
    ANCIENT,
    PERFECT,
    COUNT
};

inline const char* organ_quality_name(OrganQuality quality) {
    switch (quality) {
        case OrganQuality::FLAWED:   return "flawed";
        case OrganQuality::COMMON:   return "common";
        case OrganQuality::GOOD:     return "good";
        case OrganQuality::PURE:     return "pure";
        case OrganQuality::ANCIENT:  return "ancient";
        case OrganQuality::PERFECT:  return "perfect";
        default:                     return "unknown";
    }
}

inline float organ_quality_multiplier(OrganQuality quality) {
    switch (quality) {
        case OrganQuality::FLAWED:   return 0.6f;
        case OrganQuality::COMMON:   return 1.0f;
        case OrganQuality::GOOD:     return 1.3f;
        case OrganQuality::PURE:     return 1.6f;
        case OrganQuality::ANCIENT:  return 1.8f;
        case OrganQuality::PERFECT:  return 2.0f;
        default:                     return 1.0f;
    }
}

// ============================================================
// OrganTransformType — how an organ was transformed
// ============================================================
//
// SUBLIMATION: organ transformed via elixir (sublimation path).
// BLOODLINE:   organ transformed by devouring a dropped source organ.
//              Bloodline organs are weaker imitations of the source.

enum class OrganTransformType : uint8_t {
    NONE = 0,
    SUBLIMATION,
    BLOODLINE,
    COUNT
};

inline const char* organ_transform_type_name(OrganTransformType type) {
    switch (type) {
        case OrganTransformType::NONE:         return "none";
        case OrganTransformType::SUBLIMATION:  return "sublimation";
        case OrganTransformType::BLOODLINE:    return "bloodline";
        default:                               return "unknown";
    }
}

// ============================================================
// BloodlineSource — origin category of a devoured organ
// ============================================================
//
// CREATURE:   organ dropped by a normal creature (e.g. rock lizard).
// ABERRATION: organ dropped by an aberration (distorted sublimation
//             organ).  Grants an imitation of the corresponding
//             sublimation path organ.

enum class BloodlineSource : uint8_t {
    NONE = 0,
    CREATURE,
    ABERRATION,
    COUNT
};

inline const char* bloodline_source_name(BloodlineSource src) {
    switch (src) {
        case BloodlineSource::NONE:        return "none";
        case BloodlineSource::CREATURE:    return "creature";
        case BloodlineSource::ABERRATION:  return "aberration";
        default:                           return "unknown";
    }
}

// ============================================================
// SublimationPath — identifier for a sublimation road
// ============================================================
//
// V0.1 only defines SAND_ARMOR; more paths added in later versions.

enum class SublimationPath : uint8_t {
    NONE = 0,
    SAND_ARMOR,
    TIDAL,
    STORM,
    FURNACE,
    RADIANCE,
    COUNT
};

inline const char* sublimation_path_name(SublimationPath path) {
    switch (path) {
        case SublimationPath::NONE:       return "none";
        case SublimationPath::SAND_ARMOR: return "sand_armor";
        case SublimationPath::TIDAL:      return "tidal";
        case SublimationPath::STORM:      return "storm";
        case SublimationPath::FURNACE:    return "furnace";
        case SublimationPath::RADIANCE:   return "radiance";
        default:                          return "unknown";
    }
}

// ============================================================
// Element relation lookup
// ============================================================

ElementRelation get_element_relation(magic::RuneElement a, magic::RuneElement b);

} // namespace science_and_theology::source_law
