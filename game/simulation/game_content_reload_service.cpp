// Game-owned content reload planning implementation.

#define SNT_LOG_CHANNEL "game.content_reload"
#include "game/simulation/game_content_reload_service.h"

#include "core/error.h"
#include "core/log.h"

#include <array>
#include <chrono>
#include <string_view>
#include <utility>

namespace snt::game {
namespace {

using Target = GameContentReloadTarget;

struct ModuleDescriptor {
    Target target;
    std::string_view id;
    std::string_view display_name;
    std::string_view file_name;
};

constexpr std::array<ModuleDescriptor, 6> kModules{{
    {Target::kMaterials, "materials", "Materials", "00_material_catalog.as"},
    {Target::kItems, "items", "Items", "10_item_catalog.as"},
    {Target::kMachines, "machines", "Machines", "20_machine_catalog.as"},
    {Target::kRecipes, "recipes", "Recipes", "30_recipe_catalog.as"},
    {Target::kQuests, "quests", "Quests", "40_quest_catalog.as"},
    {Target::kWorldGeneration, "worldgen", "World Generation", "50_worldgen_catalog.as"},
}};

constexpr size_t kModuleCount = kModules.size();

[[nodiscard]] std::optional<size_t> module_index(Target target) {
    for (size_t index = 0; index < kModules.size(); ++index) {
        if (kModules[index].target == target) return index;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view target_id(Target target) {
    if (target == Target::kAll) return "all";
    const auto index = module_index(target);
    return index ? kModules[*index].id : "unknown";
}

void select_target_and_dependents(Target target, std::array<bool, kModuleCount>& selected) {
    const auto index = module_index(target);
    if (!index || selected[*index]) return;
    selected[*index] = true;

    switch (target) {
        case Target::kMaterials:
            select_target_and_dependents(Target::kItems, selected);
            break;
        case Target::kItems:
            select_target_and_dependents(Target::kMachines, selected);
            select_target_and_dependents(Target::kRecipes, selected);
            select_target_and_dependents(Target::kQuests, selected);
            break;
        case Target::kMachines:
            select_target_and_dependents(Target::kRecipes, selected);
            break;
        case Target::kRecipes:
        case Target::kQuests:
        case Target::kWorldGeneration:
        case Target::kAll:
            break;
    }
}

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

snt::core::Expected<void> GameContentReloadService::configure(
    std::filesystem::path script_root) {
    if (script_root.empty()) {
        return invalid_argument("Game content reload service requires a script root");
    }
    std::error_code error_code;
    script_root_ = std::filesystem::absolute(std::move(script_root), error_code).lexically_normal();
    if (error_code) {
        script_root_.clear();
        return invalid_argument("Game content reload service could not normalize the script root");
    }
    return {};
}

void GameContentReloadService::reset() noexcept {
    script_root_.clear();
}

std::vector<GameContentReloadTargetInfo> GameContentReloadService::targets() const {
    std::vector<GameContentReloadTargetInfo> result;
    result.reserve(kModules.size() + 1);
    result.push_back({Target::kAll, "all", "All Content"});
    for (const ModuleDescriptor& module : kModules) {
        result.push_back({module.target, std::string(module.id), std::string(module.display_name)});
    }
    return result;
}

snt::core::Expected<GameContentReloadPlan> GameContentReloadService::plan(Target target) const {
    if (script_root_.empty()) {
        return invalid_state("Game content reload service is not configured");
    }

    std::array<bool, kModuleCount> selected{};
    if (target == Target::kAll) {
        selected.fill(true);
    } else if (module_index(target)) {
        select_target_and_dependents(target, selected);
    } else {
        return invalid_argument("Unknown game content reload target");
    }

    GameContentReloadPlan result;
    result.requested_target = target;
    for (size_t index = 0; index < kModules.size(); ++index) {
        if (!selected[index]) continue;
        result.expanded_targets.push_back(kModules[index].target);
        result.files.push_back(script_root_ / kModules[index].file_name);
    }
    return result;
}

snt::core::Expected<GameContentReloadResult> GameContentReloadService::reload(
    snt::script::ScriptManager& scripts, Target target) {
    auto reload_plan = plan(target);
    if (!reload_plan) {
        auto error = reload_plan.error();
        error.with_context("GameContentReloadService::plan(" +
                           std::string(target_id(target)) + ")");
        SNT_LOG_WARN("Game content reload planning failed target=%.*s: %s",
                     static_cast<int>(target_id(target).size()), target_id(target).data(),
                     error.format().c_str());
        return error;
    }

    SNT_LOG_INFO("Game content reload requested target=%.*s modules=%zu",
                 static_cast<int>(target_id(target).size()), target_id(target).data(),
                 reload_plan->files.size());
    const auto started = std::chrono::steady_clock::now();
    if (auto result = scripts.reload_files(reload_plan->files); !result) {
        auto error = result.error();
        error.with_context("GameContentReloadService::reload(" +
                           std::string(target_id(target)) + ")");
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        SNT_LOG_WARN("Game content reload failed target=%.*s modules=%zu elapsed_ms=%.2f: %s",
                     static_cast<int>(target_id(target).size()), target_id(target).data(),
                     reload_plan->files.size(), static_cast<double>(elapsed) / 1000.0,
                     error.format().c_str());
        return error;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    GameContentReloadResult result;
    result.plan = std::move(*reload_plan);
    result.elapsed_microseconds = static_cast<uint64_t>(elapsed);
    SNT_LOG_INFO("Game content reload completed target=%.*s modules=%zu elapsed_ms=%.2f",
                 static_cast<int>(target_id(target).size()), target_id(target).data(),
                 result.plan.files.size(), static_cast<double>(result.elapsed_microseconds) / 1000.0);
    return result;
}

snt::core::Expected<bool> GameContentReloadService::handle_script_file_change(
    snt::script::ScriptManager& scripts, const snt::script::FileChange& change) {
    const auto target = target_for_path(change.path);
    if (!target) return false;
    if (change.kind == snt::script::FileChangeKind::Removed) {
        return invalid_state("Managed game content module was removed; retaining the last committed "
                             "content until the module is restored: " + change.path.string());
    }
    if (auto result = reload(scripts, *target); !result) return result.error();
    return true;
}

std::optional<GameContentReloadTarget> GameContentReloadService::target_for_path(
    const std::filesystem::path& path) const {
    if (script_root_.empty()) return std::nullopt;
    std::error_code error_code;
    const std::filesystem::path normalized =
        std::filesystem::absolute(path, error_code).lexically_normal();
    if (error_code) return std::nullopt;
    for (const ModuleDescriptor& module : kModules) {
        if (normalized == (script_root_ / module.file_name).lexically_normal()) {
            return module.target;
        }
    }
    return std::nullopt;
}

}  // namespace snt::game
