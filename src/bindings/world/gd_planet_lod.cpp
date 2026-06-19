#include "gd_planet_lod.hpp"

#include <cmath>
#include <algorithm>

#include <godot_cpp/core/class_db.hpp>

namespace science_and_theology {

int64_t GDPlanetLod::compute_lod_level(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius) {
    if (planet_radius <= 0.0f) {
        return 0;
    }

    const float center_distance = player_position.distance_to(planet_center);
    const float surface_distance = std::max(0.0f, center_distance - planet_radius);
    const float ratio = surface_distance / planet_radius;

    if (ratio <= kLod0Ratio) {
        return 0;
    } else if (ratio <= kLod1Ratio) {
        return 1;
    } else if (ratio <= kLod2Ratio) {
        return 2;
    } else if (ratio <= kLod3Ratio) {
        return 3;
    } else {
        return 4;
    }
}

godot::Dictionary GDPlanetLod::compute_lod_distances(float planet_radius) {
    godot::Dictionary result;
    result["lod0_max"] = planet_radius * (1.0f + kLod0Ratio);
    result["lod1_max"] = planet_radius * (1.0f + kLod1Ratio);
    result["lod2_max"] = planet_radius * (1.0f + kLod2Ratio);
    result["lod3_max"] = planet_radius * (1.0f + kLod3Ratio);
    result["lod4_max"] = planet_radius * (1.0f + kLod3Ratio * 10.0f);
    return result;
}

float GDPlanetLod::compute_lod_fade_alpha(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius,
        int64_t lod_level) {
    if (planet_radius <= 0.0f) {
        return 0.0f;
    }

    const float center_distance = player_position.distance_to(planet_center);
    const float surface_distance = std::max(0.0f, center_distance - planet_radius);
    const float ratio = surface_distance / planet_radius;

    // Compute the band [lo, hi] for the current LOD level.
    float lo = 0.0f;
    float hi = kLod0Ratio;

    switch (static_cast<int>(lod_level)) {
        case 0: lo = 0.0f; hi = kLod0Ratio; break;
        case 1: lo = kLod0Ratio; hi = kLod1Ratio; break;
        case 2: lo = kLod1Ratio; hi = kLod2Ratio; break;
        case 3: lo = kLod2Ratio; hi = kLod3Ratio; break;
        case 4: lo = kLod3Ratio; hi = kLod3Ratio * 10.0f; break;
        default: return 0.0f;
    }

    const float band_width = hi - lo;
    if (band_width <= 0.0f) {
        return 0.0f;
    }

    // Fade starts at (hi - blend_zone) and reaches 1.0 at hi.
    const float blend_zone = band_width * kBlendZoneRatio;
    const float fade_start = hi - blend_zone;

    if (ratio <= fade_start) {
        return 0.0f;
    }
    if (ratio >= hi) {
        return 1.0f;
    }

    // Linear interpolation within the blend zone.
    return (ratio - fade_start) / blend_zone;
}

float GDPlanetLod::compute_surface_distance(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius) {
    const float dist = player_position.distance_to(planet_center);
    return dist - planet_radius;
}

void GDPlanetLod::_bind_methods() {
    using B = GDPlanetLod;

    godot::ClassDB::bind_static_method("GDPlanetLod",
        godot::D_METHOD("compute_lod_level", "player_position", "planet_center", "planet_radius"),
        &B::compute_lod_level);

    godot::ClassDB::bind_static_method("GDPlanetLod",
        godot::D_METHOD("compute_lod_distances", "planet_radius"),
        &B::compute_lod_distances);

    godot::ClassDB::bind_static_method("GDPlanetLod",
        godot::D_METHOD("compute_lod_fade_alpha", "player_position", "planet_center",
                 "planet_radius", "lod_level"),
        &B::compute_lod_fade_alpha);

    godot::ClassDB::bind_static_method("GDPlanetLod",
        godot::D_METHOD("compute_surface_distance", "player_position", "planet_center",
                 "planet_radius"),
        &B::compute_surface_distance);
}

} // namespace science_and_theology
