// SFM endpoint binding and compact transfer scheduling implementation.

#define SNT_LOG_CHANNEL "game.automation.sfm"
#include "game/automation/sfm_endpoint_registry.h"

#include "core/error.h"
#include "core/log.h"

#include <limits>
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

}  // namespace

bool SfmEndpointAddress::is_valid() const noexcept {
    return !value.empty() && value.find('\0') == std::string::npos;
}

size_t SfmEndpointAddress::Hash::operator()(const SfmEndpointAddress& address) const noexcept {
    return std::hash<std::string>{}(address.value);
}

SfmEndpointRegistry::SfmEndpointRegistry() {
    slots_.emplace_back();
}

snt::core::Expected<SfmEndpointHandle> SfmEndpointRegistry::register_endpoint(
    SfmEndpointAddress address,
    IResourceStorage& storage) {
    if (!address.is_valid()) {
        return invalid_argument("SFM endpoint registration requires a stable non-empty address");
    }
    if (!storage.key_context().is_valid()) {
        return invalid_argument("SFM endpoint registration requires a storage bound to a resource snapshot");
    }
    if (endpoint_slots_.contains(address)) {
        return invalid_state("SFM endpoint address is already registered");
    }

    uint32_t slot_index = 0;
    if (!reusable_slots_.empty()) {
        slot_index = reusable_slots_.back();
        reusable_slots_.pop_back();
    } else {
        if (slots_.size() > std::numeric_limits<uint32_t>::max()) {
            return invalid_state("SFM endpoint registry exhausted runtime handle slots");
        }
        slot_index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    Slot& slot = slots_[slot_index];
    if (slot.storage != nullptr || slot.generation == 0) {
        return invalid_state("SFM endpoint registry encountered an unavailable runtime handle slot");
    }
    slot.storage = &storage;
    slot.address = std::move(address);
    const auto [entry, inserted] = endpoint_slots_.emplace(slot.address, slot_index);
    if (!inserted) {
        slot.storage = nullptr;
        slot.address = {};
        reusable_slots_.push_back(slot_index);
        return invalid_state("SFM endpoint registry could not publish a unique endpoint address");
    }
    static_cast<void>(entry);
    return SfmEndpointHandle{.slot = slot_index, .generation = slot.generation};
}

bool SfmEndpointRegistry::unregister_endpoint(SfmEndpointHandle handle) noexcept {
    Slot* slot = find_slot(handle);
    if (slot == nullptr) return false;

    endpoint_slots_.erase(slot->address);
    slot->storage = nullptr;
    slot->address = {};
    if (slot->generation == std::numeric_limits<uint32_t>::max()) {
        // Never wrap a generation: retaining this retired slot is cheaper
        // than allowing an old compiled rule to become valid again.
        return true;
    }
    ++slot->generation;
    reusable_slots_.push_back(handle.slot);
    return true;
}

std::optional<SfmEndpointHandle> SfmEndpointRegistry::resolve_endpoint(
    const SfmEndpointAddress& address) const {
    if (!address.is_valid()) return std::nullopt;
    const auto found = endpoint_slots_.find(address);
    if (found == endpoint_slots_.end()) return std::nullopt;
    const uint32_t slot_index = found->second;
    if (slot_index >= slots_.size()) return std::nullopt;
    const Slot& slot = slots_[slot_index];
    if (slot.storage == nullptr || slot.generation == 0) return std::nullopt;
    return SfmEndpointHandle{.slot = slot_index, .generation = slot.generation};
}

IResourceStorage* SfmEndpointRegistry::find_endpoint(SfmEndpointHandle handle) const noexcept {
    const Slot* slot = find_slot(handle);
    return slot == nullptr ? nullptr : slot->storage;
}

snt::core::Expected<SfmBoundResourceTransfer> SfmEndpointRegistry::compile_transfer(
    const SfmResourceTransferRule& rule,
    const IResourceKeyResolver& resource_resolver) const {
    if (!rule.is_valid()) {
        return invalid_argument("SFM transfer rule requires valid endpoint addresses and resource stack");
    }
    const ResourceKeyContext context = resource_resolver.key_context();
    if (!context.is_valid()) {
        return invalid_argument("SFM transfer compilation requires a valid resource snapshot");
    }

    const auto source = resolve_endpoint(rule.source);
    const auto destination = resolve_endpoint(rule.destination);
    if (!source || !destination) {
        return invalid_state("SFM transfer rule refers to an unavailable endpoint");
    }
    IResourceStorage* const source_storage = find_endpoint(*source);
    IResourceStorage* const destination_storage = find_endpoint(*destination);
    if (source_storage == nullptr || destination_storage == nullptr ||
        !source_storage->key_context().matches(context) ||
        !destination_storage->key_context().matches(context)) {
        return invalid_state("SFM transfer endpoints do not share the compiled resource snapshot");
    }

    const auto requested = resolve_resource_stack(rule.requested, resource_resolver);
    if (!requested) {
        return invalid_state("SFM transfer rule contains an unresolved resource key");
    }
    return SfmBoundResourceTransfer{
        .source = *source,
        .destination = *destination,
        .requested = *requested,
        .resource_context = context,
    };
}

snt::core::Expected<SfmResourceTransferResult> SfmEndpointRegistry::execute_transfer(
    const ResourceKeyContext& context,
    const SfmBoundResourceTransfer& transfer,
    ResourceTransferMode mode) const {
    if (!context.is_valid() || !transfer.is_valid()) {
        return invalid_argument("SFM compiled transfer requires a valid context and compact stack");
    }
    if (!transfer.resource_context.matches(context)) {
        return invalid_state("SFM compiled transfer was built for another resource snapshot");
    }
    IResourceStorage* const source = find_endpoint(transfer.source);
    IResourceStorage* const destination = find_endpoint(transfer.destination);
    if (source == nullptr || destination == nullptr) {
        SNT_LOG_WARN("SFM compiled transfer rejected a stale endpoint handle: "
                     "source_slot=%u destination_slot=%u",
                     static_cast<unsigned int>(transfer.source.slot),
                     static_cast<unsigned int>(transfer.destination.slot));
        return invalid_state("SFM compiled transfer refers to a stale endpoint handle");
    }
    return SfmResourceTransfer::transfer(*source, *destination, context, transfer.requested, mode);
}

SfmEndpointRegistry::Slot* SfmEndpointRegistry::find_slot(
    SfmEndpointHandle handle) noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    Slot& slot = slots_[handle.slot];
    if (slot.storage == nullptr || slot.generation != handle.generation) return nullptr;
    return &slot;
}

const SfmEndpointRegistry::Slot* SfmEndpointRegistry::find_slot(
    SfmEndpointHandle handle) const noexcept {
    if (!handle.is_valid() || handle.slot >= slots_.size()) return nullptr;
    const Slot& slot = slots_[handle.slot];
    if (slot.storage == nullptr || slot.generation != handle.generation) return nullptr;
    return &slot;
}

}  // namespace snt::game
