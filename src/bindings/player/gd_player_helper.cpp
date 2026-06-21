#include "gd_player_helper.h"

#include <cmath>
#include <limits>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

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
    // Outside gravity radius — zero-G in outer space.
    return godot::Vector3(0.0f, 0.0f, 0.0f);
}

godot::Vector3 GDPlayerHelper::compute_gravity_direction_multi(
        const godot::Vector3& player_position,
        const godot::PackedVector3Array& planet_centers,
        const godot::PackedFloat32Array& planet_radii,
        const godot::PackedFloat32Array& gravity_multipliers) {
    const int64_t count_a = static_cast<int64_t>(planet_centers.size());
    const int64_t count_b = static_cast<int64_t>(planet_radii.size());
    const int64_t count_c = static_cast<int64_t>(gravity_multipliers.size());
    const int64_t count = std::min(count_a, std::min(count_b, count_c));

    if (count == 0) {
        return godot::Vector3(0.0f, 0.0f, 0.0f);
    }

    godot::Vector3 best_dir(0.0f, 0.0f, 0.0f);
    float best_influence = 0.0f;

    for (int i = 0; i < count; ++i) {
        const godot::Vector3 center = planet_centers[i];
        const float radius = planet_radii[i];
        const float mult = gravity_multipliers[i];

        if (radius <= 0.0f) {
            continue;
        }

        // Gravity radius is 4x the planet radius (matching V20 milestone).
        const float gravity_radius = radius * 4.0f;

        const godot::Vector3 to_center = center - player_position;
        const float dist = to_center.length();

        if (dist > gravity_radius || dist < 0.01f) {
            continue;
        }

        // Inverse-square falloff within the gravity radius.
        const float t = 1.0f - (dist / gravity_radius);
        const float influence = mult * t * t;

        if (influence > best_influence) {
            best_influence = influence;
            best_dir = to_center.normalized();
        }
    }

    return best_dir;
}

godot::Basis GDPlayerHelper::align_body_to_gravity(
        const godot::Basis& current_basis,
        const godot::Vector3& target_up,
        float rotation_speed,
        float delta) {
    constexpr float kDirectionEpsilonSq = 1.0e-12f;
    if (target_up.length_squared() < kDirectionEpsilonSq) {
        return current_basis;
    }

    // Godot stores Basis rows internally, but local x/y/z axes are columns.
    // Using rows here creates a feedback error once the body is rotated.
    const godot::Basis normalized_basis = current_basis.orthonormalized();
    const godot::Vector3 current_up = normalized_basis.get_column(1);
    const godot::Vector3 normalized_target_up = target_up.normalized();
    const float dot = std::fmax(
        -1.0f, std::fmin(1.0f, current_up.dot(normalized_target_up)));
    const godot::Vector3 axis = current_up.cross(normalized_target_up);
    const float axis_length_sq = axis.length_squared();
    const float max_angle = std::fmax(0.0f, rotation_speed * delta);
    if (max_angle <= 0.0f) {
        return normalized_basis;
    }

    // Parallel directions are aligned. Anti-parallel directions need a
    // stable local fallback axis because their cross product is degenerate.
    if (axis_length_sq < kDirectionEpsilonSq) {
        if (dot >= 0.0f) {
            return normalized_basis;
        }
        const godot::Vector3 fallback_axis =
            normalized_basis.get_column(2).normalized();
        const float angle = static_cast<float>(M_PI);
        const float actual_angle = std::fmin(angle, max_angle);
        const godot::Basis rot(fallback_axis, actual_angle);
        const godot::Basis result = rot * normalized_basis;
        return result.orthonormalized();
    }

    // atan2 remains stable for tiny angles, so gravity changes are applied
    // continuously instead of accumulating in a visible angular dead zone.
    const float axis_length = std::sqrt(axis_length_sq);
    const float angle = std::atan2(axis_length, dot);
    const float actual_angle = std::fmin(angle, max_angle);
    const godot::Basis rot(axis / axis_length, actual_angle);
    const godot::Basis result = rot * normalized_basis;
    return result.orthonormalized();
}

void GDPlayerHelper::_bind_methods() {
    using B = GDPlayerHelper;

    godot::ClassDB::bind_static_method("GDPlayerHelper",
        godot::D_METHOD("compute_gravity_direction", "player_position", "planet_center",
                 "planet_gravity_radius", "use_planet_gravity"),
        &B::compute_gravity_direction);

    godot::ClassDB::bind_static_method("GDPlayerHelper",
        godot::D_METHOD("compute_gravity_direction_multi", "player_position",
                 "planet_centers", "planet_radii", "gravity_multipliers"),
        &B::compute_gravity_direction_multi);

    godot::ClassDB::bind_static_method("GDPlayerHelper",
        godot::D_METHOD("align_body_to_gravity", "current_basis", "target_up",
                 "rotation_speed", "delta"),
        &B::align_body_to_gravity);
}

} // namespace science_and_theology
