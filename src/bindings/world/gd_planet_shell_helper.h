#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

namespace science_and_theology {

// Pure-computation helper for large-planet shell streaming.
// Keeps the local tangent-disk + radial shell candidate enumeration in C++ while
// preserving the existing GDScript scene-tree, loading, meshing, and fallback paths.
class GDPlanetShellHelper : public godot::Node {
    GDCLASS(GDPlanetShellHelper, godot::Node)

public:
    GDPlanetShellHelper() = default;
    ~GDPlanetShellHelper() override = default;

    godot::Array compute_shell_chunk_order(
        const godot::Vector3& player_position,
        const godot::Vector3& planet_center,
        double planet_radius,
        double active_shell_above,
        double active_shell_below,
        int32_t chunk_size,
        int32_t radius_chunks) const;

    // Tangent-plane distance from a chunk's center to the player's surface
    // point on the planet. Used to prioritize which chunks to mesh first.
    // Sinks of PlanetShellChunkRendererBridge._chunk_tangent_distance.
    static float chunk_tangent_distance(
        const godot::Vector3i& chunk,
        const godot::Vector3& player_pos,
        const godot::Vector3& planet_center,
        float planet_radius,
        int32_t chunk_size);

    // Test whether a chunk's center falls inside the active radial shell
    // around the planet surface. `altitude_at_center` is the chunk center's
    // altitude relative to the planet surface (positive = above surface).
    // Sinks of PlanetShellChunkRendererBridge._chunk_intersects_active_shell.
    static bool chunk_intersects_active_shell(
        float altitude_at_center,
        double active_shell_above,
        double active_shell_below,
        int32_t chunk_size);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
