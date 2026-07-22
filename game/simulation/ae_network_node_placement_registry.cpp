// Game-owned AE physical-node placement content registry implementation.

#define SNT_LOG_CHANNEL "game.ae_node_placement_registry"
#include "game/simulation/ae_network_node_placement_registry.h"

#include "core/error.h"
#include "core/log.h"
#include "game/worldgen/world_gen_config.h"

#include <set>
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

[[nodiscard]] bool is_valid_connection_mask(uint8_t mask) noexcept {
    return (mask & static_cast<uint8_t>(~CONN_ALL)) == 0;
}

[[nodiscard]] bool is_valid_drive_storage(
    const AeDriveStorageCellDefinition& definition) noexcept {
    if (definition.byte_capacity <= 0 || definition.max_distinct_resources == 0 ||
        definition.bytes_per_distinct_resource <= 0 ||
        definition.units_per_byte <= 0 ||
        definition.bytes_per_distinct_resource > definition.byte_capacity) {
        return false;
    }
    std::set<std::string, std::less<>> accepted_types;
    for (const std::string& type : definition.accepted_resource_types) {
        if (!is_valid_identifier(type) || !accepted_types.insert(type).second) return false;
    }
    return true;
}

}  // namespace

snt::core::Expected<void> AeNetworkNodePlacementRegistry::register_builtin(
    AeNetworkNodePlacementDefinition definition) {
    return register_definition(snt::script::kBuiltinScriptId, std::move(definition), true);
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::register_script(
    snt::script::ScriptId script_id,
    AeNetworkNodePlacementDefinition definition) {
    return register_definition(script_id, std::move(definition), false);
}

const AeNetworkNodePlacementDefinition* AeNetworkNodePlacementRegistry::find_by_item(
    std::string_view item_id) const noexcept {
    const auto found = live_definitions_.find(item_id);
    return found == live_definitions_.end() ? nullptr : &found->second.definition;
}

const AeNetworkNodePlacementDefinition* AeNetworkNodePlacementRegistry::find_by_node_key(
    std::string_view node_key) const noexcept {
    const auto indexed = node_key_to_item_.find(std::string(node_key));
    if (indexed == node_key_to_item_.end()) return nullptr;
    return find_by_item(indexed->second);
}

const AeNetworkNodePlacementDefinition*
AeNetworkNodePlacementRegistry::find_by_material_key(std::string_view material_key) const noexcept {
    const auto indexed = material_to_item_.find(std::string(material_key));
    if (indexed == material_to_item_.end()) return nullptr;
    return find_by_item(indexed->second);
}

std::vector<AeNetworkNodePlacementDefinition> AeNetworkNodePlacementRegistry::definitions() const {
    std::vector<AeNetworkNodePlacementDefinition> result;
    result.reserve(live_definitions_.size());
    for (const auto& [item_id, entry] : live_definitions_) {
        static_cast<void>(item_id);
        result.push_back(entry.definition);
    }
    return result;
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::begin_reload(
    snt::script::ScriptId script_id) {
    if (script_id == snt::script::kBuiltinScriptId) {
        return invalid_argument("Built-in AE node placement content cannot be reloaded as a script");
    }
    if (reloads_.contains(script_id)) {
        return invalid_state("AE node placement reload is already active for script");
    }
    reloads_.emplace(script_id, snapshot_script_definitions(script_id));
    erase_script_definitions(script_id);
    SNT_LOG_DEBUG("AE node placement content began transactional reload for script %llu",
                  static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::commit_reload(
    snt::script::ScriptId script_id) {
    const auto found = reloads_.find(script_id);
    if (found == reloads_.end()) {
        return invalid_state("No active AE node placement reload for script");
    }
    reloads_.erase(found);
    SNT_LOG_INFO("AE node placement content committed reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::rollback_reload(
    snt::script::ScriptId script_id) {
    const auto found = reloads_.find(script_id);
    if (found == reloads_.end()) {
        return invalid_state("No active AE node placement reload for script");
    }
    erase_script_definitions(script_id);
    restore_script_definitions(found->second);
    reloads_.erase(found);
    SNT_LOG_WARN("AE node placement content rolled back reload for script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::begin_reload_batch(
    std::span<const snt::script::ScriptId> script_ids) {
    if (script_ids.empty()) {
        return invalid_argument("AE node placement reload batch must not be empty");
    }
    std::set<snt::script::ScriptId> unique_ids;
    for (const snt::script::ScriptId script_id : script_ids) {
        if (script_id == snt::script::kBuiltinScriptId) {
            return invalid_argument("Built-in AE node placement content cannot be reloaded as a script");
        }
        if (!unique_ids.insert(script_id).second) {
            return invalid_argument("AE node placement reload batch contains a duplicate ScriptId");
        }
        if (reloads_.contains(script_id)) {
            return invalid_state("AE node placement reload is already active for a batch script");
        }
    }
    for (const snt::script::ScriptId script_id : script_ids) {
        reloads_.emplace(script_id, snapshot_script_definitions(script_id));
        erase_script_definitions(script_id);
    }
    SNT_LOG_DEBUG("AE node placement content began transactional reload batch with %zu script(s)",
                  script_ids.size());
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::commit_reload_batch(
    std::span<const snt::script::ScriptId> script_ids) {
    if (script_ids.empty()) {
        return invalid_argument("AE node placement reload batch must not be empty");
    }
    std::set<snt::script::ScriptId> unique_ids;
    for (const snt::script::ScriptId script_id : script_ids) {
        if (!unique_ids.insert(script_id).second || !reloads_.contains(script_id)) {
            return invalid_state("No active AE node placement reload for a batch script");
        }
    }
    for (const snt::script::ScriptId script_id : script_ids) reloads_.erase(script_id);
    SNT_LOG_INFO("AE node placement content committed reload batch with %zu script(s)",
                 script_ids.size());
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::rollback_reload_batch(
    std::span<const snt::script::ScriptId> script_ids) {
    if (script_ids.empty()) {
        return invalid_argument("AE node placement reload batch must not be empty");
    }
    std::set<snt::script::ScriptId> unique_ids;
    for (const snt::script::ScriptId script_id : script_ids) {
        if (!unique_ids.insert(script_id).second || !reloads_.contains(script_id)) {
            return invalid_state("No active AE node placement reload for a batch script");
        }
    }
    for (auto it = script_ids.rbegin(); it != script_ids.rend(); ++it) {
        const auto snapshot = reloads_.find(*it);
        erase_script_definitions(*it);
        restore_script_definitions(snapshot->second);
    }
    for (const snt::script::ScriptId script_id : script_ids) reloads_.erase(script_id);
    SNT_LOG_WARN("AE node placement content rolled back reload batch with %zu script(s)",
                 script_ids.size());
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::unload_script(
    snt::script::ScriptId script_id) {
    if (script_id == snt::script::kBuiltinScriptId) {
        return invalid_argument("Built-in AE node placement content cannot be unloaded as a script");
    }
    if (reloads_.contains(script_id)) {
        return invalid_state("Cannot unload AE node placement content during its active reload");
    }
    erase_script_definitions(script_id);
    SNT_LOG_INFO("AE node placement content unloaded script %llu",
                 static_cast<unsigned long long>(script_id));
    return {};
}

void AeNetworkNodePlacementRegistry::reset() noexcept {
    backup_definitions_.clear();
    live_definitions_.clear();
    node_key_to_item_.clear();
    material_to_item_.clear();
    reloads_.clear();
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::register_definition(
    snt::script::ScriptId owner,
    AeNetworkNodePlacementDefinition definition,
    bool builtin) {
    auto normalized_material_key = normalize_terrain_material_key(definition.material_key);
    if (!normalized_material_key) return normalized_material_key.error();
    definition.material_key = std::move(*normalized_material_key);
    if (auto result = validate(definition); !result) return result.error();
    if (builtin != (owner == snt::script::kBuiltinScriptId)) {
        return invalid_argument("Built-in AE node placement registrations must use ScriptId 0");
    }

    const std::string item_id = definition.item_id;
    const auto existing = live_definitions_.find(item_id);
    if (!builtin && existing != live_definitions_.end() &&
        existing->second.owner != snt::script::kBuiltinScriptId &&
        existing->second.owner != owner) {
        return invalid_state("AE node placement item id is already owned by another script: " +
                             item_id);
    }
    if (builtin && existing != live_definitions_.end() &&
        existing->second.owner != snt::script::kBuiltinScriptId) {
        return invalid_state("Cannot replace a live script AE node placement with a built-in placement: " +
                             item_id);
    }

    const auto has_collision = [&definition, &item_id](const DefinitionMap& definitions,
                                                        std::string_view value,
                                                        bool node_key) -> const std::string* {
        for (const auto& [registered_item_id, entry] : definitions) {
            if (registered_item_id == item_id) continue;
            const std::string& candidate = node_key
                ? entry.definition.node_key
                : entry.definition.material_key;
            if (candidate == value) return &registered_item_id;
        }
        return nullptr;
    };
    const auto reject_collision = [&has_collision, &definition](const DefinitionMap& definitions,
                                                                 std::string_view label)
        -> snt::core::Expected<void> {
        if (const std::string* collision =
                has_collision(definitions, definition.node_key, true)) {
            return invalid_state("AE node placement node key is already registered by item: " +
                                 *collision + " (" + std::string(label) + ")");
        }
        if (const std::string* collision =
                has_collision(definitions, definition.material_key, false)) {
            return invalid_state("AE node placement material key is already registered by item: " +
                                 *collision + " (" + std::string(label) + ")");
        }
        return {};
    };
    if (auto result = reject_collision(live_definitions_, "live"); !result) return result.error();
    if (auto result = reject_collision(backup_definitions_, "built-in"); !result) return result.error();
    for (const auto& [reloading_script_id, snapshot] : reloads_) {
        if (reloading_script_id == owner) continue;
        if (snapshot.contains(item_id)) {
            return invalid_state("AE node placement item id is reserved by an active script reload: " +
                                 item_id);
        }
        if (auto result = reject_collision(snapshot, "active reload"); !result) {
            return result.error();
        }
    }

    OwnedDefinition entry{owner, std::move(definition)};
    if (builtin) backup_definitions_[item_id] = entry;
    live_definitions_[item_id] = std::move(entry);
    rebuild_indexes();
    return {};
}

snt::core::Expected<void> AeNetworkNodePlacementRegistry::validate(
    const AeNetworkNodePlacementDefinition& definition) {
    if (!is_valid_identifier(definition.item_id) || !is_valid_identifier(definition.node_key) ||
        !is_valid_identifier(definition.material_key)) {
        return invalid_argument("AE node placement has an invalid content identifier");
    }
    if (!is_known_ae_network_node_type(definition.type) ||
        definition.type == AeNetworkNodeType::kController ||
        definition.provided_channels < 0 ||
        (!ae_network_node_is_channel_provider(definition.type) &&
         definition.provided_channels != 0) ||
        !is_valid_connection_mask(definition.connection_mask)) {
        return invalid_argument("AE node placement has an invalid topology configuration");
    }
    if (definition.type == AeNetworkNodeType::kDrive) {
        if (!definition.drive_storage_cell ||
            !is_valid_drive_storage(*definition.drive_storage_cell)) {
            return invalid_argument("AE drive placement requires a valid local cell definition");
        }
    } else if (definition.drive_storage_cell.has_value()) {
        return invalid_argument("Only an AE drive placement may declare local cell storage");
    }
    return {};
}

AeNetworkNodePlacementRegistry::DefinitionMap
AeNetworkNodePlacementRegistry::snapshot_script_definitions(
    snt::script::ScriptId script_id) const {
    DefinitionMap snapshot;
    for (const auto& [item_id, entry] : live_definitions_) {
        if (entry.owner == script_id) snapshot.emplace(item_id, entry);
    }
    return snapshot;
}

void AeNetworkNodePlacementRegistry::erase_script_definitions(
    snt::script::ScriptId script_id) {
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
    rebuild_indexes();
}

void AeNetworkNodePlacementRegistry::restore_script_definitions(
    const DefinitionMap& snapshot) {
    for (const auto& [item_id, entry] : snapshot) live_definitions_[item_id] = entry;
    rebuild_indexes();
}

void AeNetworkNodePlacementRegistry::rebuild_indexes() {
    node_key_to_item_.clear();
    material_to_item_.clear();
    node_key_to_item_.reserve(live_definitions_.size());
    material_to_item_.reserve(live_definitions_.size());
    for (const auto& [item_id, entry] : live_definitions_) {
        node_key_to_item_.emplace(entry.definition.node_key, item_id);
        material_to_item_.emplace(entry.definition.material_key, item_id);
    }
}

}  // namespace snt::game
