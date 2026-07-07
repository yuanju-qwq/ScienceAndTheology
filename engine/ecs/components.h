// ECS components — data-only structs attached to entities.
//
// P1.5: Transform (position/rotation/scale) + MeshRef (path to .obj file).
// P2.4: MeshRef now stores a MeshHandle (uint32_t) into MeshCache instead
//        of a file path. This decouples components from string lookups
//        every frame + makes mesh references stable across cache rebuilds.
// P2+ will add: Velocity, Collider, Health, Inventory, etc.

#pragma once

#include <cstdint>
#include <string>

namespace snt::ecs {

// Transform: world-space position, rotation (Euler angles in degrees),
// and scale. Stored as separate floats for cache friendliness.
struct Transform {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // Euler degrees: pitch, yaw, roll
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

// MeshHandle forward decl — defined in render/mesh_cache.h.
// We duplicate the type here to keep components.h free of render deps.
// (Same layout: a single uint32_t.)
struct MeshHandle {
    uint32_t id = 0xFFFFFFFFu;
    bool valid() const { return id != 0xFFFFFFFFu; }
};

// MeshRef: reference to a mesh asset via a MeshCache handle.
// The render system resolves the handle to a VulkanMesh* each frame.
struct MeshRef {
    MeshHandle handle;
};

// Camera: marks an entity as a camera. Only one active camera is used.
struct Camera {
    float fov = 60.0f;       // field of view in degrees
    float near_plane = 0.1f;
    float far_plane = 100.0f;
    float aspect = 16.0f / 9.0f;  // updated by window resize
};

}  // namespace snt::ecs
