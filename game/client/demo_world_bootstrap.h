// Development world bootstrap owned by the ScienceAndTheology game session.

#pragma once

#include "core/expected.h"
#include "game_session_config.h"

namespace snt::data { class ChunkRegistry; }
namespace snt::voxel { class ChunkRenderSystem; }

namespace snt::game {

snt::core::Expected<void> bootstrap_demo_world(const GameDemoConfig& config,
                                                snt::data::ChunkRegistry& chunk_registry,
                                                snt::voxel::ChunkRenderSystem& chunk_render_system);

}  // namespace snt::game
