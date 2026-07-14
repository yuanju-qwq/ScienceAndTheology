// Development world bootstrap owned by the ScienceAndTheology game session.

#pragma once

#include "core/expected.h"
#include "game_session_config.h"

namespace snt::voxel { class ChunkRegistry; }

namespace snt::game {

class GameChunkSidecarRegistry;

snt::core::Expected<void> bootstrap_demo_world(const GameDemoConfig& config,
                                                snt::voxel::ChunkRegistry& chunk_registry,
                                                GameChunkSidecarRegistry& sidecars);

}  // namespace snt::game
