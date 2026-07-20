// Shared test support for the packaged world-generation script.

#pragma once

#include <filesystem>
#include <memory>

#include "core/expected.h"
#include "game/client/game_content_registry.h"
#include "game/simulation/worldgen_script_content.h"
#include "script/script_manager.h"

namespace snt::game::test {

// Builds the same immutable snapshot used by a packaged game session. Script
// and registry locals deliberately outlive the ScriptManager shutdown; the
// returned snapshot has no AngelScript-owned references.
inline snt::core::Expected<std::shared_ptr<const WorldGenConfigSnapshot>>
build_packaged_worldgen_config() {
    GameContentRegistry content;
    snt::script::ScriptManager scripts;
    if (auto result = scripts.set_content_host(content); !result) return result.error();
    if (auto result = scripts.init(); !result) return result.error();

    const std::filesystem::path script_path =
        std::filesystem::path(SNT_GAME_TEST_ROOT) / "game/scripts/50_worldgen_catalog.as";
    if (auto result = scripts.load_file(script_path.string()); !result) return result.error();
    return build_worldgen_config_from_script(scripts);
}

}  // namespace snt::game::test
