// Game-owned AE physical-node placement content registry.
//
// This module maps a durable inventory item and stable node content key to
// one independently owned AE world block. The placement configuration is
// resolved only at content/load boundaries; active topology uses the compact
// AeNetworkNodeType and persisted node configuration without string lookups.

#pragma once

#include "core/expected.h"
#include "game/automation/ae_network_types.h"
#include "game/world/defs/block_entity.h"
#include "script/content_host.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace snt::game {

// Drive capacity is authored content. Its values are copied into live
// AeStorageCell instances only while the owning chunk is materialized.
struct AeDriveStorageCellDefinition {
    int64_t byte_capacity = 0;
    uint32_t max_distinct_resources = 0;
    int64_t bytes_per_distinct_resource = 8;
    int64_t units_per_byte = 1;
    std::vector<std::string> accepted_resource_types;
};

struct AeNetworkNodePlacementDefinition {
    std::string item_id;
    // Stable content identity persisted by the typed AE node record. It is
    // not a runtime handle and survives a compact ResourceKey snapshot reload.
    std::string node_key;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    std::string material_key;
    bool enabled = true;
    int32_t provided_channels = 0;
    uint8_t connection_mask = CONN_ALL;
    // Exactly drive placements own one cell definition. Storage buses bind a
    // separately declared external endpoint and therefore intentionally have
    // no local cell state here.
    std::optional<AeDriveStorageCellDefinition> drive_storage_cell;
};

class AeNetworkNodePlacementRegistry final {
public:
    AeNetworkNodePlacementRegistry() = default;
    ~AeNetworkNodePlacementRegistry() = default;

    AeNetworkNodePlacementRegistry(const AeNetworkNodePlacementRegistry&) = delete;
    AeNetworkNodePlacementRegistry& operator=(const AeNetworkNodePlacementRegistry&) = delete;

    [[nodiscard]] snt::core::Expected<void> register_builtin(
        AeNetworkNodePlacementDefinition definition);
    [[nodiscard]] snt::core::Expected<void> register_script(
        snt::script::ScriptId script_id,
        AeNetworkNodePlacementDefinition definition);

    [[nodiscard]] const AeNetworkNodePlacementDefinition* find_by_item(
        std::string_view item_id) const noexcept;
    [[nodiscard]] const AeNetworkNodePlacementDefinition* find_by_node_key(
        std::string_view node_key) const noexcept;
    // Material-to-placement resolution is an expected O(1) hash lookup. It
    // is used at the server world-boundary, never by the active AE hot path.
    [[nodiscard]] const AeNetworkNodePlacementDefinition* find_by_material_key(
        std::string_view material_key) const noexcept;
    [[nodiscard]] std::vector<AeNetworkNodePlacementDefinition> definitions() const;

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
        AeNetworkNodePlacementDefinition definition;
    };

    using DefinitionMap = std::map<std::string, OwnedDefinition, std::less<>>;

    [[nodiscard]] snt::core::Expected<void> register_definition(
        snt::script::ScriptId owner,
        AeNetworkNodePlacementDefinition definition,
        bool builtin);
    [[nodiscard]] static snt::core::Expected<void> validate(
        const AeNetworkNodePlacementDefinition& definition);
    [[nodiscard]] DefinitionMap snapshot_script_definitions(
        snt::script::ScriptId script_id) const;
    void erase_script_definitions(snt::script::ScriptId script_id);
    void restore_script_definitions(const DefinitionMap& snapshot);
    void rebuild_indexes();

    // Ordered ownership map keeps catalog snapshots deterministic. Secondary
    // indexes keep stable-key and material-key resolutions constant-time.
    DefinitionMap backup_definitions_;
    DefinitionMap live_definitions_;
    std::unordered_map<std::string, std::string> node_key_to_item_;
    std::unordered_map<std::string, std::string> material_to_item_;
    std::map<snt::script::ScriptId, DefinitionMap> reloads_;
};

}  // namespace snt::game
