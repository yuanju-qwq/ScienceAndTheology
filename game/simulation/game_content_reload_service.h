// Game-owned content reload planning.
//
// This module maps editor-facing content categories to script modules and
// their downstream dependency closure. It deliberately has no UI dependency:
// a page can enumerate targets and enqueue one through the simulation session,
// while ScriptManager only receives ordered filesystem paths.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/expected.h"
#include "script/file_watcher.h"
#include "script/script_manager.h"

namespace snt::game {

enum class GameContentReloadTarget : uint8_t {
    kAll,
    kMaterials,
    kItems,
    kMachines,
    kRecipes,
    kQuests,
};

struct GameContentReloadTargetInfo {
    GameContentReloadTarget target = GameContentReloadTarget::kAll;
    std::string id;
    std::string display_name;
};

struct GameContentReloadPlan {
    GameContentReloadTarget requested_target = GameContentReloadTarget::kAll;
    std::vector<GameContentReloadTarget> expanded_targets;
    std::vector<std::filesystem::path> files;
};

struct GameContentReloadResult {
    GameContentReloadPlan plan;
    uint64_t elapsed_microseconds = 0;
};

class GameContentReloadService final : public snt::script::IScriptFileChangeHandler {
public:
    GameContentReloadService() = default;
    ~GameContentReloadService() override = default;

    GameContentReloadService(const GameContentReloadService&) = delete;
    GameContentReloadService& operator=(const GameContentReloadService&) = delete;

    [[nodiscard]] snt::core::Expected<void> configure(std::filesystem::path script_root);
    void reset() noexcept;

    [[nodiscard]] std::vector<GameContentReloadTargetInfo> targets() const;
    [[nodiscard]] snt::core::Expected<GameContentReloadPlan> plan(
        GameContentReloadTarget target) const;
    [[nodiscard]] snt::core::Expected<GameContentReloadResult> reload(
        snt::script::ScriptManager& scripts, GameContentReloadTarget target);

    // Known game modules are expanded through their dependency closure. Other
    // package files return false and keep ScriptManager's generic behavior.
    [[nodiscard]] snt::core::Expected<bool> handle_script_file_change(
        snt::script::ScriptManager& scripts,
        const snt::script::FileChange& change) override;

private:
    [[nodiscard]] std::optional<GameContentReloadTarget> target_for_path(
        const std::filesystem::path& path) const;

    std::filesystem::path script_root_;
};

}  // namespace snt::game
