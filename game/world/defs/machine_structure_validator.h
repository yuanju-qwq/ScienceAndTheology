// Game-owned voxel structure validation for machine activation.
//
// This module validates immutable terrain snapshots on the simulation main
// thread. Player commands use its value result to build a trusted
// MachineActivationContext; it deliberately does not know actors, inventory,
// transport, scripts, or ECS runtime components.

#pragma once

#include "game/world/voxel_primitives.h"

#include <cstdint>
#include <string>

namespace snt::game {

enum class MachineStructureFailure : uint8_t {
    None,
    InvalidExpectedMaterial,
    MissingChunk,
    InvalidTerrainCell,
    MaterialMismatch,
};

struct MachineStructureCheck {
    MachineStructureFailure failure = MachineStructureFailure::None;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    TerrainMaterialId actual_material = 0;

    [[nodiscard]] bool valid() const noexcept {
        return failure == MachineStructureFailure::None;
    }
};

class MachineStructureValidator final {
public:
    // The bloomery controller occupies the bottom-center cell. Every other
    // cell in its 3x3x2 enclosure must hold bloomery_material. Material 0 is
    // air in the current game data contract and is never a valid structure
    // material. Missing chunks fail closed instead of treating unloaded world
    // data as an implicitly valid structure.
    [[nodiscard]] static MachineStructureCheck validate_bloomery(
        const ChunkRegistry& chunks,
        const std::string& dimension_id,
        int32_t controller_x,
        int32_t controller_y,
        int32_t controller_z,
        TerrainMaterialId bloomery_material);
};

}  // namespace snt::game
