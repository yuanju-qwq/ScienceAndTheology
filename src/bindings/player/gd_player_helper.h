#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace science_and_theology {

// Pure-computation helper for player physics calculations.
// Gravity direction and body alignment math run in C++ to avoid
// GDScript interpretation overhead on per-frame hot paths.
class GDPlayerHelper : public godot::Node {
    GDCLASS(GDPlayerHelper, godot::Node)

public:
    GDPlayerHelper() = default;
    ~GDPlayerHelper() override = default;

    // Compute gravity direction toward a single planet center.
    // Returns Vector3.DOWN if planet gravity is disabled.
    // Returns Vector3.ZERO if player is outside the gravity radius (zero-G in space).
    static godot::Vector3 compute_gravity_direction(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_gravity_radius,
        bool use_planet_gravity);

    // Compute gravity direction across multiple planets.
    // Each planet is specified as three consecutive elements in the packed arrays:
    //   planet_centers[i]  — universe-space position of planet center
    //   planet_radii[i]    — planet radius in blocks
    //   gravity_multipliers[i] — gravity strength multiplier (1.0 = standard)
    // Returns the gravity direction toward the planet with the strongest
    // influence at the player's position. Returns Vector3.ZERO if the
    // player is outside all gravity radii (deep space).
    static godot::Vector3 compute_gravity_direction_multi(
        const godot::Vector3& player_position,
        const godot::PackedVector3Array& planet_centers,
        const godot::PackedFloat32Array& planet_radii,
        const godot::PackedFloat32Array& gravity_multipliers);

    // Compute a rotation that aligns the body's up vector toward
    // the target up direction, with a maximum rotation angle per step.
    // Returns the new Basis after rotation, or the current basis if
    // already well-aligned (dot > 0.999).
    static godot::Basis align_body_to_gravity(
        const godot::Basis& current_basis,
        const godot::Vector3& target_up,
        float rotation_speed,
        float delta);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
