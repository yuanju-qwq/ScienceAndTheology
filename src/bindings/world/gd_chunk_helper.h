#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

namespace science_and_theology {

// Pure-computation helper for chunk rendering and coordinate transforms.
// All methods are static and stateless; they exist so that hot-path
// calculations (visibility, coordinate math, ladder facing) run in C++
// instead of interpreted GDScript.
class GDChunkHelper : public godot::Node {
    GDCLASS(GDChunkHelper, godot::Node)

public:
    GDChunkHelper() = default;
    ~GDChunkHelper() override = default;

    // --- Coordinate transforms ---

    static godot::Vector3i world_position_to_cell(const godot::Vector3& world_position);
    static godot::Vector3 cell_to_world_position(const godot::Vector3i& cell);
    static godot::Vector3i cell_to_chunk(const godot::Vector3i& cell, int32_t chunk_size);
    static godot::Vector3i cell_to_local(const godot::Vector3i& cell, int32_t chunk_size);
    static godot::Vector3i world_position_to_chunk(const godot::Vector3& world_position, int32_t chunk_size);

    // --- Terrain array helpers ---

    static int64_t terrain_index(int32_t local_x, int32_t local_y, int32_t local_z,
                                 int32_t size_x, int32_t size_z);

    static bool is_surface_voxel(const godot::Vector3i& local,
                                 const godot::PackedByteArray& materials,
                                 int32_t size_x, int32_t size_y, int32_t size_z,
                                 int32_t air_material, int32_t ladder_material);

    static float ladder_facing(const godot::Vector3i& local,
                               const godot::PackedByteArray& materials,
                               int32_t size_x, int32_t size_y, int32_t size_z,
                               int32_t air_material);

    // Compute a surface mask for the chunk: one byte per voxel.
    // 1 = surface voxel (at least one neighbor is air/ladder),
    // 0 = fully interior voxel (all 6 neighbors are solid).
    // This is used for LOD 1 simplified rendering where only
    // surface voxels are drawn, skipping interior geometry.
    static godot::PackedByteArray compute_surface_mask(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material);

    // --- Chunk visibility ---

    // Returns a Dictionary with keys "wanted_visible" (Dictionary of Vector3i->bool)
    // and "visible_order" (Array of Vector3i sorted by distance to player_chunk).
    static godot::Dictionary compute_visible_chunks(
        const godot::Vector3i& player_chunk,
        int32_t loaded_radius, int32_t view_radius,
        bool use_spherical_loading);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
