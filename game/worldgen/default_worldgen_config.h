// Current game-owned world-generation defaults.
//
// This module owns the immutable terrain snapshot shared by bootstrap,
// persistence loading, and deterministic simulation. Content migration can
// replace these defaults with package-backed definitions without changing
// those consumers' ownership boundary.

#pragma once

#include "game/worldgen/world_gen_config.h"

#include <memory>

namespace snt::game {

[[nodiscard]] std::shared_ptr<const WorldGenConfigSnapshot>
make_default_game_worldgen_config();

}  // namespace snt::game
