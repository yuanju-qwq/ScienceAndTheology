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

#include <cstdint>

namespace snt::ecs {
class World;
}

namespace snt::game {

struct MachineRuntimeComponent;

// The durable anchor and transient ECS runtime identity created together for
// one placed machine. Callers keep this value only for the current session;
// chunk persistence reconstructs the same runtime Guid from the sidecar.
struct MachineAnchoredRuntime {
    EntityId anchor_entity_id;
    snt::ecs::EntityGuid entity_guid;
};

class GameMachineRuntimePersistence final {
public:
    // Allocates a MACHINE BlockEntityPlacement and creates its matching ECS
    // runtime record in one lifecycle operation. The root must be owned by
    // chunk_key. The returned Guid is saved with the record and restored
    // exactly on later sessions.
    [[nodiscard]] static snt::core::Expected<MachineAnchoredRuntime> create_anchored_machine(
        snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key,
        int32_t root_x,
        int32_t root_y,
        int32_t root_z,
        MachineRuntimeComponent runtime);

    // Removes the ECS component/entity, runtime record, and MACHINE anchor.
    // Callers must use this lifecycle operation instead of destroying an anchored
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

    // Materializes only kLoaded records owned by one chunk. A streaming
    // coordinator calls this after terrain and its sidecar are available.
    [[nodiscard]] static snt::core::Expected<void> restore_chunk(
        snt::ecs::World& world,
        const GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key);

    // Copies every anchored ECS machine back into its sidecar record before a
    // controlled world save. It rejects an unanchored live component so a
    // caller cannot silently lose machine state.
    [[nodiscard]] static snt::core::Expected<void> capture(
        const snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars);

    // Captures the materialized machine values in one chunk without touching
    // other active chunks. It leaves residency ownership unchanged.
    [[nodiscard]] static snt::core::Expected<void> capture_chunk(
        const snt::ecs::World& world,
        GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key);

    // Removes the ECS runtimes for kLoaded records after capture_chunk has
    // succeeded. The caller must change those records to a non-loaded owner
    // only after this operation completes.
    [[nodiscard]] static snt::core::Expected<void> destroy_chunk_runtimes(
        snt::ecs::World& world,
        const GameChunkSidecarRegistry& sidecars,
        const ChunkKey& chunk_key);

    // Value conversion used by the offline service. These functions never
    // allocate entities or alter residency metadata.
    [[nodiscard]] static MachineRuntimeComponent make_runtime_component(
        const MachineRuntimePersistenceRecord& record);
    static void copy_runtime_to_record(
        MachineRuntimePersistenceRecord& record,
        const MachineRuntimeComponent& runtime);
};

}  // namespace snt::game
