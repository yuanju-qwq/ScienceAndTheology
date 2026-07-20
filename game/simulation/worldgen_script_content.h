// World-generation script snapshot construction.
//
// The AngelScript module owns authored terrain semantics. This boundary keeps
// the mutable registration draft on the simulation main thread, then freezes
// it into the immutable WorldGenConfigSnapshot consumed by worker systems.

#pragma once

#include <memory>

#include "core/expected.h"

class asIScriptEngine;

namespace snt::script {
class ScriptManager;
}

namespace snt::game {

struct WorldGenConfigSnapshot;

// Registers the native declarations used exclusively by
// `void snt_register_worldgen()`. GameContentRegistry invokes this while the
// shared AngelScript engine is initialized; calling the declarations outside
// a world-generation build raises an AngelScript exception.
[[nodiscard]] snt::core::Expected<void> register_worldgen_script_api(
    asIScriptEngine* engine);

// Builds a fresh immutable world-generation snapshot from the currently
// committed `50_worldgen_catalog` module. The caller owns the resulting
// snapshot and may publish it only at a world/session creation boundary.
//
// All authored references remain semantic keys while the script runs. The
// builder resolves runtime material IDs exactly once during finalization, so
// future biome, ore, rock, and planet declarations can use the same draft API
// without exposing transient IDs to scripts.
[[nodiscard]] snt::core::Expected<std::shared_ptr<const WorldGenConfigSnapshot>>
build_worldgen_config_from_script(snt::script::ScriptManager& scripts);

}  // namespace snt::game
