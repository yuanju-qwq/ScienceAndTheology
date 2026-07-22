// Active owner for persisted AE drive cells.

#define SNT_LOG_CHANNEL "game.ae_drive_storage_runtime"
#include "game/simulation/ae_drive_storage_runtime.h"

#include "core/error.h"
#include "core/log.h"
#include "game/client/game_content_registry.h"
#include "game/simulation/ae_network_node_placement_registry.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] const AeNetworkNodePersistenceRecord* find_drive_node(
    const GameChunkSidecar& sidecar,
    EntityId anchor_entity_id) {
    const AeNetworkNodePersistenceRecord* result = nullptr;
    for (const AeNetworkNodePersistenceRecord& node : sidecar.ae_network_node_records) {
        if (node.anchor_entity_id != anchor_entity_id || node.type != AeNetworkNodeType::kDrive) {
            continue;
        }
        if (result != nullptr) return nullptr;
        result = &node;
    }
    return result;
}

[[nodiscard]] AeStorageCellConfig make_cell_config(
    const AeDriveStorageCellDefinition& definition) {
    return {
        .byte_capacity = definition.byte_capacity,
        .max_distinct_resources = definition.max_distinct_resources,
        .bytes_per_distinct_resource = definition.bytes_per_distinct_resource,
        .units_per_byte = definition.units_per_byte,
        .accepted_resource_types = definition.accepted_resource_types,
    };
}

}  // namespace

AeDriveStorageRuntimeService::AeDriveStorageRuntimeService(
    AeNetworkRuntimeService& network_runtime,
    const GameContentRegistry& content) noexcept
    : network_runtime_(&network_runtime), content_(&content) {}

AeDriveStorageRuntimeService::~AeDriveStorageRuntimeService() {
    if (network_runtime_ != nullptr) {
        for (auto& [anchor_id, drive] : drives_) {
            static_cast<void>(anchor_id);
            if (!network_runtime_->detach_storage(drive->attachment)) {
                SNT_LOG_ERROR("AE drive shutdown could not detach anchor=%llu",
                              static_cast<unsigned long long>(drive->anchor_entity_id.id));
            }
        }
    }
    drives_.clear();
}

snt::core::Expected<void> AeDriveStorageRuntimeService::materialize_chunk(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar) {
    if (network_runtime_ == nullptr || content_ == nullptr) {
        return invalid_state("AE drive storage runtime has no network or content owner");
    }
    const ResourceRuntimeIndex::Snapshot snapshot = content_->resource_runtime_index();
    if (!snapshot.key_context().is_valid()) {
        return invalid_state("AE drive storage runtime has no valid resource snapshot");
    }

    std::vector<std::unique_ptr<LiveDrive>> prepared;
    prepared.reserve(sidecar.ae_drive_storage_records.size());

    for (const AeDriveStoragePersistenceRecord& persisted : sidecar.ae_drive_storage_records) {
        const AeNetworkNodePersistenceRecord* const node =
            find_drive_node(sidecar, persisted.anchor_entity_id);
        if (node == nullptr) {
            return invalid_state("AE drive storage record has no unique drive topology owner");
        }
        const auto existing = drives_.find(persisted.anchor_entity_id.id);
        if (existing != drives_.end()) {
            if (!(existing->second->chunk_key == chunk_key)) {
                return invalid_state("AE drive anchor is already materialized by another chunk");
            }
            continue;
        }
        const AeNetworkNodePlacementDefinition* const placement =
            content_->find_ae_network_node_placement_by_node_key(node->node_key);
        if (placement == nullptr || placement->type != AeNetworkNodeType::kDrive ||
            !placement->drive_storage_cell.has_value()) {
            return invalid_state("AE drive node key has no current drive cell content definition: " +
                                 node->node_key);
        }
        AeStorageCellPersistenceRecord contents{
            .stored_resources = persisted.stored_resources,
        };
        auto cell = AeStorageCell::restore_persistence_record(
            make_cell_config(*placement->drive_storage_cell), contents, snapshot);
        if (!cell) {
            auto error = cell.error();
            error.with_context("AE drive storage restore for node key '" + node->node_key + "'");
            return error;
        }
        prepared.push_back(std::make_unique<LiveDrive>(LiveDrive{
            .chunk_key = chunk_key,
            .anchor_entity_id = persisted.anchor_entity_id,
            .cell = std::move(*cell),
        }));
    }

    for (std::unique_ptr<LiveDrive>& drive : prepared) {
        auto attachment = network_runtime_->attach_storage(drive->anchor_entity_id, drive->cell);
        if (!attachment) {
            for (const std::unique_ptr<LiveDrive>& attached_drive : prepared) {
                if (attached_drive->attachment.is_valid()) {
                    static_cast<void>(network_runtime_->detach_storage(attached_drive->attachment));
                }
            }
            auto error = attachment.error();
            error.with_context("AE drive storage attach");
            return error;
        }
        drive->attachment = *attachment;
    }
    for (std::unique_ptr<LiveDrive>& drive : prepared) {
        drives_.emplace(drive->anchor_entity_id.id, std::move(drive));
    }
    if (!prepared.empty()) {
        SNT_LOG_INFO("Materialized %zu AE drive cell(s) for chunk (%s,%d,%d,%d)",
                     prepared.size(), chunk_key.dimension_id.c_str(), chunk_key.chunk_x,
                     chunk_key.chunk_y, chunk_key.chunk_z);
    }
    return {};
}

snt::core::Expected<void> AeDriveStorageRuntimeService::dematerialize_chunk(
    const ChunkKey& chunk_key,
    GameChunkSidecar& sidecar) {
    if (network_runtime_ == nullptr) {
        return invalid_state("AE drive storage runtime has no network owner");
    }
    if (auto result = flush_chunk(chunk_key, sidecar); !result) return result.error();

    std::vector<uint64_t> anchors;
    for (const auto& [anchor_id, drive] : drives_) {
        if (drive->chunk_key == chunk_key) anchors.push_back(anchor_id);
    }
    for (const uint64_t anchor_id : anchors) {
        const auto found = drives_.find(anchor_id);
        if (found == drives_.end()) continue;
        if (!network_runtime_->detach_storage(found->second->attachment)) {
            return invalid_state("AE drive storage lost its runtime attachment during dematerialization");
        }
        drives_.erase(found);
    }
    if (!anchors.empty()) {
        SNT_LOG_INFO("Dematerialized %zu AE drive cell(s) for chunk (%s,%d,%d,%d)",
                     anchors.size(), chunk_key.dimension_id.c_str(), chunk_key.chunk_x,
                     chunk_key.chunk_y, chunk_key.chunk_z);
    }
    return {};
}

snt::core::Expected<void> AeDriveStorageRuntimeService::flush_chunk(
    const ChunkKey& chunk_key,
    GameChunkSidecar& sidecar) const {
    for (const auto& [anchor_id, drive] : drives_) {
        static_cast<void>(anchor_id);
        if (!(drive->chunk_key == chunk_key)) continue;
        auto captured = drive->cell.capture_persistence_record();
        if (!captured) {
            auto error = captured.error();
            error.with_context("AE drive storage persistence capture");
            return error;
        }
        if (auto result = write_persisted_contents(
                sidecar, drive->anchor_entity_id, std::move(captured->stored_resources));
            !result) {
            return result.error();
        }
    }
    return {};
}

AeStorageCell* AeDriveStorageRuntimeService::find_drive_cell(
    EntityId anchor_entity_id) noexcept {
    const auto found = drives_.find(anchor_entity_id.id);
    return found == drives_.end() ? nullptr : &found->second->cell;
}

const AeStorageCell* AeDriveStorageRuntimeService::find_drive_cell(
    EntityId anchor_entity_id) const noexcept {
    const auto found = drives_.find(anchor_entity_id.id);
    return found == drives_.end() ? nullptr : &found->second->cell;
}

snt::core::Expected<void> AeDriveStorageRuntimeService::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (network_runtime_ == nullptr) {
        return invalid_state("AE drive storage runtime has no network owner");
    }
    network_runtime_->detach_storage_aggregates_for_resource_reload();
    aggregates_detached_for_snapshot_ = true;
    for (auto& [anchor_id, drive] : drives_) {
        static_cast<void>(anchor_id);
        if (auto result = drive->cell.prepare_resource_runtime_snapshot(next_snapshot); !result) {
            for (auto& [other_anchor_id, other] : drives_) {
                static_cast<void>(other_anchor_id);
                other->cell.cancel_resource_runtime_snapshot();
            }
            static_cast<void>(rebuild_network_aggregates());
            aggregates_detached_for_snapshot_ = false;
            auto error = result.error();
            error.with_context("AE drive storage resource snapshot preparation");
            return error;
        }
    }
    return {};
}

void AeDriveStorageRuntimeService::commit_resource_runtime_snapshot() noexcept {
    for (auto& [anchor_id, drive] : drives_) {
        static_cast<void>(anchor_id);
        drive->cell.commit_resource_runtime_snapshot();
    }
    if (aggregates_detached_for_snapshot_) {
        static_cast<void>(rebuild_network_aggregates());
        aggregates_detached_for_snapshot_ = false;
    }
}

void AeDriveStorageRuntimeService::cancel_resource_runtime_snapshot() noexcept {
    for (auto& [anchor_id, drive] : drives_) {
        static_cast<void>(anchor_id);
        drive->cell.cancel_resource_runtime_snapshot();
    }
    if (aggregates_detached_for_snapshot_) {
        static_cast<void>(rebuild_network_aggregates());
        aggregates_detached_for_snapshot_ = false;
    }
}

snt::core::Expected<void> AeDriveStorageRuntimeService::write_persisted_contents(
    GameChunkSidecar& sidecar,
    EntityId anchor_entity_id,
    std::vector<ResourceContentStack> contents) {
    AeDriveStoragePersistenceRecord* record = nullptr;
    for (AeDriveStoragePersistenceRecord& candidate : sidecar.ae_drive_storage_records) {
        if (candidate.anchor_entity_id != anchor_entity_id) continue;
        if (record != nullptr) {
            return invalid_state("AE drive storage persistence has duplicate anchors");
        }
        record = &candidate;
    }
    if (record == nullptr) {
        return invalid_state("AE drive storage persistence has no matching durable record");
    }
    record->stored_resources = std::move(contents);
    return {};
}

snt::core::Expected<void> AeDriveStorageRuntimeService::rebuild_network_aggregates() noexcept {
    auto result = network_runtime_->rebuild_storage_aggregates();
    if (!result) {
        SNT_LOG_ERROR("AE drive storage could not rebuild aggregate indexes after resource snapshot boundary: %s",
                      result.error().format().c_str());
        return result.error();
    }
    return {};
}

}  // namespace snt::game
