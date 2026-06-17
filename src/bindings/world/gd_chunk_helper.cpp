#include "gd_chunk_helper.h"

#include <cmath>
#include <algorithm>
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
    godot::ClassDB::bind_static_method("GDChunkHelper", godot::D_METHOD("compute_visible_chunks", "player_chunk", "loaded_radius", "view_radius", "use_spherical_loading"),
                                &B::compute_visible_chunks);
}

} // namespace science_and_theology
