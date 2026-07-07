// ECS components — data-only structs attached to entities.
//
// P1.5: Transform (position/rotation/scale) + MeshRef (path to .obj file).
// P2.4: MeshRef now stores a MeshHandle (uint32_t) into MeshCache instead
//        of a file path. This decouples components from string lookups
//        every frame + makes mesh references stable across cache rebuilds.
// P2.F: MeshHandle is now an alias for the canonical snt::assets::MeshHandle
//        (defined in assets/asset_handle.h). One handle type across the
//        engine — no more layout-duplicated ecs::MeshHandle / render::MeshHandle.
// P2+ will add: Velocity, Collider, Health, Inventory, etc.

#pragma once

#include "assets/asset_handle.h"  // MeshHandle = snt::assets::MeshHandle

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

// MeshHandle: alias for the canonical snt::assets::MeshHandle. Defined
// in assets/asset_handle.h; aliased here so ECS code can write `MeshHandle`
// without the assets:: prefix. Components stay free of render-backend
// deps (assets/ depends only on core/).
using MeshHandle = snt::assets::MeshHandle;

// MeshRef: reference to a mesh asset via an AssetManager mesh handle.
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
