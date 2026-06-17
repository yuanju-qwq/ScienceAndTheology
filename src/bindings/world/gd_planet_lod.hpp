#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace science_and_theology {

// Pure-computation helper for planet LOD level decisions.
// All methods are static and stateless so that per-frame LOD
// calculations run in C++ instead of interpreted GDScript.
//
// LOD levels (based on distance from player to planet center):
//   LOD 0 — near surface: real voxel chunks
//   LOD 1 — mid range: simplified chunk meshes
//   LOD 2 — far range: planet proxy sphere
//   LOD 3 — very far / space: low-poly sphere + billboard
class GDPlanetLod : public godot::Node {
    GDCLASS(GDPlanetLod, godot::Node)

public:
    GDPlanetLod() = default;
    ~GDPlanetLod() override = default;

    // Compute the current LOD level (0-3) based on player distance
    // to the planet center. Distance thresholds are derived from
    // planet_radius using the ratios defined in the design doc §11.2.
    static int64_t compute_lod_level(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius);

    // Return a Dictionary with the four LOD distance thresholds.
    // Keys: "lod0_max", "lod1_max", "lod2_max", "lod3_max".
    // Values: float distances from planet center.
    static godot::Dictionary compute_lod_distances(float planet_radius);

    // Compute a fade alpha [0..1] for smooth transitions between LOD levels.
    // When the player is near a LOD boundary, alpha transitions from 0 to 1
    // over a configurable blend zone. This can be used to fade mesh opacity
    // or blend shader parameters.
    //   lod_level: current LOD level (0-3)
    //   Returns: 0.0 = fully in current LOD, 1.0 = ready to switch to next LOD
    static float compute_lod_fade_alpha(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius,
        int64_t lod_level);

    // Compute the surface distance from the player to the planet surface.
    // Negative values mean the player is inside the planet.
    static float compute_surface_distance(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_radius);

protected:
    static void _bind_methods();

private:
    // LOD distance ratios relative to planet_radius.
    // LOD 0: 0 ~ kLod0Ratio * radius
    // LOD 1: kLod0Ratio ~ kLod1Ratio * radius
    // LOD 2: kLod1Ratio ~ kLod2Ratio * radius
    // LOD 3: kLod2Ratio * radius and beyond
    static constexpr float kLod0Ratio = 0.4f;
    static constexpr float kLod1Ratio = 0.8f;
    static constexpr float kLod2Ratio = 3.0f;

    // Blend zone width as a fraction of the LOD band width.
    // 0.15 means the fade starts at 85% into a LOD band and
    // completes at 100% (the boundary).
    static constexpr float kBlendZoneRatio = 0.15f;
};

} // namespace science_and_theology
