// Development world bootstrap owned by the ScienceAndTheology game session.

#pragma once

#include "core/expected.h"
#include "game_session_config.h"

#include <memory>

namespace snt::voxel { class ChunkRegistry; }

namespace snt::game {

class GameChunkSidecarRegistry;
struct WorldGenConfigSnapshot;

snt::core::Expected<void> bootstrap_demo_world(const GameDemoConfig& config,
                                                snt::voxel::ChunkRegistry& chunk_registry,
                                                GameChunkSidecarRegistry& sidecars,
                                                std::shared_ptr<const WorldGenConfigSnapshot>
                                                    worldgen_config);

}  // namespace snt::game
