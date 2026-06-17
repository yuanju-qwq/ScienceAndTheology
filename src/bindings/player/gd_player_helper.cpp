#include "gd_player_helper.h"

#include <cmath>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/basis.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace science_and_theology {

godot::Vector3 GDPlayerHelper::compute_gravity_direction(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        float planet_gravity_radius,
        bool use_planet_gravity) {
    if (!use_planet_gravity) {
        return godot::Vector3(0.0f, -1.0f, 0.0f);
    }

    const godot::Vector3 to_center = planet_center - player_position;
    const float dist = to_center.length();

    if (dist < planet_gravity_radius && dist > 0.01f) {
        return to_center.normalized();
    }
    return godot::Vector3(0.0f, -1.0f, 0.0f);
}

godot::Basis GDPlayerHelper::align_body_to_gravity(
        const godot::Basis& current_basis,
        const godot::Vector3& target_up,
        float rotation_speed,
        float delta) {
    const godot::Vector3 current_up = current_basis.rows[1];
    const float dot = current_up.dot(target_up);

    // Already well-aligned.
    if (dot > 0.999f) {
        return current_basis;
    }

    const godot::Vector3 axis = current_up.cross(target_up);
    const float axis_length_sq = axis.length_squared();
    if (axis_length_sq < 0.0001f) {
        return current_basis;
    }

    const float angle = std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot)));
    const float max_angle = rotation_speed * delta;
    const float actual_angle = std::fmin(angle, max_angle);

    const godot::Vector3 normalized_axis = axis / std::sqrt(axis_length_sq);

    // Rotate the basis around the axis by actual_angle.
    const float c = std::cos(actual_angle);
    const float s = std::sin(actual_angle);
    const float t = 1.0f - c;

    const float x = normalized_axis.x;
    const float y = normalized_axis.y;
    const float z = normalized_axis.z;

    godot::Basis rot;
    rot.rows[0] = godot::Vector3(t * x * x + c,     t * x * y - s * z, t * x * z + s * y);
    rot.rows[1] = godot::Vector3(t * x * y + s * z, t * y * y + c,     t * y * z - s * x);
    rot.rows[2] = godot::Vector3(t * x * z - s * y, t * y * z + s * x, t * z * z + c);

    godot::Basis result = rot * current_basis;
    return result.orthonormalized();
}

void GDPlayerHelper::_bind_methods() {
    using B = GDPlayerHelper;

    godot::ClassDB::bind_static_method("GDPlayerHelper",
        godot::D_METHOD("compute_gravity_direction", "player_position", "planet_center",
                 "planet_gravity_radius", "use_planet_gravity"),
        &B::compute_gravity_direction);

    godot::ClassDB::bind_static_method("GDPlayerHelper",
        godot::D_METHOD("align_body_to_gravity", "current_basis", "target_up",
                 "rotation_speed", "delta"),
        &B::align_body_to_gravity);
}

} // namespace science_and_theology
