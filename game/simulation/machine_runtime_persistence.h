// Chunk-anchored machine runtime persistence boundary.
//
// Ownership: game-world sidecars own serialized values; this stateless helper
// is called only by the shared simulation-session composition layer. It is the
// sole bridge between a MACHINE BlockEntityPlacement anchor and an ECS
// MachineRuntimeComponent.
//
// Thread affinity: all functions run on the simulation main thread, outside
// worker capture/barrier execution and outside fixed-tick file I/O.
//
// Dependencies: storage stays value-only in game/world; this module depends
// on ECS solely to create, restore, capture, and remove runtime components.

#pragma once

#include "core/expected.h"
#include "ecs/entity_guid.h"
#include "game/world/game_chunk.h"

namespace snt::ecs {
class World;
}

namespace snt::game {

struct MachineRuntimeComponent;

class GameMachineRuntimePersistence final {
public:
    // Adds a runtime component for an existing MACHINE BlockEntityPlacement in
    // the given chunk sidecar. The returned EntityGuid is saved with the
    // record and restored exactly on later sessions.
    [[nodiscard]] static snt::core::Expected<snt::ecs::EntityGuid> create_anchored_machine(
        snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key,
        EntityId anchor_entity_id,
        MachineRuntimeComponent runtime);

    // Removes both the ECS component/entity and its anchor record. Callers
    // must use this lifecycle operation instead of destroying an anchored
    // machine directly, otherwise capture() deliberately refuses to save.
    [[nodiscard]] static snt::core::Expected<void> remove_anchored_machine(
        snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars,
        snt::ecs::EntityGuid entity_guid);

    // Rebuilds all persisted machines after chunk sidecars are loaded and
    // before gameplay begins. Any corrupt, duplicate, unanchored, or Guid-
    // colliding record rejects the entire world session.
    [[nodiscard]] static snt::core::Expected<void> restore(
        snt::ecs::World& world,
        const GameChunkSidecarRegistry& sidecars);

    // Copies every anchored ECS machine back into its sidecar record before a
    // controlled world save. It rejects an unanchored live component so a
    // caller cannot silently lose machine state.
    [[nodiscard]] static snt::core::Expected<void> capture(
        const snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars);
};

}  // namespace snt::game
