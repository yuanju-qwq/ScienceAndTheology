// Game-owned automation-controller placement content registry.
//
// This module maps a durable inventory item identity to one independently
// owned automation-controller block. It deliberately does not share the
// processing-machine registry: controller blocks own topology/program state,
// not MachineRuntimeComponent state.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"
#include "script/content_host.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct AutomationControllerPlacementDefinition {
    std::string item_id;
    std::string controller_key;
    AutomationControllerKind kind = AutomationControllerKind::kSfmManager;
    // Authored content uses a semantic terrain key; the worldgen snapshot
    // resolves the compact terrain id after the full catalog is frozen.
    std::string material_key;
};

class AutomationControllerPlacementRegistry final {
public:
    AutomationControllerPlacementRegistry() = default;
    ~AutomationControllerPlacementRegistry() = default;

    AutomationControllerPlacementRegistry(const AutomationControllerPlacementRegistry&) = delete;
    AutomationControllerPlacementRegistry& operator=(
        const AutomationControllerPlacementRegistry&) = delete;

    [[nodiscard]] snt::core::Expected<void> register_builtin(
        AutomationControllerPlacementDefinition definition);
    [[nodiscard]] snt::core::Expected<void> register_script(
        snt::script::ScriptId script_id,
        AutomationControllerPlacementDefinition definition);

    [[nodiscard]] const AutomationControllerPlacementDefinition* find_by_item(
        std::string_view item_id) const noexcept;
    [[nodiscard]] const AutomationControllerPlacementDefinition* find_by_material_key(
        std::string_view material_key) const noexcept;
    [[nodiscard]] std::vector<AutomationControllerPlacementDefinition> definitions() const;

    [[nodiscard]] snt::core::Expected<void> begin_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> commit_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> rollback_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> begin_reload_batch(
        std::span<const snt::script::ScriptId> script_ids);
    [[nodiscard]] snt::core::Expected<void> commit_reload_batch(
        std::span<const snt::script::ScriptId> script_ids);
    [[nodiscard]] snt::core::Expected<void> rollback_reload_batch(
        std::span<const snt::script::ScriptId> script_ids);
    [[nodiscard]] snt::core::Expected<void> unload_script(snt::script::ScriptId script_id);
    void reset() noexcept;

private:
    struct OwnedDefinition {
        snt::script::ScriptId owner = snt::script::kBuiltinScriptId;
        AutomationControllerPlacementDefinition definition;
    };

    using DefinitionMap = std::map<std::string, OwnedDefinition, std::less<>>;

    [[nodiscard]] snt::core::Expected<void> register_definition(
        snt::script::ScriptId owner,
        AutomationControllerPlacementDefinition definition,
        bool builtin);
    [[nodiscard]] static snt::core::Expected<void> validate(
        const AutomationControllerPlacementDefinition& definition);
    [[nodiscard]] DefinitionMap snapshot_script_definitions(
        snt::script::ScriptId script_id) const;
    void erase_script_definitions(snt::script::ScriptId script_id);
    void restore_script_definitions(const DefinitionMap& snapshot);

    DefinitionMap backup_definitions_;
    DefinitionMap live_definitions_;
    std::map<snt::script::ScriptId, DefinitionMap> reloads_;
};

}  // namespace snt::game
