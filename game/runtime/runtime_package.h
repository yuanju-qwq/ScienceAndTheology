// Packaged game-runtime bootstrap contract.
//
// The host owns package discovery and translates its executable location into
// explicit engine/game/user roots. Both graphical and dedicated hosts use this
// helper so SimulationRuntime never needs to know a repository or executable
// name.

#pragma once

#include "core/expected.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"
#include "game/client/game_session_config.h"

namespace snt::game {

struct RuntimePackage {
    snt::core::RuntimePaths paths;
    snt::core::RuntimeConfig runtime_config;
    GameSessionConfig session_config;
};

// Resolves the package next to argv0 and loads its engine- and game-owned
// configuration. The returned paths are not process-global state.
[[nodiscard]] snt::core::Expected<RuntimePackage> load_runtime_package(const char* argv0);

}  // namespace snt::game
