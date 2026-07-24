// Game-owned world persistence lifecycle.
//
// This module composes the existing GameSaveManager region codec with an
// explicit universe root and dimension identity. It deliberately does not
// know about ScriptManager, ECS entities, transport, or fixed ticks: a host
// calls load_existing() during world creation and save() during controlled
// session shutdown.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <string>

namespace snt::voxel {
class ChunkRegistry;
struct ChunkKey;
}

namespace snt::game {

struct GameChunkSidecar;
class GameChunkSidecarRegistry;

struct GameWorldPersistenceDescriptor {
    std::string universe_save_dir;
    std::string dimension_id;
    int64_t seed = 0;
    std::string universe_mode;
};

class GameWorldPersistenceLifecycle final {
public:
    explicit GameWorldPersistenceLifecycle(GameWorldPersistenceDescriptor descriptor);

    GameWorldPersistenceLifecycle(const GameWorldPersistenceLifecycle&) = delete;
    GameWorldPersistenceLifecycle& operator=(const GameWorldPersistenceLifecycle&) = delete;

    // Returns false only when no universe exists yet. A recognized universe
    // with zero region chunks still returns true so the caller never silently
    // overwrites an intentionally empty current-format world with demo data.
    // This restores a semantic sidecar index, not terrain; ticket streaming
    // materializes terrain through load_chunk_terrain() on demand.
    [[nodiscard]] snt::core::Expected<bool> load_existing(
        GameChunkSidecarRegistry& sidecars) const;

    // Loads one persisted terrain payload without replacing the live sidecar
    // state. `false` indicates that this coordinate has not been persisted.
    [[nodiscard]] snt::core::Expected<bool> load_chunk_terrain(
        snt::voxel::ChunkRegistry& chunks,
        const snt::voxel::ChunkKey& chunk_key) const;

    // Persists a terrain-resident chunk while retaining every other entry in
    // its region file. Ticket streamers call this before terrain removal.
    [[nodiscard]] snt::core::Expected<void> save_loaded_chunk(
        const snt::voxel::ChunkRegistry& chunks,
        const GameChunkSidecarRegistry& sidecars,
        const snt::voxel::ChunkKey& chunk_key) const;

    // Persists semantic state for a terrain-dematerialized chunk. This is the
    // durable path for offline machines and network-island snapshots.
    [[nodiscard]] snt::core::Expected<void> save_chunk_sidecar(
        const snt::voxel::ChunkKey& chunk_key,
        const GameChunkSidecar& sidecar) const;

    // Commits one current chunk image and then the universe header. This is
    // the narrow synchronous checkpoint used by a cross-file gameplay
    // transaction whose recovery journal may not be discarded until the
    // sidecar mutation is independently durable.
    [[nodiscard]] snt::core::Expected<void> checkpoint_chunk(
        const snt::voxel::ChunkRegistry& chunks,
        const GameChunkSidecarRegistry& sidecars,
        const snt::voxel::ChunkKey& chunk_key) const;

    // Writes the current dimension first, then commits the universe header.
    // Callers keep this out of fixed ticks and surface any returned error at a
    // lifecycle boundary rather than replacing a corrupt world with a new one.
    [[nodiscard]] snt::core::Expected<void> save(
        const snt::voxel::ChunkRegistry& chunks,
        const GameChunkSidecarRegistry& sidecars) const;

    [[nodiscard]] const GameWorldPersistenceDescriptor& descriptor() const noexcept {
        return descriptor_;
    }

private:
    [[nodiscard]] snt::core::Expected<void> validate_descriptor() const;

    GameWorldPersistenceDescriptor descriptor_;
};

}  // namespace snt::game
