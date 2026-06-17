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

    // Compute gravity direction toward a planet center.
    // Returns Vector3.DOWN if planet gravity is disabled or player is
    // outside the gravity radius.
    static godot::Vector3 compute_gravity_direction(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_gravity_radius,
        bool use_planet_gravity);

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
