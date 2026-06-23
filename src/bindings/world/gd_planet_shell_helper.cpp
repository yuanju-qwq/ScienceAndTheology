#include "gd_planet_shell_helper.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>

#include "gd_chunk_helper.h"

namespace science_and_theology {
namespace {

godot::Vector3 normalized_or_up(const godot::Vector3& v) {
    const double len_sq = static_cast<double>(v.x) * v.x
        + static_cast<double>(v.y) * v.y
        + static_cast<double>(v.z) * v.z;
    if (len_sq < 1.0e-8) {
        return godot::Vector3(0.0f, 1.0f, 0.0f);
    }
    const double inv_len = 1.0 / std::sqrt(len_sq);
    return godot::Vector3(
        static_cast<float>(v.x * inv_len),
        static_cast<float>(v.y * inv_len),
        static_cast<float>(v.z * inv_len));
}

double dot3(const godot::Vector3& a, const godot::Vector3& b) {
    return static_cast<double>(a.x) * b.x
        + static_cast<double>(a.y) * b.y
        + static_cast<double>(a.z) * b.z;
}

godot::Vector3 cross3(const godot::Vector3& a, const godot::Vector3& b) {
    return godot::Vector3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

godot::Vector3 tangent_axis_for_up(const godot::Vector3& up) {
    godot::Vector3 reference(0.0f, 0.0f, -1.0f); // Godot Vector3.FORWARD
    if (std::abs(dot3(reference, up)) > 0.95) {
        reference = godot::Vector3(1.0f, 0.0f, 0.0f); // Godot Vector3.RIGHT
    }
    return normalized_or_up(cross3(reference, up));
}

godot::Vector3 chunk_center(const godot::Vector3i& chunk, int32_t chunk_size) {
    const float size = static_cast<float>(chunk_size);
    return godot::Vector3(
        (static_cast<float>(chunk.x) + 0.5f) * size,
        (static_cast<float>(chunk.y) + 0.5f) * size,
        (static_cast<float>(chunk.z) + 0.5f) * size);
}

bool chunk_intersects_active_shell(
        const godot::Vector3i& chunk,
        const godot::Vector3& planet_center,
        double planet_radius,
        double active_shell_above,
        double active_shell_below,
        int32_t chunk_size) {
    const godot::Vector3 center = chunk_center(chunk, chunk_size);
    const godot::Vector3 rel = center - planet_center;
    const double dist = std::sqrt(dot3(rel, rel));
    const double altitude = dist - planet_radius;
    const double half_diag = std::sqrt(3.0) * static_cast<double>(chunk_size) * 0.5;
    return altitude >= -active_shell_below - half_diag
        && altitude <= active_shell_above + half_diag;
}

} // namespace

godot::Array GDPlanetShellHelper::compute_shell_chunk_order(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        double planet_radius,
        double active_shell_above,
        double active_shell_below,
        int32_t chunk_size,
        int32_t radius_chunks) const {
    godot::Array result;
    if (chunk_size <= 0 || radius_chunks < 0 || planet_radius <= 0.0) {
        return result;
    }

    const godot::Vector3 up = normalized_or_up(player_position - planet_center);
    const godot::Vector3 surface_point = planet_center + up * static_cast<float>(planet_radius);
    const godot::Vector3 tangent_a = tangent_axis_for_up(up);
    const godot::Vector3 tangent_b = normalized_or_up(cross3(up, tangent_a));

    int32_t above_layers = std::max(
        0,
        static_cast<int32_t>(std::ceil(active_shell_above / static_cast<double>(chunk_size))));
    int32_t below_layers = std::max(
        0,
        static_cast<int32_t>(std::ceil(active_shell_below / static_cast<double>(chunk_size))));
    above_layers = std::max(above_layers, 1);
    below_layers = std::max(below_layers, 1);

    godot::Dictionary seen;
    const int32_t radius_sq = radius_chunks * radius_chunks;
    const float size = static_cast<float>(chunk_size);

    for (int32_t ring = 0; ring <= radius_chunks; ++ring) {
        for (int32_t tx = -ring; tx <= ring; ++tx) {
            for (int32_t tz = -ring; tz <= ring; ++tz) {
                if (std::max(std::abs(tx), std::abs(tz)) != ring) {
                    continue;
                }
                if (tx * tx + tz * tz > radius_sq) {
                    continue;
                }
                for (int32_t h = -below_layers; h <= above_layers; ++h) {
                    const godot::Vector3 sample = surface_point
                        + tangent_a * (static_cast<float>(tx) * size)
                        + tangent_b * (static_cast<float>(tz) * size)
                        + up * (static_cast<float>(h) * size);
                    const godot::Vector3i chunk =
                        GDChunkHelper::world_position_to_chunk(sample, chunk_size);
                    if (seen.has(chunk)) {
                        continue;
                    }
                    if (!chunk_intersects_active_shell(
                            chunk, planet_center, planet_radius,
                            active_shell_above, active_shell_below, chunk_size)) {
                        continue;
                    }
                    seen[chunk] = true;
                    result.append(chunk);
                }
            }
        }
    }

    return result;
}

void GDPlanetShellHelper::_bind_methods() {
    using B = GDPlanetShellHelper;
    godot::ClassDB::bind_method(godot::D_METHOD(
        "compute_shell_chunk_order",
        "player_position", "planet_center", "planet_radius",
        "active_shell_above", "active_shell_below", "chunk_size",
        "radius_chunks"),
        &B::compute_shell_chunk_order);
}

} // namespace science_and_theology
