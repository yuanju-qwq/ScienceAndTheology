#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

namespace science_and_theology {

// Pure-computation helper for chunk rendering and coordinate transforms.
// All methods are static and stateless; they exist so that hot-path
// calculations (coordinate math, ladder facing, greedy mesh) run in C++
// instead of interpreted GDScript. Planet chunk selection lives in
// GDPlanetShellHelper, not in this renderer helper.
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
    static godot::PackedByteArray compute_surface_mask(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material);

    // --- Greedy meshing ---

    // Build a greedy-meshed chunk. Returns a Dictionary keyed by material_id (int).
    // Each value is a Dictionary with:
    //   "vertices": PackedVector3Array  - vertex positions in chunk-local space
    //   "normals":  PackedVector3Array  - face normals
    //   "uvs":      PackedVector2Array  - UV coordinates (per-face, supports atlas)
    //   "uvs2":     PackedVector2Array  - UV2: x = face_type (0=top, 1=bottom, 2=sides)
    //   "indices":  PackedInt32Array    - triangle indices
    //
    // The greedy algorithm merges adjacent same-material faces along the
    // secondary axis, producing wider/taller quads instead of one quad per
    // voxel face. This drastically reduces vertex/tri count.
    // UV2.x encodes the face type for per-face texture selection in shader.
    static godot::Dictionary build_greedy_mesh(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material,
        const godot::PackedByteArray& transparent_material_mask,
        const godot::Dictionary& neighbor_materials);

    // Build collision faces for a chunk. Returns a Dictionary with:
    //   "vertices": PackedVector3Array  - vertex positions in chunk-local space
    //   "indices":  PackedInt32Array    - triangle indices
    // Only faces exposed to a non-collidable voxel are included.
    // Intended for a single ConcavePolygonShape3D per chunk.
    static godot::Dictionary build_collision_faces(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const godot::PackedByteArray& collidable_material_mask);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
