// Game-owned machine placement content registry implementation.

#define SNT_LOG_CHANNEL "game.machine_placement_registry"
#include "game/simulation/machine_placement_registry.h"

#include "core/error.h"
#include "core/log.h"

#include <limits>
#include <string>
#include <utility>

namespace snt::game {
namespace {

constexpr size_t kMaxPlacementIdentifierBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_valid_identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= kMaxPlacementIdentifierBytes &&
           value.find('\0') == std::string_view::npos;
}

}  // namespace

snt::core::Expected<void> MachinePlacementRegistry::register_builtin(
    MachinePlacementDefinition definition) {
    return register_definition(snt::script::kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> MachinePlacementRegistry::register_script(
    snt::script::ScriptId script_id, MachinePlacementDefinition definition) {
    return register_definition(script_id, std::move(definition), false);
}

const MachinePlacementDefinition* MachinePlacementRegistry::find_by_item(
    std::string_view item_id) const noexcept {
    const auto it = live_definitions_.find(item_id);
    return it == live_definitions_.end() ? nullptr : &it->second.definition;
}

const MachinePlacementDefinition* MachinePlacementRegistry::find_by_material(
    uint32_t material_id) const noexcept {
    for (const auto& [item_id, entry] : live_definitions_) {
        (void)item_id;
        if (entry.definition.material_id == material_id) return &entry.definition;
    }
    return nullptr;
}

std::vector<MachinePlacementDefinition> MachinePlacementRegistry::definitions() const {
    std::vector<MachinePlacementDefinition> result;
    result.reserve(live_definitions_.size());
    for (const auto& [item_id, entry] : live_definitions_) {
        (void)item_id;
        result.push_back(entry.definition);
    }
    return result;
}

snt::core::Expected<void> MachinePlacementRegistry::begin_reload(
    snt::script::ScriptId script_id) {
    if (script_id == snt::script::kBuiltinScriptId) {
        return invalid_argument("Built-in machine placement content cannot be reloaded as a script");
    }
    if (reloads_.contains(script_id)) {
        return invalid_state("Machine placement reload is already active for script");
    }

    reloads_.emplace(script_id, snapshot_script_definitions(script_id));
    erase_script_definitions(script_id);
    SNT_LOG_DEBUG("Machine placement content began transactional reload for script %llu",
                  static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> MachinePlacementRegistry::commit_reload(
    snt::script::ScriptId script_id) {
    const auto it = reloads_.find(script_id);
    if (it == reloads_.end()) {
        return invalid_state("No active machine placement reload for script");
    }
    reloads_.erase(it);
    SNT_LOG_INFO("Machine placement content committed reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> MachinePlacementRegistry::rollback_reload(
    snt::script::ScriptId script_id) {
    const auto it = reloads_.find(script_id);
    if (it == reloads_.end()) {
        return invalid_state("No active machine placement reload for script");
    }

    erase_script_definitions(script_id);
    restore_script_definitions(it->second);
    reloads_.erase(it);
    SNT_LOG_WARN("Machine placement content rolled back reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> MachinePlacementRegistry::unload_script(
    snt::script::ScriptId script_id) {
    if (script_id == snt::script::kBuiltinScriptId) {
        return invalid_argument("Built-in machine placement content cannot be unloaded as a script");
    }
    if (reloads_.contains(script_id)) {
        return invalid_state("Cannot unload machine placement content during its active reload");
    }
    erase_script_definitions(script_id);
    SNT_LOG_INFO("Machine placement content unloaded script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

void MachinePlacementRegistry::reset() noexcept {
    backup_definitions_.clear();
    live_definitions_.clear();
    reloads_.clear();
}

snt::core::Expected<void> MachinePlacementRegistry::register_definition(
    snt::script::ScriptId owner, MachinePlacementDefinition definition, bool builtin) {
    if (auto result = validate(definition); !result) return result.error();
    if (builtin != (owner == snt::script::kBuiltinScriptId)) {
        return invalid_argument("Built-in machine placement registrations must use ScriptId 0");
    }

    const std::string item_id = definition.item_id;
    const auto existing = live_definitions_.find(item_id);
    if (!builtin && existing != live_definitions_.end() &&
        existing->second.owner != snt::script::kBuiltinScriptId &&
        existing->second.owner != owner) {
        return invalid_state("Machine placement item id is already owned by another script: " + item_id);
    }
    if (builtin && existing != live_definitions_.end() &&
        existing->second.owner != snt::script::kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script machine placement with a built-in placement: " +
                             item_id);
    }

    const auto find_material_collision = [&definition, &item_id](
                                             const DefinitionMap& definitions)
        -> const std::string* {
        for (const auto& [registered_item_id, entry] : definitions) {
            if (registered_item_id != item_id &&
                entry.definition.material_id == definition.material_id) {
                return &registered_item_id;
            }
        }
        return nullptr;
    };
    if (const std::string* collision = find_material_collision(live_definitions_)) {
        return invalid_state("Machine placement material id is already registered by item: " +
                             *collision);
    }
    if (const std::string* collision = find_material_collision(backup_definitions_)) {
        return invalid_state("Machine placement material id is reserved by built-in item: " +
                             *collision);
    }
    for (const auto& [reloading_script_id, snapshot] : reloads_) {
        if (reloading_script_id == owner) continue;
        if (snapshot.contains(item_id)) {
            return invalid_state("Machine placement item id is reserved by an active script reload: " +
                                 item_id);
        }
        if (const std::string* collision = find_material_collision(snapshot)) {
            return invalid_state(
                "Machine placement material id is reserved by an active script reload item: " +
                *collision);
        }
    }

    OwnedDefinition entry{owner, std::move(definition)};
    if (builtin) backup_definitions_[item_id] = entry;
    live_definitions_[item_id] = std::move(entry);
    return {};
}

snt::core::Expected<void> MachinePlacementRegistry::validate(
    const MachinePlacementDefinition& definition) {
    if (!is_valid_identifier(definition.item_id)) {
        return invalid_argument("Machine placement item id is invalid");
    }
    if (!is_valid_identifier(definition.machine_id)) {
        return invalid_argument("Machine placement machine id is invalid");
    }
    if (definition.material_id == 0 ||
        definition.material_id > std::numeric_limits<uint8_t>::max()) {
        return invalid_argument("Machine placement material id must be a non-air terrain material");
    }
    return {};
}

MachinePlacementRegistry::DefinitionMap
MachinePlacementRegistry::snapshot_script_definitions(snt::script::ScriptId script_id) const {
    DefinitionMap snapshot;
    for (const auto& [item_id, entry] : live_definitions_) {
        if (entry.owner == script_id) snapshot.emplace(item_id, entry);
    }
    return snapshot;
}

void MachinePlacementRegistry::erase_script_definitions(snt::script::ScriptId script_id) {
    for (auto it = live_definitions_.begin(); it != live_definitions_.end();) {
        if (it->second.owner != script_id) {
            ++it;
            continue;
        }
        const auto fallback = backup_definitions_.find(it->first);
        if (fallback == backup_definitions_.end()) {
            it = live_definitions_.erase(it);
        } else {
            it->second = fallback->second;
            ++it;
        }
    }
}

void MachinePlacementRegistry::restore_script_definitions(const DefinitionMap& snapshot) {
    for (const auto& [item_id, entry] : snapshot) {
        live_definitions_[item_id] = entry;
    }
}

}  // namespace snt::game
