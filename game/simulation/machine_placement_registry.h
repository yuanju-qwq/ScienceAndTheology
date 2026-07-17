// Game-owned machine placement content registry.
//
// This module owns the mapping from inventory item ids to placeable machine
// types and terrain materials. It deliberately stays separate from
// MachineDefinition: a machine's runtime behavior and its world block forms
// evolve at different rates. Definitions are value-only and participate in
// the same script reload lifecycle as the rest of the game content.

#pragma once

#include "core/expected.h"
#include "script/content_host.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct MachinePlacementDefinition {
    std::string item_id;
    std::string machine_id;
    uint32_t material_id = 0;
};

class MachinePlacementRegistry final {
public:
    MachinePlacementRegistry() = default;
    ~MachinePlacementRegistry() = default;

    MachinePlacementRegistry(const MachinePlacementRegistry&) = delete;
    MachinePlacementRegistry& operator=(const MachinePlacementRegistry&) = delete;

    [[nodiscard]] snt::core::Expected<void> register_builtin(
        MachinePlacementDefinition definition);
    [[nodiscard]] snt::core::Expected<void> register_script(
        snt::script::ScriptId script_id, MachinePlacementDefinition definition);

    [[nodiscard]] const MachinePlacementDefinition* find_by_item(
        std::string_view item_id) const noexcept;
    [[nodiscard]] const MachinePlacementDefinition* find_by_material(
        uint32_t material_id) const noexcept;
    // Stable value snapshot for cross-registry startup validation. Callers
    // cannot mutate the registry through this returned collection.
    [[nodiscard]] std::vector<MachinePlacementDefinition> definitions() const;

    [[nodiscard]] snt::core::Expected<void> begin_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> commit_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> rollback_reload(snt::script::ScriptId script_id);
    [[nodiscard]] snt::core::Expected<void> unload_script(snt::script::ScriptId script_id);
    void reset() noexcept;

private:
    struct OwnedDefinition {
        snt::script::ScriptId owner = snt::script::kBuiltinScriptId;
        MachinePlacementDefinition definition;
    };

    using DefinitionMap = std::map<std::string, OwnedDefinition, std::less<>>;

    [[nodiscard]] snt::core::Expected<void> register_definition(
        snt::script::ScriptId owner, MachinePlacementDefinition definition, bool builtin);
    [[nodiscard]] static snt::core::Expected<void> validate(
        const MachinePlacementDefinition& definition);
    [[nodiscard]] DefinitionMap snapshot_script_definitions(
        snt::script::ScriptId script_id) const;
    void erase_script_definitions(snt::script::ScriptId script_id);
    void restore_script_definitions(const DefinitionMap& snapshot);

    // Built-ins are fallback content for script overrides. The live map is
    // keyed by item id because command handling starts with a selected item.
    DefinitionMap backup_definitions_;
    DefinitionMap live_definitions_;
    std::map<snt::script::ScriptId, DefinitionMap> reloads_;
};

}  // namespace snt::game
