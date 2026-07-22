// Active owner for chunk-materialized AE physical topology implementation.

#define SNT_LOG_CHANNEL "game.ae_network_runtime"
#include "game/simulation/ae_network_runtime.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

struct Direction {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    uint8_t outgoing_mask = 0;
    uint8_t incoming_mask = 0;
};

constexpr std::array<Direction, 6> kDirections{{
    {1, 0, 0, CONN_POS_X, CONN_NEG_X},
    {-1, 0, 0, CONN_NEG_X, CONN_POS_X},
    {0, 1, 0, CONN_POS_Y, CONN_NEG_Y},
    {0, -1, 0, CONN_NEG_Y, CONN_POS_Y},
    {0, 0, 1, CONN_POS_Z, CONN_NEG_Z},
    {0, 0, -1, CONN_NEG_Z, CONN_POS_Z},
}};

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

[[nodiscard]] const BlockEntityPlacement* find_anchor(
    const GameChunkSidecar& sidecar, EntityId anchor_entity_id) {
    const BlockEntityPlacement* found = nullptr;
    for (const BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != anchor_entity_id) continue;
        if (found != nullptr) return nullptr;
        found = &placement;
    }
    return found;
}

[[nodiscard]] size_t matching_ae_controller_records(
    const GameChunkSidecar& sidecar, EntityId anchor_entity_id) noexcept {
    return static_cast<size_t>(std::count_if(
        sidecar.automation_controller_records.begin(),
        sidecar.automation_controller_records.end(),
        [anchor_entity_id](const AutomationControllerPersistenceRecord& record) {
            return record.anchor_entity_id == anchor_entity_id &&
                record.kind == AutomationControllerKind::kAeController;
        }));
}

}  // namespace

AeNetworkRuntimeService::AeNetworkRuntimeService() {
    // Slot zero stays permanently invalid, matching the topology and SFM
    // handle conventions. A default attachment handle can never target an endpoint.
    storage_attachments_.emplace_back();
}

AeNetworkRuntimeService::~AeNetworkRuntimeService() {
    clear_storage_observers();
}

size_t AeNetworkRuntimeService::PositionHash::operator()(
    const Position& position) const noexcept {
    size_t hash = std::hash<std::string>{}(position.dimension_id);
    const auto mix = [&hash](int32_t value) {
        hash ^= std::hash<int32_t>{}(value) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
    };
    mix(position.root_x);
    mix(position.root_y);
    mix(position.root_z);
    return hash;
}

snt::core::Expected<void> AeNetworkRuntimeService::materialize_chunk(
    const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
    std::unordered_set<uint64_t> seen_anchors;
    seen_anchors.reserve(sidecar.ae_network_node_records.size());
    RuntimeMap candidate = runtimes_;
    for (auto it = candidate.begin(); it != candidate.end();) {
        if (it->second.presentation.anchor_chunk == chunk_key) {
            it = candidate.erase(it);
        } else {
            ++it;
        }
    }

    for (const AeNetworkNodePersistenceRecord& node : sidecar.ae_network_node_records) {
        const BlockEntityPlacement* anchor = nullptr;
        if (auto result = validate_node(chunk_key, sidecar, node, anchor); !result) {
            return result.error();
        }
        if (!seen_anchors.insert(node.anchor_entity_id.id).second) {
            return invalid_state("AE network chunk has duplicate node anchors");
        }
        Runtime runtime{
            .presentation = {
                .anchor_chunk = chunk_key,
                .anchor_entity_id = node.anchor_entity_id,
                .root_x = anchor->root_x,
                .root_y = anchor->root_y,
                .root_z = anchor->root_z,
                .type = node.type,
                .enabled = node.enabled,
                .provided_channels = node.provided_channels,
                .authoritative_revision = node.revision,
            },
            .connection_mask = node.connection_mask,
        };
        if (!candidate.emplace(node.anchor_entity_id.id, std::move(runtime)).second) {
            return invalid_state("AE network node anchor is owned by more than one active chunk");
        }
    }

    auto rebuilt = rebuild_topology(candidate);
    if (!rebuilt) return rebuilt.error();
    auto prepared_storage_indexes = prepare_storage_indexes(candidate);
    if (!prepared_storage_indexes) return prepared_storage_indexes.error();
    ChunkRuntimeIndex next_chunk_index;
    rebuild_chunk_index(candidate, next_chunk_index);
    runtimes_ = std::move(candidate);
    chunk_index_ = std::move(next_chunk_index);
    positions_ = std::move(rebuilt->positions);
    topology_ = std::move(rebuilt->topology);
    commit_storage_indexes(std::move(*prepared_storage_indexes));
    return {};
}

snt::core::Expected<void> AeNetworkRuntimeService::dematerialize_chunk(
    const ChunkKey& chunk_key) {
    const auto existing = chunk_index_.find(chunk_key);
    if (existing == chunk_index_.end()) return {};
    RuntimeMap candidate = runtimes_;
    for (const uint64_t anchor_id : existing->second) candidate.erase(anchor_id);
    auto rebuilt = rebuild_topology(candidate);
    if (!rebuilt) return rebuilt.error();
    auto prepared_storage_indexes = prepare_storage_indexes(candidate);
    if (!prepared_storage_indexes) return prepared_storage_indexes.error();
    ChunkRuntimeIndex next_chunk_index;
    rebuild_chunk_index(candidate, next_chunk_index);
    runtimes_ = std::move(candidate);
    chunk_index_ = std::move(next_chunk_index);
    positions_ = std::move(rebuilt->positions);
    topology_ = std::move(rebuilt->topology);
    commit_storage_indexes(std::move(*prepared_storage_indexes));
    return {};
}

uint64_t AeNetworkRuntimeService::topology_revision() const noexcept {
    return topology_ ? topology_->topology_revision() : 0;
}

const AeNetworkRuntimeNodePresentation* AeNetworkRuntimeService::find_node(
    EntityId anchor_entity_id) const noexcept {
    if (!anchor_entity_id.is_valid()) return nullptr;
    const auto found = runtimes_.find(anchor_entity_id.id);
    return found == runtimes_.end() ? nullptr : &found->second.presentation;
}

const AeNetworkRuntimeNodePresentation* AeNetworkRuntimeService::find_node_at(
    std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const {
    const Position position{
        .dimension_id = std::string(dimension_id),
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
    };
    const auto indexed = positions_.find(position);
    if (indexed == positions_.end()) return nullptr;
    const auto found = runtimes_.find(indexed->second);
    return found == runtimes_.end() ? nullptr : &found->second.presentation;
}

std::optional<AeNetworkRuntimeComponentPresentation>
AeNetworkRuntimeService::find_component(uint32_t component_id) const noexcept {
    if (component_id == 0 || !topology_) return std::nullopt;
    const auto component = topology_->component_state(component_id);
    if (!component) return std::nullopt;
    return AeNetworkRuntimeComponentPresentation{
        .id = component->id,
        .node_count = component->node_count,
        .controller_count = component->controller_count,
        .total_channels = component->total_channels,
        .online_devices = component->online_devices,
        .offline_devices = component->offline_devices,
        .is_powered = component->is_powered,
    };
}

std::vector<AeNetworkRuntimeNodePresentation>
AeNetworkRuntimeService::collect_presentations(std::span<const ChunkKey> chunks) const {
    std::vector<AeNetworkRuntimeNodePresentation> result;
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
              [](const AeNetworkRuntimeNodePresentation& left,
                 const AeNetworkRuntimeNodePresentation& right) {
                  return left.anchor_entity_id.id < right.anchor_entity_id.id;
              });
    return result;
}

snt::core::Expected<AeNetworkStorageAttachmentHandle>
AeNetworkRuntimeService::attach_storage(EntityId node_anchor,
                                        IResourceAggregateStorage& storage) {
    if (!node_anchor.is_valid() || !storage.key_context().is_valid()) {
        return invalid_argument("AE storage attachment requires a valid node anchor and endpoint snapshot");
    }
    if (storage.has_resource_aggregate_observer()) {
        return invalid_state("AE storage endpoint is already attached to an aggregate");
    }
    const auto node = runtimes_.find(node_anchor.id);
    if (node == runtimes_.end()) {
        return invalid_state("AE storage attachment requires a materialized physical node");
    }
    if (!is_storage_node_type(node->second.presentation.type)) {
        return invalid_argument("Only AE drives and storage buses may own an AE storage endpoint");
    }

    uint32_t slot_index = 0;
    if (!reusable_storage_attachment_slots_.empty()) {
        slot_index = reusable_storage_attachment_slots_.back();
        reusable_storage_attachment_slots_.pop_back();
    } else {
        if (storage_attachments_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE runtime exhausted storage attachment handle slots");
        }
        slot_index = static_cast<uint32_t>(storage_attachments_.size());
        storage_attachments_.emplace_back();
    }

    StorageAttachmentSlot& slot = storage_attachments_[slot_index];
    if (slot.storage != nullptr || slot.generation == 0) {
        return invalid_state("AE runtime encountered an unavailable storage attachment slot");
    }
    slot.storage = &storage;
    slot.node_anchor = node_anchor;
    ++attached_storage_count_;

    auto indexes = prepare_storage_indexes(runtimes_);
    if (!indexes) {
        slot.storage = nullptr;
        slot.node_anchor = {};
        if (attached_storage_count_ > 0) --attached_storage_count_;
        if (slot.generation != std::numeric_limits<uint32_t>::max()) {
            reusable_storage_attachment_slots_.push_back(slot_index);
        }
        return indexes.error();
    }
    commit_storage_indexes(std::move(*indexes));

    SNT_LOG_DEBUG("Attached AE storage endpoint to node anchor=%llu slot=%u",
                  static_cast<unsigned long long>(node_anchor.id),
                  static_cast<unsigned int>(slot_index));
    return AeNetworkStorageAttachmentHandle{
        .slot = slot_index,
        .generation = slot.generation,
    };
}

bool AeNetworkRuntimeService::detach_storage(
    AeNetworkStorageAttachmentHandle handle) noexcept {
    StorageAttachmentSlot* const slot = find_storage_attachment(handle);
    if (slot == nullptr) return false;

    const uint32_t component_id = slot->component_id;
    const ResourceAggregateStorageHandle aggregate_handle = slot->aggregate_handle;
    if (slot->storage != nullptr) slot->storage->clear_resource_aggregate_observer();
    if (component_id != 0 && aggregate_handle.is_valid()) {
        const auto index = component_storage_indexes_.find(component_id);
        if (index == component_storage_indexes_.end() ||
            !index->second->detach_storage(aggregate_handle)) {
            SNT_LOG_ERROR("AE runtime lost aggregate state while detaching storage slot=%u",
                          static_cast<unsigned int>(handle.slot));
        } else if (index->second->storage_count() == 0) {
            component_storage_indexes_.erase(index);
        }
    }

    slot->storage = nullptr;
    slot->node_anchor = {};
    slot->component_id = 0;
    slot->aggregate_handle = {};
    if (attached_storage_count_ > 0) --attached_storage_count_;
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_storage_attachment_slots_.push_back(handle.slot);
    }
    SNT_LOG_DEBUG("Detached AE storage endpoint slot=%u", static_cast<unsigned int>(handle.slot));
    return true;
}

bool AeNetworkRuntimeService::is_storage_attached(
    AeNetworkStorageAttachmentHandle handle) const noexcept {
    return find_storage_attachment(handle) != nullptr;
}

std::optional<uint32_t> AeNetworkRuntimeService::storage_component_of(
    AeNetworkStorageAttachmentHandle handle) const noexcept {
    const StorageAttachmentSlot* const slot = find_storage_attachment(handle);
    if (slot == nullptr || slot->component_id == 0 || !slot->aggregate_handle.is_valid()) {
        return std::nullopt;
    }
    const auto index = component_storage_indexes_.find(slot->component_id);
    if (index == component_storage_indexes_.end() ||
        !index->second->is_attached(slot->aggregate_handle)) {
        return std::nullopt;
    }
    return slot->component_id;
}

int64_t AeNetworkRuntimeService::amount_at_node(
    EntityId node_anchor, const ResourceKeyContext& context,
    const ResourceKey& key) const noexcept {
    if (!node_anchor.is_valid()) return 0;
    const auto node = runtimes_.find(node_anchor.id);
    if (node == runtimes_.end() || !node->second.presentation.online ||
        node->second.presentation.component_id == 0) {
        return 0;
    }
    return amount_in_component(node->second.presentation.component_id, context, key);
}

int64_t AeNetworkRuntimeService::amount_in_component(
    uint32_t component_id, const ResourceKeyContext& context,
    const ResourceKey& key) const noexcept {
    if (component_id == 0) return 0;
    const auto index = component_storage_indexes_.find(component_id);
    return index == component_storage_indexes_.end()
        ? 0
        : index->second->amount_of(context, key);
}

size_t AeNetworkRuntimeService::storage_count_in_component(uint32_t component_id) const noexcept {
    if (component_id == 0) return 0;
    const auto index = component_storage_indexes_.find(component_id);
    return index == component_storage_indexes_.end() ? 0 : index->second->storage_count();
}

void AeNetworkRuntimeService::detach_storage_aggregates_for_resource_reload() noexcept {
    clear_storage_observers();
    for (StorageAttachmentSlot& slot : storage_attachments_) {
        if (slot.storage == nullptr) continue;
        slot.component_id = 0;
        slot.aggregate_handle = {};
    }
    component_storage_indexes_.clear();
    SNT_LOG_DEBUG("Detached AE storage aggregates for resource snapshot reload; attachments=%zu",
                  attached_storage_count_);
}

snt::core::Expected<void> AeNetworkRuntimeService::rebuild_storage_aggregates() {
    auto indexes = prepare_storage_indexes(runtimes_);
    if (!indexes) return indexes.error();
    commit_storage_indexes(std::move(*indexes));
    SNT_LOG_DEBUG("Rebuilt AE storage aggregates after runtime boundary; attachments=%zu",
                  attached_storage_count_);
    return {};
}

snt::core::Expected<void> AeNetworkRuntimeService::validate_node(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const AeNetworkNodePersistenceRecord& node,
    const BlockEntityPlacement*& out_anchor) {
    out_anchor = nullptr;
    if (!node.anchor_entity_id.is_valid() || node.node_key.empty() ||
        node.node_key.size() > 256 || node.node_key.find('\0') != std::string::npos ||
        !is_known_ae_network_node_type(node.type) ||
        node.provided_channels < 0 ||
        (!ae_network_node_is_channel_provider(node.type) && node.provided_channels != 0) ||
        !valid_connection_mask(node.connection_mask) || node.revision == 0) {
        return invalid_argument("AE network runtime received an invalid node record");
    }
    const BlockEntityPlacement* const anchor = find_anchor(sidecar, node.anchor_entity_id);
    const BlockEntityType expected_type = node.type == AeNetworkNodeType::kController
        ? BlockEntityType::AUTOMATION_CONTROLLER
        : BlockEntityType::AUTOMATION_NETWORK_NODE;
    if (anchor == nullptr || anchor->entity_type != expected_type) {
        return invalid_state("AE network runtime node has no matching typed block anchor");
    }
    if (chunk_coordinate_for_block(anchor->root_x) != chunk_key.chunk_x ||
        chunk_coordinate_for_block(anchor->root_y) != chunk_key.chunk_y ||
        chunk_coordinate_for_block(anchor->root_z) != chunk_key.chunk_z) {
        return invalid_state("AE network runtime node anchor belongs to another chunk");
    }
    if (node.type == AeNetworkNodeType::kController &&
        matching_ae_controller_records(sidecar, node.anchor_entity_id) != 1) {
        return invalid_state("AE network runtime controller node has no unique controller owner");
    }
    out_anchor = anchor;
    return {};
}

snt::core::Expected<AeNetworkRuntimeService::TopologyBuild>
AeNetworkRuntimeService::rebuild_topology(RuntimeMap& runtimes) {
    TopologyBuild result{
        .topology = std::make_unique<AeNetworkTopology>(),
    };
    result.positions.reserve(runtimes.size());
    std::vector<uint64_t> anchor_ids;
    anchor_ids.reserve(runtimes.size());
    for (const auto& [anchor_id, runtime] : runtimes) {
        static_cast<void>(runtime);
        anchor_ids.push_back(anchor_id);
    }
    std::sort(anchor_ids.begin(), anchor_ids.end());

    for (const uint64_t anchor_id : anchor_ids) {
        Runtime& runtime = runtimes.at(anchor_id);
        Position position{
            .dimension_id = runtime.presentation.anchor_chunk.dimension_id,
            .root_x = runtime.presentation.root_x,
            .root_y = runtime.presentation.root_y,
            .root_z = runtime.presentation.root_z,
        };
        if (!result.positions.emplace(std::move(position), anchor_id).second) {
            return invalid_state("AE network runtime found duplicate active node positions");
        }
        auto handle = result.topology->add_node({
            .type = runtime.presentation.type,
            .enabled = runtime.presentation.enabled,
            .provided_channels = runtime.presentation.provided_channels,
        });
        if (!handle) return handle.error();
        runtime.handle = *handle;
    }

    for (const uint64_t anchor_id : anchor_ids) {
        const Runtime& runtime = runtimes.at(anchor_id);
        if (runtime.connection_mask == 0) continue;
        for (const Direction& direction : kDirections) {
            if ((runtime.connection_mask & direction.outgoing_mask) == 0) continue;
            const Position neighbor_position{
                .dimension_id = runtime.presentation.anchor_chunk.dimension_id,
                .root_x = runtime.presentation.root_x + direction.x,
                .root_y = runtime.presentation.root_y + direction.y,
                .root_z = runtime.presentation.root_z + direction.z,
            };
            const auto neighboring = result.positions.find(neighbor_position);
            if (neighboring == result.positions.end() || anchor_id >= neighboring->second) {
                continue;
            }
            const Runtime& neighbor = runtimes.at(neighboring->second);
            if ((neighbor.connection_mask & direction.incoming_mask) == 0) continue;
            if (auto connected = result.topology->connect(runtime.handle, neighbor.handle);
                !connected) {
                return connected.error();
            }
        }
    }

    for (const uint64_t anchor_id : anchor_ids) {
        Runtime& runtime = runtimes.at(anchor_id);
        const auto state = result.topology->find_node(runtime.handle);
        if (!state) return invalid_state("AE network topology lost a materialized node handle");
        runtime.presentation.online = state->online;
        runtime.presentation.component_id = state->component_id;
        runtime.presentation.provided_channels = state->provided_channels;
    }
    return result;
}

snt::core::Expected<AeNetworkRuntimeService::PreparedStorageIndexes>
AeNetworkRuntimeService::prepare_storage_indexes(const RuntimeMap& runtimes) const {
    PreparedStorageIndexes prepared;
    prepared.attachments.reserve(attached_storage_count_);
    std::unordered_map<uint32_t, ResourceKeyContext> component_contexts;
    component_contexts.reserve(attached_storage_count_);
    prepared.indexes.reserve(attached_storage_count_);

    for (uint32_t slot_index = 1; slot_index < storage_attachments_.size(); ++slot_index) {
        const StorageAttachmentSlot& attachment = storage_attachments_[slot_index];
        if (attachment.storage == nullptr) continue;
        const auto node = runtimes.find(attachment.node_anchor.id);
        // A cell remains bound to its durable block anchor while that chunk is
        // absent. It has no active aggregate until the physical node returns.
        if (node == runtimes.end()) continue;
        if (!is_storage_node_type(node->second.presentation.type)) {
            return invalid_state("AE storage attachment no longer belongs to a drive or storage bus");
        }
        if (!attachment.storage->key_context().is_valid()) {
            return invalid_state("AE storage attachment has no valid resource runtime snapshot");
        }
        const uint32_t component_id = node->second.presentation.component_id;
        if (!node->second.presentation.online || component_id == 0) continue;

        const ResourceKeyContext context = attachment.storage->key_context();
        const auto [context_entry, inserted] = component_contexts.emplace(component_id, context);
        if (!inserted && !context_entry->second.matches(context)) {
            return invalid_state(
                "AE component cannot aggregate storage cells from different resource snapshots");
        }

        auto index = prepared.indexes.find(component_id);
        if (index == prepared.indexes.end()) {
            auto created = std::make_unique<AeNetworkStorageIndex>(context);
            index = prepared.indexes.emplace(component_id, std::move(created)).first;
        }
        auto aggregate_handle = index->second->attach_storage(
            attachment.storage->capture_runtime_contents(context));
        if (!aggregate_handle) return aggregate_handle.error();
        prepared.attachments.push_back({
            .slot = slot_index,
            .component_id = component_id,
            .aggregate_handle = *aggregate_handle,
        });
    }
    return prepared;
}

void AeNetworkRuntimeService::commit_storage_indexes(
    PreparedStorageIndexes indexes) noexcept {
    clear_storage_observers();
    for (StorageAttachmentSlot& slot : storage_attachments_) {
        if (slot.storage == nullptr) continue;
        slot.component_id = 0;
        slot.aggregate_handle = {};
    }

    for (const PreparedStorageAttachment& prepared : indexes.attachments) {
        if (prepared.slot == 0 || prepared.slot >= storage_attachments_.size()) {
            SNT_LOG_ERROR("AE runtime prepared an invalid storage attachment slot=%u",
                          static_cast<unsigned int>(prepared.slot));
            continue;
        }
        StorageAttachmentSlot& slot = storage_attachments_[prepared.slot];
        const auto index = indexes.indexes.find(prepared.component_id);
        if (slot.storage == nullptr || index == indexes.indexes.end() ||
            !index->second->is_attached(prepared.aggregate_handle) ||
            !slot.storage->set_resource_aggregate_observer(prepared.aggregate_handle,
                                                            *index->second)) {
            SNT_LOG_ERROR("AE runtime could not publish storage attachment slot=%u component=%u",
                          static_cast<unsigned int>(prepared.slot),
                          static_cast<unsigned int>(prepared.component_id));
            continue;
        }
        slot.component_id = prepared.component_id;
        slot.aggregate_handle = prepared.aggregate_handle;
    }

    // Moving unique_ptr values preserves the observer addresses published
    // above. Old indexes are destroyed only after all cells have detached.
    component_storage_indexes_ = std::move(indexes.indexes);
}

void AeNetworkRuntimeService::clear_storage_observers() noexcept {
    for (StorageAttachmentSlot& slot : storage_attachments_) {
        if (slot.storage != nullptr) slot.storage->clear_resource_aggregate_observer();
    }
}

AeNetworkRuntimeService::StorageAttachmentSlot*
AeNetworkRuntimeService::find_storage_attachment(
    AeNetworkStorageAttachmentHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= storage_attachments_.size()) return nullptr;
    StorageAttachmentSlot& slot = storage_attachments_[handle.slot];
    if (slot.storage == nullptr || slot.generation != handle.generation) return nullptr;
    return &slot;
}

const AeNetworkRuntimeService::StorageAttachmentSlot*
AeNetworkRuntimeService::find_storage_attachment(
    AeNetworkStorageAttachmentHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= storage_attachments_.size()) return nullptr;
    const StorageAttachmentSlot& slot = storage_attachments_[handle.slot];
    if (slot.storage == nullptr || slot.generation != handle.generation) return nullptr;
    return &slot;
}

bool AeNetworkRuntimeService::is_storage_node_type(AeNetworkNodeType type) noexcept {
    return type == AeNetworkNodeType::kDrive || type == AeNetworkNodeType::kStorageBus;
}

void AeNetworkRuntimeService::rebuild_chunk_index(
    const RuntimeMap& runtimes, ChunkRuntimeIndex& chunk_index) {
    chunk_index.clear();
    chunk_index.reserve(runtimes.size());
    for (const auto& [anchor_id, runtime] : runtimes) {
        chunk_index[runtime.presentation.anchor_chunk].push_back(anchor_id);
    }
    for (auto& [chunk_key, anchors] : chunk_index) {
        static_cast<void>(chunk_key);
        std::sort(anchors.begin(), anchors.end());
    }
}

}  // namespace snt::game
