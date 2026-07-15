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
}

namespace snt::game {

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
    [[nodiscard]] snt::core::Expected<bool> load_existing(
        snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars) const;

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
