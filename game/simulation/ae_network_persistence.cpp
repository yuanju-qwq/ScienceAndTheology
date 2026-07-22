// Chunk-anchored AE physical-node persistence implementation.

#define SNT_LOG_CHANNEL "game.ae_network_persistence"
#include "game/simulation/ae_network_persistence.h"

#include "core/error.h"
#include "game/client/game_content_registry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace snt::game {
namespace {

constexpr uint64_t kAeNetworkNodeAnchorIdFlag = uint64_t{1} << 60u;
constexpr uint64_t kAeNetworkNodeAnchorSerialMask = kAeNetworkNodeAnchorIdFlag - 1u;
constexpr size_t kMaxAeNetworkNodeKeyBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int32_t chunk_coordinate_for_block(int32_t coordinate) noexcept {
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t value = coordinate;
    return static_cast<int32_t>(value >= 0
        ? value / kChunkSize
        : -(((-value) + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] bool valid_connection_mask(uint8_t mask) noexcept {
    return (mask & static_cast<uint8_t>(~CONN_ALL)) == 0;
}

[[nodiscard]] bool valid_node_key(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kMaxAeNetworkNodeKeyBytes &&
        key.find('\0') == std::string_view::npos;
}

[[nodiscard]] snt::core::Expected<void> validate_node_configuration(
    const AeNetworkNodePersistenceRecord& record) {
    if (!record.anchor_entity_id.is_valid() || !valid_node_key(record.node_key) ||
        !is_known_ae_network_node_type(record.type) ||
        record.provided_channels < 0 ||
        (!ae_network_node_is_channel_provider(record.type) &&
         record.provided_channels != 0) ||
        !valid_connection_mask(record.connection_mask) || record.revision == 0) {
        return invalid_argument("AE network node record has an invalid configuration");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_drive_storage_record(
    const GameChunkSidecar& sidecar,
    const AeDriveStoragePersistenceRecord& record) {
    if (!record.anchor_entity_id.is_valid() || record.revision == 0) {
        return invalid_argument("AE drive storage record has an invalid anchor or revision");
    }
    size_t drive_count = 0;
    for (const AeNetworkNodePersistenceRecord& node : sidecar.ae_network_node_records) {
        if (node.anchor_entity_id == record.anchor_entity_id &&
            node.type == AeNetworkNodeType::kDrive) {
            ++drive_count;
        }
    }
    if (drive_count != 1) {
        return invalid_state("AE drive storage record does not name one drive node");
    }
    std::unordered_set<ResourceContentKey, ResourceContentKey::Hash> seen;
    seen.reserve(record.stored_resources.size());
    for (const ResourceContentStack& stack : record.stored_resources) {
        if (!stack.is_valid() || !seen.insert(stack.key).second) {
            return invalid_argument("AE drive storage record contains an invalid or duplicate resource");
        }
    }
    return {};
}

[[nodiscard]] const BlockEntityPlacement* find_anchor(
    const GameChunkSidecar& sidecar, EntityId anchor_entity_id) {
    const BlockEntityPlacement* result = nullptr;
    for (const BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != anchor_entity_id) continue;
        if (result != nullptr) return nullptr;
        result = &placement;
    }
    return result;
}

[[nodiscard]] bool has_ae_controller_record(const GameChunkSidecar& sidecar,
                                             EntityId anchor_entity_id) noexcept {
    return std::any_of(sidecar.automation_controller_records.begin(),
                       sidecar.automation_controller_records.end(),
                       [anchor_entity_id](const AutomationControllerPersistenceRecord& record) {
                           return record.anchor_entity_id == anchor_entity_id &&
                               record.kind == AutomationControllerKind::kAeController;
                       });
}

[[nodiscard]] snt::core::Expected<void> validate_node_record(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const AeNetworkNodePersistenceRecord& record) {
    if (auto result = validate_node_configuration(record); !result) return result.error();
    const BlockEntityPlacement* const anchor = find_anchor(sidecar, record.anchor_entity_id);
    const BlockEntityType expected_type = record.type == AeNetworkNodeType::kController
        ? BlockEntityType::AUTOMATION_CONTROLLER
        : BlockEntityType::AUTOMATION_NETWORK_NODE;
    if (anchor == nullptr || anchor->entity_type != expected_type) {
        return invalid_state("AE network node record has no matching typed block anchor");
    }
    if (chunk_coordinate_for_block(anchor->root_x) != chunk_key.chunk_x ||
        chunk_coordinate_for_block(anchor->root_y) != chunk_key.chunk_y ||
        chunk_coordinate_for_block(anchor->root_z) != chunk_key.chunk_z) {
        return invalid_state("AE network node anchor belongs to another chunk");
    }
    if (record.type == AeNetworkNodeType::kController &&
        !has_ae_controller_record(sidecar, record.anchor_entity_id)) {
        return invalid_state("AE controller node has no matching automation controller record");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<EntityId> allocate_anchor_id(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> occupied_ids;
    uint64_t greatest_serial = 0;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            occupied_ids.insert(placement.id.id);
            if ((placement.id.id & kAeNetworkNodeAnchorIdFlag) == 0 ||
                (placement.id.id & (uint64_t{1} << 61u)) != 0 ||
                (placement.id.id & (uint64_t{1} << 62u)) != 0 ||
                (placement.id.id & (uint64_t{1} << 63u)) != 0) {
                continue;
            }
            greatest_serial = std::max(
                greatest_serial, placement.id.id & kAeNetworkNodeAnchorSerialMask);
        }
    });
    if (greatest_serial >= kAeNetworkNodeAnchorSerialMask) {
        return invalid_state("AE network node persistence exhausted reserved anchor ids");
    }
    for (uint64_t serial = greatest_serial + 1u;
         serial <= kAeNetworkNodeAnchorSerialMask; ++serial) {
        const uint64_t candidate = kAeNetworkNodeAnchorIdFlag | serial;
        if (!occupied_ids.contains(candidate)) return EntityId{candidate};
    }
    return invalid_state("AE network node persistence exhausted reserved anchor ids");
}

}  // namespace

snt::core::Expected<AeNetworkNodeAnchor> GameAeNetworkPersistence::create_node(
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key,
    int32_t root_x,
    int32_t root_y,
    int32_t root_z,
    AeNetworkNodeCreateRequest request) {
    if (request.type == AeNetworkNodeType::kController) {
        return invalid_argument("AE controller nodes must use automation controller persistence");
    }
    GameChunkSidecar* const sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot create an AE network node without its chunk sidecar");
    }
    if (chunk_coordinate_for_block(root_x) != chunk_key.chunk_x ||
        chunk_coordinate_for_block(root_y) != chunk_key.chunk_y ||
        chunk_coordinate_for_block(root_z) != chunk_key.chunk_z) {
        return invalid_argument("AE network node root is not owned by its target chunk");
    }
    for (const BlockEntityPlacement& placement : sidecar->block_entities) {
        if (placement.root_x == root_x && placement.root_y == root_y && placement.root_z == root_z) {
            return invalid_state("AE network node root already has a block-entity owner");
        }
    }

    auto anchor_id = allocate_anchor_id(sidecars);
    if (!anchor_id) return anchor_id.error();
    AeNetworkNodePersistenceRecord record{
        .anchor_entity_id = *anchor_id,
        .node_key = std::move(request.node_key),
        .type = request.type,
        .enabled = request.enabled,
        .provided_channels = request.provided_channels,
        .connection_mask = request.connection_mask,
        .revision = 1,
    };
    if (auto result = validate_node_configuration(record); !result) return result.error();
    sidecar->block_entities.push_back({
        .id = *anchor_id,
        .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
        .owned_cell_count = 1,
    });
    if (auto result = validate_node_record(chunk_key, *sidecar, record); !result) {
        sidecar->block_entities.pop_back();
        return result.error();
    }
    sidecar->ae_network_node_records.push_back(std::move(record));
    if (request.type == AeNetworkNodeType::kDrive) {
        AeDriveStoragePersistenceRecord storage_record{
            .anchor_entity_id = *anchor_id,
            .revision = 1,
        };
        if (auto result = validate_drive_storage_record(*sidecar, storage_record); !result) {
            sidecar->ae_network_node_records.pop_back();
            sidecar->block_entities.pop_back();
            return result.error();
        }
        sidecar->ae_drive_storage_records.push_back(std::move(storage_record));
    }
    return AeNetworkNodeAnchor{.anchor_entity_id = *anchor_id};
}

snt::core::Expected<void> GameAeNetworkPersistence::remove_node(
    GameChunkSidecarRegistry& sidecars, EntityId anchor_entity_id) {
    if (!anchor_entity_id.is_valid()) {
        return invalid_argument("AE network node removal requires an anchor id");
    }
    GameChunkSidecar* target_sidecar = nullptr;
    size_t target_index = 0;
    size_t match_count = 0;
    sidecars.for_each([&](const ChunkKey&, GameChunkSidecar& sidecar) {
        for (size_t index = 0; index < sidecar.ae_network_node_records.size(); ++index) {
            const AeNetworkNodePersistenceRecord& record = sidecar.ae_network_node_records[index];
            if (record.anchor_entity_id != anchor_entity_id) continue;
            ++match_count;
            target_sidecar = &sidecar;
            target_index = index;
        }
    });
    if (match_count != 1 || target_sidecar == nullptr) {
        return invalid_state("AE network anchor does not map to exactly one node record");
    }
    const AeNetworkNodePersistenceRecord& target =
        target_sidecar->ae_network_node_records[target_index];
    if (target.type == AeNetworkNodeType::kController) {
        return invalid_argument("AE controller nodes must be removed with their automation controller");
    }
    auto drive_storage = target_sidecar->ae_drive_storage_records.end();
    if (target.type == AeNetworkNodeType::kDrive) {
        drive_storage = std::find_if(
            target_sidecar->ae_drive_storage_records.begin(),
            target_sidecar->ae_drive_storage_records.end(),
            [anchor_entity_id](const AeDriveStoragePersistenceRecord& record) {
                return record.anchor_entity_id == anchor_entity_id;
            });
        if (drive_storage == target_sidecar->ae_drive_storage_records.end()) {
            return invalid_state("AE drive node has no removable drive storage record");
        }
    }
    const auto anchor = std::find_if(
        target_sidecar->block_entities.begin(), target_sidecar->block_entities.end(),
        [anchor_entity_id](const BlockEntityPlacement& placement) {
            return placement.id == anchor_entity_id &&
                placement.entity_type == BlockEntityType::AUTOMATION_NETWORK_NODE;
        });
    if (anchor == target_sidecar->block_entities.end()) {
        return invalid_state("AE network node record has no removable block anchor");
    }
    target_sidecar->ae_network_node_records.erase(
        target_sidecar->ae_network_node_records.begin() + static_cast<std::ptrdiff_t>(target_index));
    if (drive_storage != target_sidecar->ae_drive_storage_records.end()) {
        target_sidecar->ae_drive_storage_records.erase(drive_storage);
    }
    target_sidecar->block_entities.erase(anchor);
    return {};
}

snt::core::Expected<void> GameAeNetworkPersistence::validate_all(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> node_anchors;
    std::unordered_set<uint64_t> controller_anchors;
    std::unordered_set<uint64_t> drive_storage_anchors;
    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey& key, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const AutomationControllerPersistenceRecord& controller :
             sidecar.automation_controller_records) {
            if (controller.kind == AutomationControllerKind::kAeController) {
                if (!controller_anchors.insert(controller.anchor_entity_id.id).second) {
                    error = invalid_state("Duplicate AE controller anchor across chunk sidecars");
                    return;
                }
            }
        }
        for (const AeNetworkNodePersistenceRecord& record : sidecar.ae_network_node_records) {
            if (auto result = validate_node_record(key, sidecar, record); !result) {
                error = result.error();
                return;
            }
            if (!node_anchors.insert(record.anchor_entity_id.id).second) {
                error = invalid_state("Duplicate AE network node anchor across chunk sidecars");
                return;
            }
        }
        for (const AeDriveStoragePersistenceRecord& record : sidecar.ae_drive_storage_records) {
            if (auto result = validate_drive_storage_record(sidecar, record); !result) {
                error = result.error();
                return;
            }
            if (!drive_storage_anchors.insert(record.anchor_entity_id.id).second) {
                error = invalid_state("Duplicate AE drive storage anchor across chunk sidecars");
                return;
            }
        }
        for (const AeNetworkNodePersistenceRecord& record : sidecar.ae_network_node_records) {
            const bool has_drive_storage = drive_storage_anchors.contains(record.anchor_entity_id.id);
            if ((record.type == AeNetworkNodeType::kDrive) != has_drive_storage) {
                error = invalid_state("AE drive topology node and durable cell owner disagree");
                return;
            }
        }
    });
    if (error) return *error;
    for (const uint64_t anchor : controller_anchors) {
        if (!node_anchors.contains(anchor)) {
            return invalid_state("AE automation controller has no matching topology node");
        }
    }
    return {};
}

snt::core::Expected<void> GameAeNetworkPersistence::validate_content_references(
    const GameChunkSidecarRegistry& sidecars,
    const GameContentRegistry& content) {
    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const AeNetworkNodePersistenceRecord& record : sidecar.ae_network_node_records) {
            if (record.type == AeNetworkNodeType::kController) continue;
            const AeNetworkNodePlacementDefinition* const placement =
                content.find_ae_network_node_placement_by_node_key(record.node_key);
            if (placement == nullptr || placement->type != record.type) {
                error = invalid_state("Persisted AE node key '" + record.node_key +
                                      "' has no matching current placement definition");
                return;
            }
        }
    });
    if (error) return *error;
    return {};
}

}  // namespace snt::game
