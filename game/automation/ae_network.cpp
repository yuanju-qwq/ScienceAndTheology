// AE network topology and aggregate storage index implementation.

#define SNT_LOG_CHANNEL "game.automation.ae"
#include "game/automation/ae_network.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool add_would_overflow(int64_t current, int64_t amount) noexcept {
    return amount > 0 && current > std::numeric_limits<int64_t>::max() - amount;
}

}  // namespace

AeNetworkTopology::AeNetworkTopology() {
    slots_.emplace_back();
}

snt::core::Expected<AeNetworkNodeHandle> AeNetworkTopology::add_node(
    AeNetworkNodeConfig config) {
    if (config.provided_channels < 0) {
        return invalid_argument("AE network node cannot provide a negative channel count");
    }
    if (!is_channel_provider(config.type) && config.provided_channels != 0) {
        return invalid_argument("Only an AE controller or channel provider may provide channels");
    }

    const int32_t channels = config.provided_channels == 0
        ? default_provided_channels(config.type)
        : config.provided_channels;
    uint32_t slot_index = 0;
    if (!reusable_slots_.empty()) {
        slot_index = reusable_slots_.back();
        reusable_slots_.pop_back();
    } else {
        if (slots_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE network exhausted runtime node handle slots");
        }
        slot_index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    NodeSlot& slot = slots_[slot_index];
    if (slot.occupied || slot.generation == 0 || !slot.neighbors.empty()) {
        return invalid_state("AE network encountered an unavailable node handle slot");
    }
    slot.type = config.type;
    slot.occupied = true;
    slot.enabled = config.enabled;
    slot.online = false;
    slot.component_id = 0;
    slot.provided_channels = channels;
    ++node_count_;
    rebuild_topology();
    return AeNetworkNodeHandle{.slot = slot_index, .generation = slot.generation};
}

bool AeNetworkTopology::remove_node(AeNetworkNodeHandle handle) noexcept {
    NodeSlot* const slot = find_slot(handle);
    if (slot == nullptr) return false;

    for (const uint32_t neighbor : slot->neighbors) {
        if (neighbor < slots_.size()) slots_[neighbor].neighbors.erase(handle.slot);
    }
    slot->neighbors.clear();
    slot->occupied = false;
    slot->enabled = false;
    slot->online = false;
    slot->component_id = 0;
    slot->provided_channels = 0;
    --node_count_;
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_slots_.push_back(handle.slot);
    }
    rebuild_topology();
    return true;
}

snt::core::Expected<void> AeNetworkTopology::connect(
    AeNetworkNodeHandle first, AeNetworkNodeHandle second) {
    if (first == second) {
        return invalid_argument("AE network cannot connect a node to itself");
    }
    NodeSlot* const first_slot = find_slot(first);
    NodeSlot* const second_slot = find_slot(second);
    if (first_slot == nullptr || second_slot == nullptr) {
        return invalid_state("AE network connect received a stale node handle");
    }
    if (!first_slot->neighbors.insert(second.slot).second) return {};
    second_slot->neighbors.insert(first.slot);
    rebuild_topology();
    return {};
}

bool AeNetworkTopology::disconnect(AeNetworkNodeHandle first,
                                   AeNetworkNodeHandle second) noexcept {
    NodeSlot* const first_slot = find_slot(first);
    NodeSlot* const second_slot = find_slot(second);
    if (first_slot == nullptr || second_slot == nullptr) return false;
    if (first_slot->neighbors.erase(second.slot) == 0) return false;
    second_slot->neighbors.erase(first.slot);
    rebuild_topology();
    return true;
}

snt::core::Expected<void> AeNetworkTopology::set_node_enabled(
    AeNetworkNodeHandle handle, bool enabled) {
    NodeSlot* const slot = find_slot(handle);
    if (slot == nullptr) return invalid_state("AE network enable received a stale node handle");
    if (slot->enabled == enabled) return {};
    slot->enabled = enabled;
    rebuild_topology();
    return {};
}

snt::core::Expected<void> AeNetworkTopology::set_provided_channels(
    AeNetworkNodeHandle handle, int32_t provided_channels) {
    NodeSlot* const slot = find_slot(handle);
    if (slot == nullptr) {
        return invalid_state("AE network channel update received a stale node handle");
    }
    if (!is_channel_provider(slot->type) || provided_channels < 0) {
        return invalid_argument("AE network channel count is invalid for this node type");
    }
    if (slot->provided_channels == provided_channels) return {};
    slot->provided_channels = provided_channels;
    rebuild_topology();
    return {};
}

std::optional<AeNetworkNodeState> AeNetworkTopology::find_node(
    AeNetworkNodeHandle handle) const noexcept {
    const NodeSlot* const slot = find_slot(handle);
    if (slot == nullptr) return std::nullopt;
    return AeNetworkNodeState{
        .handle = handle,
        .type = slot->type,
        .enabled = slot->enabled,
        .online = slot->online,
        .component_id = slot->component_id,
        .provided_channels = slot->provided_channels,
    };
}

std::optional<uint32_t> AeNetworkTopology::component_of(
    AeNetworkNodeHandle handle) const noexcept {
    const NodeSlot* const slot = find_slot(handle);
    if (slot == nullptr || !slot->enabled || slot->component_id == 0) return std::nullopt;
    return slot->component_id;
}

bool AeNetworkTopology::is_online(AeNetworkNodeHandle handle) const noexcept {
    const NodeSlot* const slot = find_slot(handle);
    return slot != nullptr && slot->online;
}

std::optional<AeNetworkComponentState> AeNetworkTopology::component_state(
    uint32_t component_id) const noexcept {
    const auto found = components_.find(component_id);
    if (found == components_.end()) return std::nullopt;
    return found->second;
}

bool AeNetworkTopology::are_connected(AeNetworkNodeHandle first,
                                      AeNetworkNodeHandle second) const noexcept {
    const NodeSlot* const first_slot = find_slot(first);
    const NodeSlot* const second_slot = find_slot(second);
    return first_slot != nullptr && second_slot != nullptr &&
        first_slot->neighbors.contains(second.slot);
}

AeNetworkTopology::NodeSlot* AeNetworkTopology::find_slot(
    AeNetworkNodeHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    NodeSlot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

const AeNetworkTopology::NodeSlot* AeNetworkTopology::find_slot(
    AeNetworkNodeHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    const NodeSlot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

bool AeNetworkTopology::is_channel_provider(AeNetworkNodeType type) noexcept {
    return type == AeNetworkNodeType::kController ||
        type == AeNetworkNodeType::kChannelProvider;
}

bool AeNetworkTopology::is_device(AeNetworkNodeType type) noexcept {
    switch (type) {
        case AeNetworkNodeType::kDrive:
        case AeNetworkNodeType::kStorageBus:
        case AeNetworkNodeType::kInterface:
        case AeNetworkNodeType::kTerminal:
            return true;
        case AeNetworkNodeType::kController:
        case AeNetworkNodeType::kChannelProvider:
        case AeNetworkNodeType::kCable:
            return false;
    }
    return false;
}

int32_t AeNetworkTopology::default_provided_channels(AeNetworkNodeType type) noexcept {
    return is_channel_provider(type) ? 32 : 0;
}

void AeNetworkTopology::rebuild_topology() noexcept {
    components_.clear();
    for (NodeSlot& slot : slots_) {
        slot.online = false;
        slot.component_id = 0;
    }

    for (uint32_t root = 1; root < slots_.size(); ++root) {
        NodeSlot& root_slot = slots_[root];
        if (!root_slot.occupied || !root_slot.enabled || root_slot.component_id != 0) {
            continue;
        }
        if (next_component_id_ == 0) {
            SNT_LOG_ERROR("AE network component id space exhausted; all future topology queries fail closed");
            break;
        }
        const uint32_t component_id = next_component_id_++;
        std::queue<uint32_t> pending;
        std::vector<uint32_t> members;
        root_slot.component_id = component_id;
        pending.push(root);
        while (!pending.empty()) {
            const uint32_t current = pending.front();
            pending.pop();
            members.push_back(current);
            for (const uint32_t neighbor : slots_[current].neighbors) {
                if (neighbor >= slots_.size()) continue;
                NodeSlot& neighbor_slot = slots_[neighbor];
                if (!neighbor_slot.occupied || !neighbor_slot.enabled ||
                    neighbor_slot.component_id != 0) {
                    continue;
                }
                neighbor_slot.component_id = component_id;
                pending.push(neighbor);
            }
        }

        std::sort(members.begin(), members.end());
        AeNetworkComponentState component{
            .id = component_id,
            .node_count = static_cast<uint32_t>(members.size()),
        };
        for (const uint32_t member : members) {
            const NodeSlot& slot = slots_[member];
            if (slot.type == AeNetworkNodeType::kController) ++component.controller_count;
            if (is_channel_provider(slot.type)) {
                component.total_channels = std::min(
                    std::numeric_limits<int32_t>::max(),
                    component.total_channels > std::numeric_limits<int32_t>::max() -
                            slot.provided_channels
                        ? std::numeric_limits<int32_t>::max()
                        : component.total_channels + slot.provided_channels);
            }
        }
        component.is_powered = component.controller_count == 1;
        if (!component.is_powered) component.total_channels = 0;

        int32_t remaining_channels = component.total_channels;
        for (const uint32_t member : members) {
            NodeSlot& slot = slots_[member];
            if (!component.is_powered) {
                slot.online = false;
                if (is_device(slot.type)) ++component.offline_devices;
                continue;
            }
            if (!is_device(slot.type)) {
                slot.online = true;
                continue;
            }
            if (remaining_channels > 0) {
                slot.online = true;
                --remaining_channels;
                ++component.online_devices;
            } else {
                slot.online = false;
                ++component.offline_devices;
            }
        }
        components_.emplace(component_id, component);
    }
    if (topology_revision_ != std::numeric_limits<uint64_t>::max()) ++topology_revision_;
}

AeNetworkStorageIndex::AeNetworkStorageIndex(ResourceKeyContext context)
    : context_(std::move(context)) {
    slots_.emplace_back();
}

snt::core::Expected<ResourceAggregateStorageHandle> AeNetworkStorageIndex::attach_storage(
    std::vector<ResourceStack> initial_contents) {
    if (!context_.is_valid()) {
        return invalid_state("AE network storage index requires a valid resource snapshot");
    }
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> validated;
    validated.reserve(initial_contents.size());
    for (const ResourceStack& stack : initial_contents) {
        if (!stack.is_valid() || validated.contains(stack.key)) {
            return invalid_argument("AE storage attachment contains an invalid or duplicate stack");
        }
        const int64_t aggregate = amount_of(context_, stack.key);
        if (add_would_overflow(aggregate, stack.amount)) {
            return invalid_state("AE storage attachment would overflow a network aggregate amount");
        }
        validated.emplace(stack.key, stack.amount);
    }

    uint32_t slot_index = 0;
    if (!reusable_slots_.empty()) {
        slot_index = reusable_slots_.back();
        reusable_slots_.pop_back();
    } else {
        if (slots_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("AE network storage index exhausted runtime handle slots");
        }
        slot_index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    StorageSlot& slot = slots_[slot_index];
    if (slot.occupied || slot.generation == 0 || !slot.amounts.empty()) {
        return invalid_state("AE network storage index encountered an unavailable handle slot");
    }
    slot.occupied = true;
    slot.amounts = std::move(validated);
    for (const auto& [key, amount] : slot.amounts) {
        aggregate_amounts_[key] += amount;
    }
    ++storage_count_;
    return ResourceAggregateStorageHandle{.slot = slot_index, .generation = slot.generation};
}

bool AeNetworkStorageIndex::detach_storage(ResourceAggregateStorageHandle handle) noexcept {
    StorageSlot* const slot = find_slot(handle);
    if (slot == nullptr) return false;
    for (const auto& [key, amount] : slot->amounts) {
        const auto aggregate = aggregate_amounts_.find(key);
        if (aggregate == aggregate_amounts_.end() || aggregate->second < amount) {
            SNT_LOG_ERROR("AE network storage index detected a corrupt aggregate while detaching slot=%u",
                          static_cast<unsigned int>(handle.slot));
            continue;
        }
        aggregate->second -= amount;
        if (aggregate->second == 0) aggregate_amounts_.erase(aggregate);
    }
    slot->amounts.clear();
    slot->occupied = false;
    --storage_count_;
    if (slot->generation != std::numeric_limits<uint32_t>::max()) {
        ++slot->generation;
        reusable_slots_.push_back(handle.slot);
    }
    return true;
}

bool AeNetworkStorageIndex::is_attached(ResourceAggregateStorageHandle handle) const noexcept {
    return find_slot(handle) != nullptr;
}

int64_t AeNetworkStorageIndex::amount_of(const ResourceKeyContext& context,
                                         const ResourceKey& key) const noexcept {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !key.is_valid()) {
        return 0;
    }
    const auto found = aggregate_amounts_.find(key);
    return found == aggregate_amounts_.end() ? 0 : found->second;
}

bool AeNetworkStorageIndex::can_apply_resource_aggregate_delta(
    ResourceAggregateStorageHandle handle,
    const ResourceKeyContext& context,
    const ResourceStack& changed,
    int64_t delta) const noexcept {
    const StorageSlot* const slot = find_slot(handle);
    if (slot == nullptr || !context_.is_valid() || !context.is_valid() ||
        !context_.matches(context) || !is_valid_delta(changed, delta)) {
        return false;
    }
    const auto storage_amount = slot->amounts.find(changed.key);
    const int64_t current_storage = storage_amount == slot->amounts.end()
        ? 0
        : storage_amount->second;
    const auto aggregate_amount = aggregate_amounts_.find(changed.key);
    const int64_t current_aggregate = aggregate_amount == aggregate_amounts_.end()
        ? 0
        : aggregate_amount->second;
    if (delta > 0) return !add_would_overflow(current_storage, delta) &&
        !add_would_overflow(current_aggregate, delta);
    const int64_t removed = -delta;
    return current_storage >= removed && current_aggregate >= removed;
}

void AeNetworkStorageIndex::apply_resource_aggregate_delta(
    ResourceAggregateStorageHandle handle,
    const ResourceKeyContext& context,
    const ResourceStack& changed,
    int64_t delta) noexcept {
    StorageSlot* const slot = find_slot(handle);
    if (slot == nullptr ||
        !can_apply_resource_aggregate_delta(handle, context, changed, delta)) {
        SNT_LOG_ERROR("AE network storage index rejected an unprepared storage mutation: slot=%u",
                      static_cast<unsigned int>(handle.slot));
        return;
    }
    apply_delta_unchecked(*slot, changed, delta);
}

AeNetworkStorageIndex::StorageSlot* AeNetworkStorageIndex::find_slot(
    ResourceAggregateStorageHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    StorageSlot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

const AeNetworkStorageIndex::StorageSlot* AeNetworkStorageIndex::find_slot(
    ResourceAggregateStorageHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    const StorageSlot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return &slot;
}

bool AeNetworkStorageIndex::is_valid_delta(const ResourceStack& changed,
                                           int64_t delta) noexcept {
    return changed.is_valid() && delta != 0 &&
        (delta == changed.amount || delta == -changed.amount);
}

void AeNetworkStorageIndex::apply_delta_unchecked(StorageSlot& slot,
                                                   const ResourceStack& changed,
                                                   int64_t delta) noexcept {
    int64_t& storage_amount = slot.amounts[changed.key];
    int64_t& aggregate_amount = aggregate_amounts_[changed.key];
    storage_amount += delta;
    aggregate_amount += delta;
    if (storage_amount == 0) slot.amounts.erase(changed.key);
    if (aggregate_amount == 0) aggregate_amounts_.erase(changed.key);
}

}  // namespace snt::game
