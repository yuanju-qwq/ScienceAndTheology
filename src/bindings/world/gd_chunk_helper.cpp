#include "gd_chunk_helper.h"

#include <array>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace science_and_theology {

// --- Coordinate transforms ---

godot::Vector3i GDChunkHelper::world_position_to_cell(const godot::Vector3& world_position) {
    return godot::Vector3i(
        static_cast<int32_t>(std::floor(world_position.x)),
        static_cast<int32_t>(std::floor(world_position.y)),
        static_cast<int32_t>(std::floor(world_position.z)));
}

godot::Vector3 GDChunkHelper::cell_to_world_position(const godot::Vector3i& cell) {
    return godot::Vector3(
        (static_cast<float>(cell.x) + 0.5f),
        (static_cast<float>(cell.y) + 0.5f),
        (static_cast<float>(cell.z) + 0.5f));
}

godot::Vector3i GDChunkHelper::cell_to_chunk(const godot::Vector3i& cell, int32_t chunk_size) {
    const float inv = 1.0f / static_cast<float>(chunk_size);
    return godot::Vector3i(
        static_cast<int32_t>(std::floor(static_cast<float>(cell.x) * inv)),
        static_cast<int32_t>(std::floor(static_cast<float>(cell.y) * inv)),
        static_cast<int32_t>(std::floor(static_cast<float>(cell.z) * inv)));
}

godot::Vector3i GDChunkHelper::cell_to_local(const godot::Vector3i& cell, int32_t chunk_size) {
    const godot::Vector3i chunk = cell_to_chunk(cell, chunk_size);
    return godot::Vector3i(
        cell.x - chunk.x * chunk_size,
        cell.y - chunk.y * chunk_size,
        cell.z - chunk.z * chunk_size);
}

godot::Vector3i GDChunkHelper::world_position_to_chunk(const godot::Vector3& world_position, int32_t chunk_size) {
    const godot::Vector3i cell = world_position_to_cell(world_position);
    return cell_to_chunk(cell, chunk_size);
}

// --- Terrain array helpers ---

int64_t GDChunkHelper::terrain_index(int32_t local_x, int32_t local_y, int32_t local_z,
                                      int32_t size_x, int32_t size_z) {
    return static_cast<int64_t>((local_y * size_z + local_z) * size_x + local_x);
}

bool GDChunkHelper::is_surface_voxel(const godot::Vector3i& local,
                                      const godot::PackedByteArray& materials,
                                      int32_t size_x, int32_t size_y, int32_t size_z,
                                      int32_t air_material, int32_t ladder_material) {
    const godot::Vector3i offsets[] = {
        godot::Vector3i(1, 0, 0), godot::Vector3i(-1, 0, 0),
        godot::Vector3i(0, 1, 0), godot::Vector3i(0, -1, 0),
        godot::Vector3i(0, 0, 1), godot::Vector3i(0, 0, -1),
    };
    for (const auto& offset : offsets) {
        const godot::Vector3i n = local + offset;
        if (n.x < 0 || n.x >= size_x || n.y < 0 || n.y >= size_y || n.z < 0 || n.z >= size_z) {
            return true;
        }
        const int64_t idx = terrain_index(n.x, n.y, n.z, size_x, size_z);
        if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
            return true;
        }
        const int32_t neighbor_material = static_cast<int32_t>(materials[idx]);
        if (neighbor_material == air_material || neighbor_material == ladder_material) {
            return true;
        }
    }
    return false;
}

float GDChunkHelper::ladder_facing(const godot::Vector3i& local,
                                    const godot::PackedByteArray& materials,
                                    int32_t size_x, int32_t size_y, int32_t size_z,
                                    int32_t air_material) {
    struct Check {
        godot::Vector3i offset;
        float angle;
    };
    const Check checks[] = {
        { godot::Vector3i(0, 0, 1),  static_cast<float>(M_PI) },
        { godot::Vector3i(0, 0, -1), 0.0f },
        { godot::Vector3i(1, 0, 0),  static_cast<float>(M_PI) * 0.5f },
        { godot::Vector3i(-1, 0, 0), static_cast<float>(M_PI) * 1.5f },
    };
    for (const auto& check : checks) {
        const godot::Vector3i n = local + check.offset;
        if (n.x < 0 || n.x >= size_x || n.y < 0 || n.y >= size_y || n.z < 0 || n.z >= size_z) {
            continue;
        }
        const int64_t idx = terrain_index(n.x, n.y, n.z, size_x, size_z);
        if (idx >= 0 && idx < static_cast<int64_t>(materials.size())) {
            if (static_cast<int32_t>(materials[idx]) != air_material) {
                return check.angle;
            }
        }
    }
    return 0.0f;
}

// --- Chunk visibility ---

godot::Dictionary GDChunkHelper::compute_visible_chunks(
        const godot::Vector3i& player_chunk,
        int32_t loaded_radius, int32_t view_radius,
        bool use_spherical_loading) {
    godot::Dictionary wanted_visible;
    godot::Array visible_order;

    const int32_t lr_sq = loaded_radius * loaded_radius;
    const int32_t vr_sq = view_radius * view_radius;

    if (use_spherical_loading) {
        for (int32_t cy = player_chunk.y - loaded_radius; cy <= player_chunk.y + loaded_radius; ++cy) {
            const int32_t dy_sq = (cy - player_chunk.y) * (cy - player_chunk.y);
            if (dy_sq > lr_sq) continue;
            for (int32_t cz = player_chunk.z - loaded_radius; cz <= player_chunk.z + loaded_radius; ++cz) {
                const int32_t dz_sq = (cz - player_chunk.z) * (cz - player_chunk.z);
                if (dy_sq + dz_sq > lr_sq) continue;
                for (int32_t cx = player_chunk.x - loaded_radius; cx <= player_chunk.x + loaded_radius; ++cx) {
                    const int32_t dx_sq = (cx - player_chunk.x) * (cx - player_chunk.x);
                    if (dx_sq + dy_sq + dz_sq > lr_sq) continue;
                    const godot::Vector3i pos(cx, cy, cz);
                    if (dx_sq + dy_sq + dz_sq <= vr_sq) {
                        wanted_visible[pos] = true;
                        visible_order.append(pos);
                    }
                }
            }
        }
    } else {
        for (int32_t cz = player_chunk.z - loaded_radius; cz <= player_chunk.z + loaded_radius; ++cz) {
            for (int32_t cx = player_chunk.x - loaded_radius; cx <= player_chunk.x + loaded_radius; ++cx) {
                const int32_t dx = cx - player_chunk.x;
                const int32_t dz = cz - player_chunk.z;
                if (dx * dx + dz * dz > lr_sq) continue;
                const godot::Vector3i pos(cx, player_chunk.y, cz);
                if (dx * dx + dz * dz <= vr_sq) {
                    wanted_visible[pos] = true;
                    visible_order.append(pos);
                }
            }
        }
    }

    // Sort by distance to player chunk.
    struct Entry {
        godot::Vector3i pos;
        int32_t dist_sq;
    };
    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(visible_order.size()));
    for (int64_t i = 0; i < visible_order.size(); ++i) {
        const godot::Vector3i pos = visible_order[i];
        const int32_t dx = pos.x - player_chunk.x;
        const int32_t dy = pos.y - player_chunk.y;
        const int32_t dz = pos.z - player_chunk.z;
        entries.push_back({pos, dx * dx + dy * dy + dz * dz});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.dist_sq != b.dist_sq) return a.dist_sq < b.dist_sq;
        if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
        if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
        return a.pos.z < b.pos.z;
    });

    godot::Array sorted_order;
    sorted_order.resize(static_cast<int64_t>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        sorted_order[static_cast<int64_t>(i)] = entries[i].pos;
    }

    godot::Dictionary result;
    result["wanted_visible"] = wanted_visible;
    result["visible_order"] = sorted_order;
    return result;
}

// --- Surface mask ---

godot::PackedByteArray GDChunkHelper::compute_surface_mask(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material) {
    const int64_t total = static_cast<int64_t>(size_x) * size_y * size_z;
    godot::PackedByteArray mask;
    mask.resize(total);
    memset(mask.ptrw(), 0, total);

    for (int32_t y = 0; y < size_y; ++y) {
        for (int32_t z = 0; z < size_z; ++z) {
            for (int32_t x = 0; x < size_x; ++x) {
                const int64_t idx = terrain_index(x, y, z, size_x, size_z);
                if (idx < 0 || idx >= total) continue;
                const int32_t mat = static_cast<int32_t>(materials[idx]);
                if (mat == air_material) continue;

                // Check 6 neighbors: if any is air/ladder, this is a surface voxel.
                bool is_surface = false;
                const godot::Vector3i offsets[] = {
                    godot::Vector3i(1, 0, 0), godot::Vector3i(-1, 0, 0),
                    godot::Vector3i(0, 1, 0), godot::Vector3i(0, -1, 0),
                    godot::Vector3i(0, 0, 1), godot::Vector3i(0, 0, -1),
                };
                for (const auto& offset : offsets) {
                    const int32_t nx = x + offset.x;
                    const int32_t ny = y + offset.y;
                    const int32_t nz = z + offset.z;
                    if (nx < 0 || nx >= size_x || ny < 0 || ny >= size_y || nz < 0 || nz >= size_z) {
                        is_surface = true;
                        break;
                    }
                    const int64_t n_idx = terrain_index(nx, ny, nz, size_x, size_z);
                    if (n_idx < 0 || n_idx >= total) {
                        is_surface = true;
                        break;
                    }
                    const int32_t n_mat = static_cast<int32_t>(materials[n_idx]);
                    if (n_mat == air_material || n_mat == ladder_material) {
                        is_surface = true;
                        break;
                    }
                }
                mask.ptrw()[idx] = is_surface ? 1 : 0;
            }
        }
    }
    return mask;
}

// --- Greedy meshing internals ---

namespace {

// Face direction constants: 0=+Y(top), 1=-Y(bottom), 2=+X, 3=-X, 4=+Z, 5=-Z.
// Face type for UV selection: 0=top(+Y), 1=bottom(-Y), 2=sides(rest).
struct FaceDir {
    static constexpr int kTop = 0;
    static constexpr int kBottom = 1;
    static constexpr int kPosX = 2;
    static constexpr int kNegX = 3;
    static constexpr int kPosZ = 4;
    static constexpr int kNegZ = 5;
    static constexpr int kCount = 6;
};

// Map face direction to face type for UV selection.
// 0=top, 1=bottom, 2=sides.
inline int face_type(int dir) {
    if (dir == FaceDir::kTop) return 0;
    if (dir == FaceDir::kBottom) return 1;
    return 2;
}

// Normal vector for each face direction.
inline godot::Vector3 face_normal(int dir) {
    switch (dir) {
        case FaceDir::kTop:    return godot::Vector3(0, 1, 0);
        case FaceDir::kBottom: return godot::Vector3(0, -1, 0);
        case FaceDir::kPosX:   return godot::Vector3(1, 0, 0);
        case FaceDir::kNegX:   return godot::Vector3(-1, 0, 0);
        case FaceDir::kPosZ:   return godot::Vector3(0, 0, 1);
        case FaceDir::kNegZ:   return godot::Vector3(0, 0, -1);
        default:               return godot::Vector3(0, 1, 0);
    }
}

// Neighbor offset for each face direction.
inline godot::Vector3i neighbor_offset(int dir) {
    switch (dir) {
        case FaceDir::kTop:    return godot::Vector3i(0, 1, 0);
        case FaceDir::kBottom: return godot::Vector3i(0, -1, 0);
        case FaceDir::kPosX:   return godot::Vector3i(1, 0, 0);
        case FaceDir::kNegX:   return godot::Vector3i(-1, 0, 0);
        case FaceDir::kPosZ:   return godot::Vector3i(0, 0, 1);
        case FaceDir::kNegZ:   return godot::Vector3i(0, 0, -1);
        default:               return godot::Vector3i(0, 0, 0);
    }
}

bool is_render_transparent(
        int32_t material,
        const godot::PackedByteArray& transparent_material_mask) {
    return material >= 0 && material < transparent_material_mask.size()
        && transparent_material_mask[material] != 0;
}

bool sample_neighbor_material(
        int32_t x, int32_t y, int32_t z, int dir,
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const std::array<godot::PackedByteArray, FaceDir::kCount>& boundary_materials,
        const std::array<bool, FaceDir::kCount>& boundary_available,
        int32_t& material_out) {
    if (x >= 0 && x < size_x && y >= 0 && y < size_y
        && z >= 0 && z < size_z) {
        const int64_t idx = GDChunkHelper::terrain_index(
            x, y, z, size_x, size_z);
        if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
            return false;
        }
        material_out = static_cast<int32_t>(materials[idx]);
        return true;
    }

    if (dir < 0 || dir >= FaceDir::kCount || !boundary_available[dir]) {
        return false;
    }

    switch (dir) {
        case FaceDir::kTop: y = 0; break;
        case FaceDir::kBottom: y = size_y - 1; break;
        case FaceDir::kPosX: x = 0; break;
        case FaceDir::kNegX: x = size_x - 1; break;
        case FaceDir::kPosZ: z = 0; break;
        case FaceDir::kNegZ: z = size_z - 1; break;
        default: return false;
    }

    const auto& neighbor = boundary_materials[dir];
    const int64_t idx = GDChunkHelper::terrain_index(
        x, y, z, size_x, size_z);
    if (idx < 0 || idx >= static_cast<int64_t>(neighbor.size())) {
        return false;
    }
    material_out = static_cast<int32_t>(neighbor[idx]);
    return true;
}

bool should_emit_render_face(
        int32_t material, bool has_neighbor, int32_t neighbor_material, int dir,
        int32_t air_material, int32_t ladder_material,
        const godot::PackedByteArray& transparent_material_mask) {
    const bool transparent = is_render_transparent(
        material, transparent_material_mask);
    if (!has_neighbor) {
        // Until an adjacent chunk arrives, only keep a liquid's horizontal
        // surface. This avoids transparent walls at streaming boundaries.
        return !transparent || dir == FaceDir::kTop;
    }
    if (neighbor_material == air_material || neighbor_material == ladder_material) {
        return true;
    }

    const bool neighbor_transparent = is_render_transparent(
        neighbor_material, transparent_material_mask);
    if (!transparent) {
        // Opaque terrain below water/ice must remain visible through it.
        return neighbor_transparent;
    }
    if (!neighbor_transparent || neighbor_material == material) {
        return false;
    }
    // At an interface between two transparent materials, emit one face only.
    return material > neighbor_material;
}

bool is_collidable_material(
        int32_t material, const godot::PackedByteArray& collidable_material_mask) {
    return material >= 0 && material < collidable_material_mask.size()
        && collidable_material_mask[material] != 0;
}

bool is_open_for_collision(
        int32_t x, int32_t y, int32_t z,
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const godot::PackedByteArray& collidable_material_mask) {
    if (x < 0 || x >= size_x || y < 0 || y >= size_y || z < 0 || z >= size_z) {
        return true;
    }
    const int64_t idx = GDChunkHelper::terrain_index(x, y, z, size_x, size_z);
    if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
        return true;
    }
    return !is_collidable_material(
        static_cast<int32_t>(materials[idx]), collidable_material_mask);
}

// Per-material mesh data accumulator.
struct MeshAccum {
    std::vector<godot::Vector3> vertices;
    std::vector<godot::Vector3> normals;
    std::vector<godot::Vector2> uvs;
    // UV2 encodes face type per vertex: x = face_type (0=top, 1=bottom, 2=sides).
    // UV2.y = variant hash (0..1) for random texture variant selection.
    std::vector<godot::Vector2> uvs2;
    std::vector<int32_t> indices;
    int32_t vertex_count = 0;

    void add_quad(const godot::Vector3 v0, const godot::Vector3 v1,
                  const godot::Vector3 v2, const godot::Vector3 v3,
                  const godot::Vector3& normal, const godot::Vector2& uv0,
                  const godot::Vector2& uv1, const godot::Vector2& uv2,
                  const godot::Vector2& uv3, int32_t ftype, float variant_hash) {
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);
        normals.push_back(normal);
        normals.push_back(normal);
        normals.push_back(normal);
        normals.push_back(normal);
        uvs.push_back(uv0);
        uvs.push_back(uv1);
        uvs.push_back(uv2);
        uvs.push_back(uv3);
        // Encode face type into UV2.x, variant hash into UV2.y.
        const float ft = static_cast<float>(ftype);
        const godot::Vector2 uv2_val(ft, variant_hash);
        uvs2.push_back(uv2_val);
        uvs2.push_back(uv2_val);
        uvs2.push_back(uv2_val);
        uvs2.push_back(uv2_val);
        // Two triangles: 0-1-2, 0-2-3.
        indices.push_back(vertex_count);
        indices.push_back(vertex_count + 1);
        indices.push_back(vertex_count + 2);
        indices.push_back(vertex_count);
        indices.push_back(vertex_count + 2);
        indices.push_back(vertex_count + 3);
        vertex_count += 4;
    }

    godot::Dictionary to_dict() const {
        godot::Dictionary d;
        godot::PackedVector3Array verts;
        godot::PackedVector3Array norms;
        godot::PackedVector2Array uvs_packed;
        godot::PackedVector2Array uvs2_packed;
        godot::PackedInt32Array idx_packed;

        verts.resize(static_cast<int64_t>(vertices.size()));
        norms.resize(static_cast<int64_t>(normals.size()));
        uvs_packed.resize(static_cast<int64_t>(uvs.size()));
        uvs2_packed.resize(static_cast<int64_t>(uvs2.size()));
        idx_packed.resize(static_cast<int64_t>(indices.size()));

        for (size_t i = 0; i < vertices.size(); ++i) {
            verts.ptrw()[i] = vertices[i];
        }
        for (size_t i = 0; i < normals.size(); ++i) {
            norms.ptrw()[i] = normals[i];
        }
        for (size_t i = 0; i < uvs.size(); ++i) {
            uvs_packed.ptrw()[i] = uvs[i];
        }
        for (size_t i = 0; i < uvs2.size(); ++i) {
            uvs2_packed.ptrw()[i] = uvs2[i];
        }
        for (size_t i = 0; i < indices.size(); ++i) {
            idx_packed.ptrw()[i] = indices[i];
        }

        d["vertices"] = verts;
        d["normals"] = norms;
        d["uvs"] = uvs_packed;
        d["uvs2"] = uvs2_packed;
        d["indices"] = idx_packed;
        return d;
    }
};

// Simple position hash for texture variant selection.
// Returns a value in [0, 1) based on 3D integer coordinates.
inline float position_hash(int32_t x, int32_t y, int32_t z) {
    // FNV-1a inspired hash, mapped to [0, 1).
    uint32_t h = 2166136261u;
    h ^= static_cast<uint32_t>(x); h *= 16777619u;
    h ^= static_cast<uint32_t>(y); h *= 16777619u;
    h ^= static_cast<uint32_t>(z); h *= 16777619u;
    return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
}

// Emit a quad for a face. The quad occupies [x,x+w] x [y,y+h] in the
// face plane, at depth d along the normal axis.
// dir: face direction (FaceDir constant).
// ft: face type (0=top, 1=bottom, 2=sides).
// variant_hash: position-based hash [0,1) for texture variant selection.
void emit_face_quad(MeshAccum& accum, int dir, int32_t d,
                    int32_t u_start, int32_t u_end,
                    int32_t v_start, int32_t v_end,
                    float variant_hash) {
    const float fd = static_cast<float>(d);
    const float fu0 = static_cast<float>(u_start);
    const float fu1 = static_cast<float>(u_end);
    const float fv0 = static_cast<float>(v_start);
    const float fv1 = static_cast<float>(v_end);

    godot::Vector3 v0, v1, v2, v3;
    godot::Vector2 uv0, uv1, uv2, uv3;

    switch (dir) {
        case FaceDir::kTop:  // +Y, plane: X-Z at y=d+1
            v0 = godot::Vector3(fu0, fd + 1.0f, fv0);
            v1 = godot::Vector3(fu1, fd + 1.0f, fv0);
            v2 = godot::Vector3(fu1, fd + 1.0f, fv1);
            v3 = godot::Vector3(fu0, fd + 1.0f, fv1);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(fu1 - fu0, 0.0f);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(0.0f, fv1 - fv0);
            break;
        case FaceDir::kBottom:  // -Y, plane: X-Z at y=d
            v0 = godot::Vector3(fu0, fd, fv1);
            v1 = godot::Vector3(fu1, fd, fv1);
            v2 = godot::Vector3(fu1, fd, fv0);
            v3 = godot::Vector3(fu0, fd, fv0);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(fu1 - fu0, 0.0f);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(0.0f, fv1 - fv0);
            break;
        case FaceDir::kPosX:  // +X, plane: Z-Y at x=d+1
            v0 = godot::Vector3(fd + 1.0f, fv0, fu1);
            v1 = godot::Vector3(fd + 1.0f, fv1, fu1);
            v2 = godot::Vector3(fd + 1.0f, fv1, fu0);
            v3 = godot::Vector3(fd + 1.0f, fv0, fu0);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(0.0f, fv1 - fv0);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(fu1 - fu0, 0.0f);
            break;
        case FaceDir::kNegX:  // -X, plane: Z-Y at x=d
            v0 = godot::Vector3(fd, fv0, fu0);
            v1 = godot::Vector3(fd, fv1, fu0);
            v2 = godot::Vector3(fd, fv1, fu1);
            v3 = godot::Vector3(fd, fv0, fu1);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(0.0f, fv1 - fv0);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(fu1 - fu0, 0.0f);
            break;
        case FaceDir::kPosZ:  // +Z, plane: X-Y at z=d+1
            v0 = godot::Vector3(fu0, fv0, fd + 1.0f);
            v1 = godot::Vector3(fu0, fv1, fd + 1.0f);
            v2 = godot::Vector3(fu1, fv1, fd + 1.0f);
            v3 = godot::Vector3(fu1, fv0, fd + 1.0f);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(0.0f, fv1 - fv0);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(fu1 - fu0, 0.0f);
            break;
        case FaceDir::kNegZ:  // -Z, plane: X-Y at z=d
            v0 = godot::Vector3(fu1, fv0, fd);
            v1 = godot::Vector3(fu1, fv1, fd);
            v2 = godot::Vector3(fu0, fv1, fd);
            v3 = godot::Vector3(fu0, fv0, fd);
            uv0 = godot::Vector2(0.0f, 0.0f);
            uv1 = godot::Vector2(0.0f, fv1 - fv0);
            uv2 = godot::Vector2(fu1 - fu0, fv1 - fv0);
            uv3 = godot::Vector2(fu1 - fu0, 0.0f);
            break;
    }

    accum.add_quad(v0, v1, v2, v3, face_normal(dir),
                   uv0, uv1, uv2, uv3, face_type(dir), variant_hash);
}

} // namespace

godot::Dictionary GDChunkHelper::build_greedy_mesh(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        int32_t air_material, int32_t ladder_material,
        const godot::PackedByteArray& transparent_material_mask,
        const godot::Dictionary& neighbor_materials) {
    // Accumulate mesh data per material.
    std::unordered_map<int32_t, MeshAccum> accum_by_material;
    std::array<godot::PackedByteArray, FaceDir::kCount> boundary_materials;
    std::array<bool, FaceDir::kCount> boundary_available{};
    for (int dir = 0; dir < FaceDir::kCount; ++dir) {
        if (!neighbor_materials.has(dir)) {
            continue;
        }
        boundary_materials[dir] = neighbor_materials[dir];
        boundary_available[dir] = !boundary_materials[dir].is_empty();
    }

    // For each of the 6 face directions, sweep the chunk in slices
    // along the normal axis and greedily merge same-material faces.
    for (int dir = 0; dir < FaceDir::kCount; ++dir) {
        // Determine sweep axis (d) and in-plane axes (u, v).
        // d = depth axis (normal direction), u/v = in-plane axes.
        int32_t d_size, u_size, v_size;
        switch (dir) {
            case FaceDir::kTop:
            case FaceDir::kBottom:
                d_size = size_y; u_size = size_x; v_size = size_z;
                break;
            case FaceDir::kPosX:
            case FaceDir::kNegX:
                d_size = size_x; u_size = size_z; v_size = size_y;
                break;
            case FaceDir::kPosZ:
            case FaceDir::kNegZ:
                d_size = size_z; u_size = size_x; v_size = size_y;
                break;
            default:
                continue;
        }

        for (int32_t d = 0; d < d_size; ++d) {
            // 2D mask: mask[u][v] = material_id if face is exposed, -1 otherwise.
            std::vector<std::vector<int32_t>> mask(
                static_cast<size_t>(u_size),
                std::vector<int32_t>(static_cast<size_t>(v_size), -1));

            for (int32_t u = 0; u < u_size; ++u) {
                for (int32_t v = 0; v < v_size; ++v) {
                    // Convert (d, u, v) back to (x, y, z).
                    int32_t x, y, z;
                    switch (dir) {
                        case FaceDir::kTop:
                        case FaceDir::kBottom:
                            x = u; y = d; z = v; break;
                        case FaceDir::kPosX:
                        case FaceDir::kNegX:
                            x = d; y = v; z = u; break;
                        case FaceDir::kPosZ:
                        case FaceDir::kNegZ:
                            x = u; y = v; z = d; break;
                        default:
                            x = u; y = d; z = v; break;
                    }

                    const int64_t idx = terrain_index(x, y, z, size_x, size_z);
                    if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) {
                        continue;
                    }
                    const int32_t mat = static_cast<int32_t>(materials[idx]);
                    if (mat == air_material || mat == ladder_material) {
                        continue;
                    }

                    // Check if this face is exposed.
                    godot::Vector3i off = neighbor_offset(dir);
                    int32_t nx = x + off.x;
                    int32_t ny = y + off.y;
                    int32_t nz = z + off.z;
                    int32_t neighbor_material = air_material;
                    const bool has_neighbor = sample_neighbor_material(
                        nx, ny, nz, dir, materials,
                        size_x, size_y, size_z,
                        boundary_materials, boundary_available,
                        neighbor_material);
                    if (!should_emit_render_face(
                            mat, has_neighbor, neighbor_material, dir,
                            air_material, ladder_material,
                            transparent_material_mask)) {
                        continue;
                    }

                    mask[static_cast<size_t>(u)][static_cast<size_t>(v)] = mat;
                }
            }

            // Greedy merge: scan the mask, find rectangles of same material.
            std::vector<std::vector<bool>> visited(
                static_cast<size_t>(u_size),
                std::vector<bool>(static_cast<size_t>(v_size), false));

            for (int32_t u = 0; u < u_size; ++u) {
                for (int32_t v = 0; v < v_size; ++v) {
                    if (visited[static_cast<size_t>(u)][static_cast<size_t>(v)]) continue;
                    const int32_t mat = mask[static_cast<size_t>(u)][static_cast<size_t>(v)];
                    if (mat < 0) continue;

                    // Extend along v (secondary axis) as far as possible.
                    int32_t v_end = v + 1;
                    while (v_end < v_size &&
                           !visited[static_cast<size_t>(u)][static_cast<size_t>(v_end)] &&
                           mask[static_cast<size_t>(u)][static_cast<size_t>(v_end)] == mat) {
                        ++v_end;
                    }

                    // Extend along u (primary axis) as far as possible.
                    int32_t u_end = u + 1;
                    bool can_extend = true;
                    while (can_extend && u_end < u_size) {
                        for (int32_t vv = v; vv < v_end; ++vv) {
                            if (visited[static_cast<size_t>(u_end)][static_cast<size_t>(vv)] ||
                                mask[static_cast<size_t>(u_end)][static_cast<size_t>(vv)] != mat) {
                                can_extend = false;
                                break;
                            }
                        }
                        if (can_extend) ++u_end;
                    }

                    // Mark visited.
                    for (int32_t uu = u; uu < u_end; ++uu) {
                        for (int32_t vv = v; vv < v_end; ++vv) {
                            visited[static_cast<size_t>(uu)][static_cast<size_t>(vv)] = true;
                        }
                    }

                    // Emit the merged quad.
                    auto& accum = accum_by_material[mat];
                    // Compute variant hash from the quad's origin position.
                    // Convert (d, u, v) back to (x, y, z) for consistent hashing.
                    int32_t hx, hy, hz;
                    switch (dir) {
                        case FaceDir::kTop:
                        case FaceDir::kBottom:
                            hx = u; hy = d; hz = v; break;
                        case FaceDir::kPosX:
                        case FaceDir::kNegX:
                            hx = d; hy = v; hz = u; break;
                        case FaceDir::kPosZ:
                        case FaceDir::kNegZ:
                            hx = u; hy = v; hz = d; break;
                        default:
                            hx = u; hy = d; hz = v; break;
                    }
                    float vhash = position_hash(hx, hy, hz);
                    emit_face_quad(accum, dir, d, u, u_end, v, v_end, vhash);
                }
            }
        }
    }

    // Build result dictionary: material_id -> mesh data.
    godot::Dictionary result;
    for (auto& [mat, accum] : accum_by_material) {
        result[mat] = accum.to_dict();
    }
    return result;
}

godot::Dictionary GDChunkHelper::build_collision_faces(
        const godot::PackedByteArray& materials,
        int32_t size_x, int32_t size_y, int32_t size_z,
        const godot::PackedByteArray& collidable_material_mask) {
    std::vector<godot::Vector3> verts;
    std::vector<int32_t> indices;
    int32_t vertex_count = 0;

    for (int32_t y = 0; y < size_y; ++y) {
        for (int32_t z = 0; z < size_z; ++z) {
            for (int32_t x = 0; x < size_x; ++x) {
                const int64_t idx = terrain_index(x, y, z, size_x, size_z);
                if (idx < 0 || idx >= static_cast<int64_t>(materials.size())) continue;
                const int32_t mat = static_cast<int32_t>(materials[idx]);
                if (!is_collidable_material(mat, collidable_material_mask)) continue;

                // For each of 6 faces, check if exposed.
                for (int dir = 0; dir < FaceDir::kCount; ++dir) {
                    godot::Vector3i off = neighbor_offset(dir);
                    if (!is_open_for_collision(
                            x + off.x, y + off.y, z + off.z,
                            materials, size_x, size_y, size_z,
                            collidable_material_mask)) {
                        continue;
                    }

                    // Emit 4 vertices and 2 triangles for this face.
                    const float fx = static_cast<float>(x);
                    const float fy = static_cast<float>(y);
                    const float fz = static_cast<float>(z);

                    godot::Vector3 v0, v1, v2, v3;
                    switch (dir) {
                        case FaceDir::kTop:
                            v0 = godot::Vector3(fx, fy + 1, fz);
                            v1 = godot::Vector3(fx + 1, fy + 1, fz);
                            v2 = godot::Vector3(fx + 1, fy + 1, fz + 1);
                            v3 = godot::Vector3(fx, fy + 1, fz + 1);
                            break;
                        case FaceDir::kBottom:
                            v0 = godot::Vector3(fx, fy, fz + 1);
                            v1 = godot::Vector3(fx + 1, fy, fz + 1);
                            v2 = godot::Vector3(fx + 1, fy, fz);
                            v3 = godot::Vector3(fx, fy, fz);
                            break;
                        case FaceDir::kPosX:
                            v0 = godot::Vector3(fx + 1, fy, fz + 1);
                            v1 = godot::Vector3(fx + 1, fy + 1, fz + 1);
                            v2 = godot::Vector3(fx + 1, fy + 1, fz);
                            v3 = godot::Vector3(fx + 1, fy, fz);
                            break;
                        case FaceDir::kNegX:
                            v0 = godot::Vector3(fx, fy, fz);
                            v1 = godot::Vector3(fx, fy + 1, fz);
                            v2 = godot::Vector3(fx, fy + 1, fz + 1);
                            v3 = godot::Vector3(fx, fy, fz + 1);
                            break;
                        case FaceDir::kPosZ:
                            v0 = godot::Vector3(fx, fy, fz + 1);
                            v1 = godot::Vector3(fx, fy + 1, fz + 1);
                            v2 = godot::Vector3(fx + 1, fy + 1, fz + 1);
                            v3 = godot::Vector3(fx + 1, fy, fz + 1);
                            break;
                        case FaceDir::kNegZ:
                            v0 = godot::Vector3(fx + 1, fy, fz);
                            v1 = godot::Vector3(fx + 1, fy + 1, fz);
                            v2 = godot::Vector3(fx, fy + 1, fz);
                            v3 = godot::Vector3(fx, fy, fz);
                            break;
                    }

                    verts.push_back(v0);
                    verts.push_back(v1);
                    verts.push_back(v2);
                    verts.push_back(v3);
                    indices.push_back(vertex_count);
                    indices.push_back(vertex_count + 1);
                    indices.push_back(vertex_count + 2);
                    indices.push_back(vertex_count);
                    indices.push_back(vertex_count + 2);
                    indices.push_back(vertex_count + 3);
                    vertex_count += 4;
                }
            }
        }
    }

    godot::Dictionary result;
    godot::PackedVector3Array verts_packed;
    godot::PackedInt32Array idx_packed;
    verts_packed.resize(static_cast<int64_t>(verts.size()));
    idx_packed.resize(static_cast<int64_t>(indices.size()));
    for (size_t i = 0; i < verts.size(); ++i) {
        verts_packed.ptrw()[i] = verts[i];
    }
    for (size_t i = 0; i < indices.size(); ++i) {
        idx_packed.ptrw()[i] = indices[i];
    }
    result["vertices"] = verts_packed;
    result["indices"] = idx_packed;
    return result;
}

// --- Bindings ---

void GDChunkHelper::_bind_methods() {
    using B = GDChunkHelper;

    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("world_position_to_cell", "world_position"),
                                &B::world_position_to_cell);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("cell_to_world_position", "cell"),
                                &B::cell_to_world_position);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("cell_to_chunk", "cell", "chunk_size"),
                                &B::cell_to_chunk);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("cell_to_local", "cell", "chunk_size"),
                                &B::cell_to_local);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("world_position_to_chunk", "world_position", "chunk_size"),
                                &B::world_position_to_chunk);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("terrain_index", "local_x", "local_y", "local_z", "size_x", "size_z"),
                                &B::terrain_index);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("is_surface_voxel", "local", "materials", "size_x", "size_y", "size_z", "air_material", "ladder_material"),
                                &B::is_surface_voxel);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("ladder_facing", "local", "materials", "size_x", "size_y", "size_z", "air_material"),
                                &B::ladder_facing);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("compute_surface_mask", "materials", "size_x", "size_y", "size_z", "air_material", "ladder_material"),
                                &B::compute_surface_mask);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD(
        "build_greedy_mesh", "materials", "size_x", "size_y", "size_z",
        "air_material", "ladder_material", "transparent_material_mask",
        "neighbor_materials"),
                                &B::build_greedy_mesh);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("build_collision_faces", "materials", "size_x", "size_y", "size_z", "collidable_material_mask"),
                                &B::build_collision_faces);
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("compute_visible_chunks", "player_chunk", "loaded_radius", "view_radius", "use_spherical_loading"),
                                &B::compute_visible_chunks);
}

} // namespace science_and_theology
