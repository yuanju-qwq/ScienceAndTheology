// World ECS components — data extracted from the legacy WorldData god-object.
//
// P2 task 4: radical refactor. WorldData was a god-object holding chunks,
// gameplay config, tick state, worldgen config, physics events, block
// entities, machine collision, and mobile structures. The refactor splits
// these into:
//   - ChunkRegistry (generic voxel storage)      -> voxel/data/chunk_registry.h
//   - BlockEntityRegistry (block entities)       -> game/world/defs/block_entity_registry.h
//   - MachineCollisionOverlay (machine collision)-> game/world/defs/machine_collision_overlay.h
//   - DynamicStructureRegistry (mobile structs)  -> game/world/mobile/dynamic_structure.h
//   - ECS components below (config/tick/events)  -> this file
//
// These components are attached to a singleton "world context" entity in the
// ECS World. Systems query them via world.ctx<>() (EnTT singleton component).
//
// Thread safety: single-threaded (main thread). Systems that run on the
// worker pool must snapshot these values before dispatch.

#pragma once

#include <cstdint>
#include <memory>

#include "game/world/defs/gameplay_config.h"
#include "game/worldgen/world_gen_config.h"

namespace snt::game {

// ECS component: runtime gameplay configuration (collapse, gravity fall, etc.).
// Mutable at runtime, separate from frozen WorldGenConfigSnapshot.
// Attached as a singleton to the world context entity.
struct GameplayConfigComponent {
    GameplayConfig config;
};

// ECS component: current simulation tick counter.
// Set by TickSystem each frame. Subsystems read this instead of maintaining
// their own counters.
// Attached as a singleton to the world context entity.
struct TickComponent {
    int64_t current_tick = 0;
};

// ECS component: frozen world generation config snapshot reference.
// Provides access to PlanetConfig, material definitions, etc. for physics
// and generation systems. The snapshot is immutable once loaded.
// Attached as a singleton to the world context entity.
struct WorldGenConfigComponent {
    std::shared_ptr<const WorldGenConfigSnapshot> config;
};

} // namespace snt::game
