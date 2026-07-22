// Active automation-controller runtime implementation.

#define SNT_LOG_CHANNEL "game.automation_runtime"
#include "game/simulation/automation_controller_runtime.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

constexpr size_t kMaxControllerKeyBytes = 256;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int32_t chunk_coordinate_for_block(int32_t block_coordinate) noexcept {
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t value = block_coordinate;
    return static_cast<int32_t>(value >= 0
        ? value / kChunkSize
        : -(((-value) + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] bool is_supported_controller_kind(AutomationControllerKind kind) noexcept {
    return kind == AutomationControllerKind::kSfmManager;
}

[[nodiscard]] bool valid_controller_key(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kMaxControllerKeyBytes &&
        key.find('\0') == std::string_view::npos;
}

}  // namespace

AutomationControllerRuntimeService::AutomationControllerRuntimeService(
    ResourceRuntimeIndex::Snapshot resource_snapshot)
    : resource_snapshot_(std::move(resource_snapshot)) {}

snt::core::Expected<SfmEndpointHandle>
AutomationControllerRuntimeService::register_sfm_endpoint(
    SfmEndpointAddress address, IResourceStorage& storage) {
    if (prepared_snapshot_) {
        return invalid_state("Cannot change SFM endpoints during a resource snapshot publication");
    }
    auto handle = endpoints_.register_endpoint(std::move(address), storage);
    if (!handle) return handle.error();
    mark_endpoint_topology_changed();
    return handle;
}

bool AutomationControllerRuntimeService::unregister_sfm_endpoint(
    SfmEndpointHandle handle) noexcept {
    if (prepared_snapshot_) return false;
    if (!endpoints_.unregister_endpoint(handle)) return false;
    mark_endpoint_topology_changed();
    return true;
}

snt::core::Expected<void> AutomationControllerRuntimeService::materialize_chunk(
    const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
    if (!resource_snapshot_.key_context().is_valid()) {
        return invalid_state("Automation controller runtime has no resource snapshot");
    }
    if (prepared_snapshot_) {
        return invalid_state("Cannot materialize automation controllers during a resource snapshot publication");
    }

    std::unordered_set<uint64_t> seen_anchors;
    seen_anchors.reserve(sidecar.automation_controller_records.size());
    std::vector<std::pair<uint64_t, Runtime>> replacements;
    replacements.reserve(sidecar.automation_controller_records.size());
    for (const AutomationControllerPersistenceRecord& record :
         sidecar.automation_controller_records) {
        // The AE topology has its own active owner. This SFM executor service
        // must not materialize an offline placeholder for an AE controller.
        if (record.kind != AutomationControllerKind::kSfmManager) continue;
        if (!record.anchor_entity_id.is_valid() ||
            !seen_anchors.insert(record.anchor_entity_id.id).second) {
            return invalid_state("Automation controller chunk has duplicate or invalid anchors");
        }
        const auto current = runtimes_.find(record.anchor_entity_id.id);
        if (current != runtimes_.end()) {
            const AutomationControllerRuntimePresentation& presentation =
                current->second.presentation;
            if (!(presentation.anchor_chunk == chunk_key)) {
                return invalid_state("Automation controller anchor is owned by more than one active chunk");
            }
            if (presentation.authoritative_revision == record.revision &&
                presentation.kind == record.kind &&
                presentation.controller_key == record.controller_key &&
                current->second.endpoint_topology_revision == endpoint_topology_revision_) {
                continue;
            }
        }
        auto runtime = build_runtime(chunk_key, sidecar, record, resource_snapshot_);
        if (!runtime) return runtime.error();
        replacements.emplace_back(record.anchor_entity_id.id, std::move(*runtime));
    }

    const auto previous_for_chunk = chunk_index_.find(chunk_key);
    bool removed_controller = false;
    if (previous_for_chunk != chunk_index_.end()) {
        for (const uint64_t anchor_id : previous_for_chunk->second) {
            if (!seen_anchors.contains(anchor_id)) {
                runtimes_.erase(anchor_id);
                removed_controller = true;
            }
        }
    }
    if (replacements.empty() && !removed_controller) return {};
    for (auto& [anchor_id, runtime] : replacements) {
        runtimes_.insert_or_assign(anchor_id, std::move(runtime));
    }
    rebuild_indexes(runtimes_, chunk_index_, tick_order_);
    for (const AutomationControllerPersistenceRecord& record :
         sidecar.automation_controller_records) {
        const auto found = runtimes_.find(record.anchor_entity_id.id);
        if (found != runtimes_.end() && !found->second.presentation.online) {
            log_offline(found->second);
        }
    }
    return {};
}

void AutomationControllerRuntimeService::dematerialize_chunk(const ChunkKey& chunk_key) noexcept {
    if (prepared_snapshot_) return;
    const auto found = chunk_index_.find(chunk_key);
    if (found == chunk_index_.end()) return;
    for (const uint64_t anchor_id : found->second) runtimes_.erase(anchor_id);
    rebuild_indexes(runtimes_, chunk_index_, tick_order_);
}

snt::core::Expected<void> AutomationControllerRuntimeService::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (prepared_snapshot_) {
        return invalid_state("Automation controller resource snapshot publication is already prepared");
    }
    auto prepared = rebuild_all(std::move(next_snapshot));
    if (!prepared) return prepared.error();
    prepared_snapshot_ = std::move(*prepared);
    return {};
}

void AutomationControllerRuntimeService::commit_resource_runtime_snapshot() noexcept {
    if (!prepared_snapshot_) return;
    PreparedSnapshot prepared = std::move(*prepared_snapshot_);
    prepared_snapshot_.reset();
    resource_snapshot_ = std::move(prepared.resource_snapshot);
    runtimes_ = std::move(prepared.runtimes);
    chunk_index_ = std::move(prepared.chunk_index);
    tick_order_ = std::move(prepared.tick_order);
    endpoint_rebuild_pending_ = false;
    for (const uint64_t anchor_id : tick_order_) {
        const auto found = runtimes_.find(anchor_id);
        if (found != runtimes_.end() && !found->second.presentation.online) {
            log_offline(found->second);
        }
    }
}

void AutomationControllerRuntimeService::cancel_resource_runtime_snapshot() noexcept {
    prepared_snapshot_.reset();
}

snt::core::Expected<AutomationControllerRuntimeTickResult>
AutomationControllerRuntimeService::fixed_tick(uint64_t tick_index) {
    if (!resource_snapshot_.key_context().is_valid()) {
        return invalid_state("Automation controller runtime has no resource snapshot");
    }
    if (last_fixed_tick_ && tick_index < *last_fixed_tick_) {
        return invalid_argument("Automation controller runtime tick index moved backwards");
    }
    if (last_fixed_tick_ && tick_index == *last_fixed_tick_) {
        return AutomationControllerRuntimeTickResult{
            .active_controller_count = runtimes_.size(),
        };
    }
    if (endpoint_rebuild_pending_) {
        auto rebuilt = rebuild_all(resource_snapshot_);
        if (!rebuilt) return rebuilt.error();
        runtimes_ = std::move(rebuilt->runtimes);
        chunk_index_ = std::move(rebuilt->chunk_index);
        tick_order_ = std::move(rebuilt->tick_order);
        endpoint_rebuild_pending_ = false;
        for (const uint64_t anchor_id : tick_order_) {
            const auto found = runtimes_.find(anchor_id);
            if (found != runtimes_.end() && !found->second.presentation.online) {
                log_offline(found->second);
            }
        }
    }

    AutomationControllerRuntimeTickResult result{
        .active_controller_count = runtimes_.size(),
    };
    for (const uint64_t anchor_id : tick_order_) {
        const auto found = runtimes_.find(anchor_id);
        if (found == runtimes_.end()) continue;
        Runtime& runtime = found->second;
        if (!runtime.presentation.online || !runtime.sfm_executor) continue;
        ++result.online_controller_count;
        auto executed = runtime.sfm_executor->tick(tick_index);
        if (!executed) {
            runtime.sfm_executor.reset();
            runtime.presentation.online = false;
            runtime.offline_reason = executed.error().format();
            log_offline(runtime);
            continue;
        }
        result.dispatched_nodes += executed->dispatched_nodes;
        result.executed_transfers += executed->executed_transfers;
        if (executed->transferred_units > 0 &&
            result.transferred_units > std::numeric_limits<int64_t>::max() -
                executed->transferred_units) {
            result.transferred_units = std::numeric_limits<int64_t>::max();
        } else if (executed->transferred_units < 0 &&
                   result.transferred_units < std::numeric_limits<int64_t>::min() -
                       executed->transferred_units) {
            result.transferred_units = std::numeric_limits<int64_t>::min();
        } else {
            result.transferred_units += executed->transferred_units;
        }
    }
    last_fixed_tick_ = tick_index;
    return result;
}

const AutomationControllerRuntimePresentation*
AutomationControllerRuntimeService::find_controller(EntityId anchor_entity_id) const noexcept {
    if (!anchor_entity_id.is_valid()) return nullptr;
    const auto found = runtimes_.find(anchor_entity_id.id);
    return found == runtimes_.end() ? nullptr : &found->second.presentation;
}

std::vector<AutomationControllerRuntimePresentation>
AutomationControllerRuntimeService::collect_presentations(
    std::span<const ChunkKey> chunks) const {
    std::vector<AutomationControllerRuntimePresentation> result;
    std::unordered_set<uint64_t> seen_anchors;
    for (const ChunkKey& chunk_key : chunks) {
        const auto found = chunk_index_.find(chunk_key);
        if (found == chunk_index_.end()) continue;
        for (const uint64_t anchor_id : found->second) {
            if (!seen_anchors.insert(anchor_id).second) continue;
            const auto runtime = runtimes_.find(anchor_id);
            if (runtime != runtimes_.end()) result.push_back(runtime->second.presentation);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const AutomationControllerRuntimePresentation& left,
                 const AutomationControllerRuntimePresentation& right) {
                  return left.anchor_entity_id.id < right.anchor_entity_id.id;
              });
    return result;
}

snt::core::Expected<AutomationControllerRuntimeService::Runtime>
AutomationControllerRuntimeService::build_runtime(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const AutomationControllerPersistenceRecord& record,
    const ResourceRuntimeIndex::Snapshot& resource_snapshot) const {
    if (!resource_snapshot.key_context().is_valid()) {
        return invalid_state("Automation controller compilation requires a resource snapshot");
    }
    const BlockEntityPlacement* anchor = nullptr;
    if (auto result = validate_anchor(chunk_key, sidecar, record, anchor); !result) {
        return result.error();
    }

    Runtime runtime{
        .presentation = {
            .anchor_chunk = chunk_key,
            .anchor_entity_id = record.anchor_entity_id,
            .root_x = anchor->root_x,
            .root_y = anchor->root_y,
            .root_z = anchor->root_z,
            .kind = record.kind,
            .controller_key = record.controller_key,
            .authoritative_revision = record.revision,
            .online = false,
            .sfm_program = record.sfm_program,
        },
        .endpoint_topology_revision = endpoint_topology_revision_,
    };
    if (!is_supported_controller_kind(record.kind)) {
        runtime.offline_reason = "controller kind has no active runtime implementation";
        return runtime;
    }
    // An empty graph is a valid online controller. It owns no executor until
    // an editor adds an interval/transfer program, avoiding a fake no-op task
    // on the hot path.
    if (record.sfm_program.nodes.empty() && record.sfm_program.connections.empty()) {
        runtime.presentation.online = true;
        return runtime;
    }
    auto compiled = SfmFlowProgramCompiler::compile(
        record.sfm_program, endpoints_, resource_snapshot);
    if (!compiled) {
        runtime.offline_reason = compiled.error().format();
        return runtime;
    }
    auto executor = SfmFlowExecutor::create(std::move(*compiled), endpoints_);
    if (!executor) {
        runtime.offline_reason = executor.error().format();
        return runtime;
    }
    runtime.presentation.online = true;
    runtime.sfm_executor = std::move(*executor);
    return runtime;
}

snt::core::Expected<AutomationControllerRuntimeService::PreparedSnapshot>
AutomationControllerRuntimeService::rebuild_all(
    ResourceRuntimeIndex::Snapshot resource_snapshot) const {
    if (!resource_snapshot.key_context().is_valid()) {
        return invalid_state("Automation controller runtime snapshot is invalid");
    }
    PreparedSnapshot prepared{
        .resource_snapshot = resource_snapshot,
    };
    prepared.runtimes.reserve(runtimes_.size());
    for (const auto& [anchor_id, runtime] : runtimes_) {
        const AutomationControllerRuntimePresentation& presentation = runtime.presentation;
        GameChunkSidecar sidecar;
        sidecar.block_entities.push_back({
            .id = presentation.anchor_entity_id,
            .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
            .root_x = presentation.root_x,
            .root_y = presentation.root_y,
            .root_z = presentation.root_z,
            .owned_cell_count = 1,
        });
        sidecar.automation_controller_records.push_back({
            .anchor_entity_id = presentation.anchor_entity_id,
            .kind = presentation.kind,
            .controller_key = presentation.controller_key,
            .revision = presentation.authoritative_revision,
            .sfm_program = presentation.sfm_program,
        });
        auto rebuilt = build_runtime(
            presentation.anchor_chunk, sidecar,
            sidecar.automation_controller_records.front(), resource_snapshot);
        if (!rebuilt) return rebuilt.error();
        if (!prepared.runtimes.emplace(anchor_id, std::move(*rebuilt)).second) {
            return invalid_state("Automation controller runtime has duplicate active anchors");
        }
    }
    rebuild_indexes(prepared.runtimes, prepared.chunk_index, prepared.tick_order);
    return prepared;
}

snt::core::Expected<void> AutomationControllerRuntimeService::validate_anchor(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const AutomationControllerPersistenceRecord& record,
    const BlockEntityPlacement*& out_anchor) {
    out_anchor = nullptr;
    if (!record.anchor_entity_id.is_valid() || record.revision == 0 ||
        !valid_controller_key(record.controller_key)) {
        return invalid_argument("Automation controller runtime record has an invalid identity");
    }
    for (const BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != record.anchor_entity_id) continue;
        if (placement.entity_type != BlockEntityType::AUTOMATION_CONTROLLER || out_anchor != nullptr) {
            return invalid_state("Automation controller runtime record has an ambiguous anchor");
        }
        out_anchor = &placement;
    }
    if (out_anchor == nullptr) {
        return invalid_state("Automation controller runtime record has no block anchor");
    }
    if (chunk_coordinate_for_block(out_anchor->root_x) != chunk_key.chunk_x ||
        chunk_coordinate_for_block(out_anchor->root_y) != chunk_key.chunk_y ||
        chunk_coordinate_for_block(out_anchor->root_z) != chunk_key.chunk_z) {
        return invalid_state("Automation controller runtime anchor belongs to another chunk");
    }
    return {};
}

void AutomationControllerRuntimeService::rebuild_indexes(
    RuntimeMap& runtimes,
    ChunkRuntimeIndex& chunk_index,
    std::vector<uint64_t>& tick_order) {
    chunk_index.clear();
    tick_order.clear();
    tick_order.reserve(runtimes.size());
    for (const auto& [anchor_id, runtime] : runtimes) {
        chunk_index[runtime.presentation.anchor_chunk].push_back(anchor_id);
        tick_order.push_back(anchor_id);
    }
    std::sort(tick_order.begin(), tick_order.end());
    for (auto& [chunk_key, anchors] : chunk_index) {
        static_cast<void>(chunk_key);
        std::sort(anchors.begin(), anchors.end());
    }
}

void AutomationControllerRuntimeService::mark_endpoint_topology_changed() noexcept {
    if (endpoint_topology_revision_ != std::numeric_limits<uint64_t>::max()) {
        ++endpoint_topology_revision_;
    }
    endpoint_rebuild_pending_ = true;
}

void AutomationControllerRuntimeService::log_offline(const Runtime& runtime) const noexcept {
    SNT_LOG_WARN("Automation controller '%s' anchor=%llu is offline: %s",
                 runtime.presentation.controller_key.c_str(),
                 static_cast<unsigned long long>(runtime.presentation.anchor_entity_id.id),
                 runtime.offline_reason.empty() ? "unknown runtime failure"
                                                : runtime.offline_reason.c_str());
}

}  // namespace snt::game
