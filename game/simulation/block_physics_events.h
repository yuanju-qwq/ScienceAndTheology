// Narrow game-owned block-physics integration contracts.
//
// Terrain physics stays in the shared simulation target, while host command
// intake and replication remain server composition concerns. These value-only
// contracts let those modules communicate without reintroducing WorldData,
// EventBus, or transport dependencies into the simulation core.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

struct BlockPhysicsTerrainChange {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    uint32_t previous_material = 0;
    uint32_t previous_flags = 0;
    uint32_t current_material = 0;
    uint32_t current_flags = 0;
};

// Server replication and future local-presentation adapters consume only
// committed terrain values. They never receive mutable chunk ownership.
class IBlockPhysicsMutationSink {
public:
    virtual ~IBlockPhysicsMutationSink() = default;
    virtual void on_block_physics_terrain_changed(
        const BlockPhysicsTerrainChange& change) = 0;
};

// Authoritative terrain writers schedule delayed physics work through this
// contract after their own mutation has committed successfully.
class IBlockPhysicsTrigger {
public:
    virtual ~IBlockPhysicsTrigger() = default;
    virtual void schedule_block_physics_after_terrain_mutation(
        std::string_view dimension_id,
        int32_t block_x,
        int32_t block_y,
        int32_t block_z,
        uint64_t source_tick) = 0;
};

}  // namespace snt::game
