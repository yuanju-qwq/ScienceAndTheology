// Chunk-anchored automation-controller persistence implementation.

#define SNT_LOG_CHANNEL "game.automation.persistence"
#include "game/simulation/automation_controller_persistence.h"

#include "core/error.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

constexpr uint64_t kAutomationControllerAnchorIdFlag = uint64_t{1} << 61u;
constexpr uint64_t kAutomationControllerAnchorSerialMask =
    kAutomationControllerAnchorIdFlag - 1u;
constexpr size_t kMaxAutomationFlowNodes = 1024;
constexpr size_t kMaxAutomationFlowConnections = 4096;
constexpr size_t kMaxAutomationControllerKeyBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int floor_div_chunk(int32_t coordinate) {
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t value = coordinate;
    return static_cast<int>(value >= 0 ? value / kChunkSize
                                       : -((-value + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] std::string describe_chunk(const ChunkKey& key) {
    return key.dimension_id + " (" + std::to_string(key.chunk_x) + "," +
        std::to_string(key.chunk_y) + "," + std::to_string(key.chunk_z) + ")";
}

[[nodiscard]] bool is_known_controller_kind(AutomationControllerKind kind) noexcept {
    return kind == AutomationControllerKind::kSfmManager;
}

[[nodiscard]] bool is_valid_controller_key(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kMaxAutomationControllerKeyBytes &&
        key.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool is_known_flow_node_type(SfmFlowNodeType type) noexcept {
    return type == SfmFlowNodeType::kInterval || type == SfmFlowNodeType::kTransfer;
}

[[nodiscard]] bool is_absent_transfer(const SfmResourceTransferRule& transfer) noexcept {
    return transfer.source.value.empty() && transfer.destination.value.empty() &&
        transfer.requested.is_absent();
}

[[nodiscard]] snt::core::Expected<void> validate_program(
    const SfmFlowProgramRecord& program) {
    if (program.nodes.size() > kMaxAutomationFlowNodes ||
        program.connections.size() > kMaxAutomationFlowConnections) {
        return invalid_argument("Automation controller flow program exceeds durable limits");
    }
    std::unordered_set<SfmFlowNodeId> node_ids;
    node_ids.reserve(program.nodes.size());
    for (const SfmFlowNodeRecord& node : program.nodes) {
        if (node.id == kInvalidSfmFlowNodeId || !is_known_flow_node_type(node.type) ||
            !node_ids.insert(node.id).second) {
            return invalid_argument("Automation controller flow program has an invalid node identity");
        }
        if (node.type == SfmFlowNodeType::kInterval) {
            if (node.interval_ticks == 0 || !is_absent_transfer(node.transfer)) {
                return invalid_argument("Automation controller interval node is malformed");
            }
        } else if (node.interval_ticks != 0 || !node.transfer.is_valid()) {
            return invalid_argument("Automation controller transfer node is malformed");
        }
    }
    std::unordered_set<uint64_t> connections;
    connections.reserve(program.connections.size());
    for (const SfmFlowConnectionRecord& connection : program.connections) {
        if (!node_ids.contains(connection.source) || !node_ids.contains(connection.destination)) {
            return invalid_argument("Automation controller flow connection has an unavailable endpoint");
        }
        const uint64_t key = (static_cast<uint64_t>(connection.source) << 32u) |
            connection.destination;
        if (!connections.insert(key).second) {
            return invalid_argument("Automation controller flow program has duplicate connections");
        }
    }
    return {};
}

[[nodiscard]] const BlockEntityPlacement* find_anchor(
    const GameChunkSidecar& sidecar,
    EntityId anchor_entity_id) {
    const BlockEntityPlacement* found = nullptr;
    for (const BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != anchor_entity_id) continue;
        if (found != nullptr) return nullptr;
        found = &placement;
    }
    return found;
}

[[nodiscard]] snt::core::Expected<void> validate_record(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const AutomationControllerPersistenceRecord& record) {
    if (!record.anchor_entity_id.is_valid() || !is_known_controller_kind(record.kind) ||
        !is_valid_controller_key(record.controller_key) || record.revision == 0) {
        return invalid_argument("Automation controller record has invalid identity or kind");
    }
    const BlockEntityPlacement* const anchor = find_anchor(sidecar, record.anchor_entity_id);
    if (anchor == nullptr || anchor->entity_type != BlockEntityType::AUTOMATION_CONTROLLER) {
        return invalid_state("Automation controller record has no unique controller anchor in chunk " +
                             describe_chunk(chunk_key));
    }
    if (floor_div_chunk(anchor->root_x) != chunk_key.chunk_x ||
        floor_div_chunk(anchor->root_y) != chunk_key.chunk_y ||
        floor_div_chunk(anchor->root_z) != chunk_key.chunk_z) {
        return invalid_state("Automation controller anchor root is owned by another chunk");
    }
    return validate_program(record.sfm_program);
}

[[nodiscard]] snt::core::Expected<EntityId> allocate_anchor_id(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> occupied_ids;
    uint64_t greatest_serial = 0;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            occupied_ids.insert(placement.id.id);
            if ((placement.id.id & kAutomationControllerAnchorIdFlag) == 0 ||
                (placement.id.id & (uint64_t{1} << 62u)) != 0 ||
                (placement.id.id & (uint64_t{1} << 63u)) != 0) {
                continue;
            }
            greatest_serial = std::max(
                greatest_serial, placement.id.id & kAutomationControllerAnchorSerialMask);
        }
    });
    if (greatest_serial >= kAutomationControllerAnchorSerialMask) {
        return invalid_state("Automation controller persistence exhausted reserved anchor ids");
    }
    for (uint64_t serial = greatest_serial + 1u;
         serial <= kAutomationControllerAnchorSerialMask; ++serial) {
        const uint64_t candidate = kAutomationControllerAnchorIdFlag | serial;
        if (!occupied_ids.contains(candidate)) return EntityId{candidate};
    }
    return invalid_state("Automation controller persistence exhausted reserved anchor ids");
}

}  // namespace

snt::core::Expected<AutomationControllerAnchor>
GameAutomationControllerPersistence::create_controller(
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key,
    int32_t root_x,
    int32_t root_y,
    int32_t root_z,
    AutomationControllerCreateRequest request) {
    GameChunkSidecar* const sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot create an automation controller without its chunk sidecar");
    }
    if (floor_div_chunk(root_x) != chunk_key.chunk_x ||
        floor_div_chunk(root_y) != chunk_key.chunk_y ||
        floor_div_chunk(root_z) != chunk_key.chunk_z) {
        return invalid_argument("Automation controller root is not owned by its target chunk");
    }
    if (!is_known_controller_kind(request.kind) ||
        !is_valid_controller_key(request.controller_key)) {
        return invalid_argument("Automation controller creation has an invalid kind or content key");
    }
    if (auto result = validate_program(request.sfm_program); !result) return result.error();
    for (const BlockEntityPlacement& placement : sidecar->block_entities) {
        if (placement.root_x == root_x && placement.root_y == root_y && placement.root_z == root_z) {
            return invalid_state("Automation controller root already has a block-entity owner");
        }
    }

    auto anchor_id = allocate_anchor_id(sidecars);
    if (!anchor_id) return anchor_id.error();
    AutomationControllerPersistenceRecord record{
        .anchor_entity_id = *anchor_id,
        .kind = request.kind,
        .controller_key = std::move(request.controller_key),
        .revision = 1,
        .sfm_program = std::move(request.sfm_program),
    };
    sidecar->block_entities.push_back({
        .id = *anchor_id,
        .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
        .owned_cell_count = 1,
    });
    if (auto result = validate_record(chunk_key, *sidecar, record); !result) {
        sidecar->block_entities.pop_back();
        return result.error();
    }
    sidecar->automation_controller_records.push_back(std::move(record));
    return AutomationControllerAnchor{.anchor_entity_id = *anchor_id};
}

snt::core::Expected<void> GameAutomationControllerPersistence::replace_sfm_program(
    GameChunkSidecarRegistry& sidecars,
    EntityId anchor_entity_id,
    SfmFlowProgramRecord program) {
    if (!anchor_entity_id.is_valid()) {
        return invalid_argument("Automation controller program replacement requires an anchor id");
    }
    if (auto result = validate_program(program); !result) return result.error();

    AutomationControllerPersistenceRecord* target = nullptr;
    const ChunkKey* target_key = nullptr;
    GameChunkSidecar* target_sidecar = nullptr;
    size_t match_count = 0;
    sidecars.for_each([&](const ChunkKey& key, GameChunkSidecar& sidecar) {
        for (AutomationControllerPersistenceRecord& record : sidecar.automation_controller_records) {
            if (record.anchor_entity_id != anchor_entity_id) continue;
            ++match_count;
            target = &record;
            target_key = &key;
            target_sidecar = &sidecar;
        }
    });
    if (match_count != 1 || target == nullptr || target_key == nullptr || target_sidecar == nullptr) {
        return invalid_state("Automation controller anchor does not map to exactly one record");
    }
    if (auto result = validate_record(*target_key, *target_sidecar, *target); !result) {
        return result.error();
    }
    if (target->revision == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Automation controller revision is exhausted");
    }
    target->sfm_program = std::move(program);
    ++target->revision;
    return {};
}

snt::core::Expected<void> GameAutomationControllerPersistence::remove_controller(
    GameChunkSidecarRegistry& sidecars,
    EntityId anchor_entity_id) {
    if (!anchor_entity_id.is_valid()) {
        return invalid_argument("Automation controller removal requires an anchor id");
    }
    GameChunkSidecar* target_sidecar = nullptr;
    size_t target_record_index = 0;
    size_t match_count = 0;
    sidecars.for_each([&](const ChunkKey&, GameChunkSidecar& sidecar) {
        for (size_t index = 0; index < sidecar.automation_controller_records.size(); ++index) {
            if (sidecar.automation_controller_records[index].anchor_entity_id != anchor_entity_id) {
                continue;
            }
            ++match_count;
            target_sidecar = &sidecar;
            target_record_index = index;
        }
    });
    if (match_count != 1 || target_sidecar == nullptr) {
        return invalid_state("Automation controller anchor does not map to exactly one record");
    }
    auto anchor = std::find_if(
        target_sidecar->block_entities.begin(), target_sidecar->block_entities.end(),
        [anchor_entity_id](const BlockEntityPlacement& placement) {
            return placement.id == anchor_entity_id &&
                placement.entity_type == BlockEntityType::AUTOMATION_CONTROLLER;
        });
    if (anchor == target_sidecar->block_entities.end()) {
        return invalid_state("Automation controller record has no removable block anchor");
    }
    target_sidecar->automation_controller_records.erase(
        target_sidecar->automation_controller_records.begin() +
        static_cast<std::ptrdiff_t>(target_record_index));
    target_sidecar->block_entities.erase(anchor);
    return {};
}

snt::core::Expected<void> GameAutomationControllerPersistence::validate_all(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> anchors;
    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey& key, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const AutomationControllerPersistenceRecord& record :
             sidecar.automation_controller_records) {
            if (auto result = validate_record(key, sidecar, record); !result) {
                error = result.error();
                return;
            }
            if (!anchors.insert(record.anchor_entity_id.id).second) {
                error = invalid_state("Duplicate automation controller anchor across chunk sidecars");
                return;
            }
        }
    });
    if (error) return *error;
    return {};
}

}  // namespace snt::game
