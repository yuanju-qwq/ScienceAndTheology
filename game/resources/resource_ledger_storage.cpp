// Snapshot-bound aggregate ResourceStack storage implementation.

#define SNT_LOG_CHANNEL "game.resources.ledger"
#include "game/resources/resource_ledger_storage.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
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

ResourceLedgerStorage::ResourceLedgerStorage(ResourceKeyContext context)
    : context_(std::move(context)) {}

ResourceKeyContext ResourceLedgerStorage::key_context() const noexcept {
    return context_;
}

int64_t ResourceLedgerStorage::amount_of(const ResourceKeyContext& context,
                                         const ResourceKey& key) const {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !key.is_valid()) {
        return 0;
    }
    const auto found = amounts_.find(key);
    return found == amounts_.end() ? 0 : found->second;
}

int64_t ResourceLedgerStorage::insert(const ResourceKeyContext& context,
                                      const ResourceStack& stack,
                                      ResourceTransferMode mode) {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !stack.is_valid()) {
        return 0;
    }
    const int64_t current = amount_of(context, stack.key);
    const int64_t accepted = std::min(stack.amount,
                                      std::numeric_limits<int64_t>::max() - current);
    if (accepted <= 0 || mode == ResourceTransferMode::kSimulate) return accepted;
    const ResourceStack changed{.key = stack.key, .amount = accepted};
    if (!can_apply_resource_aggregate_delta(changed, accepted)) {
        SNT_LOG_ERROR("Resource ledger insert rejected because its aggregate index is unavailable");
        return 0;
    }
    amounts_[stack.key] = current + accepted;
    apply_resource_aggregate_delta(changed, accepted);
    return accepted;
}

int64_t ResourceLedgerStorage::extract(const ResourceKeyContext& context,
                                       const ResourceStack& requested,
                                       ResourceTransferMode mode) {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !requested.is_valid()) {
        return 0;
    }
    const int64_t extracted = std::min(amount_of(context, requested.key), requested.amount);
    if (extracted <= 0 || mode == ResourceTransferMode::kSimulate) return extracted;
    const auto found = amounts_.find(requested.key);
    if (found == amounts_.end()) return 0;
    const ResourceStack changed{.key = requested.key, .amount = extracted};
    if (!can_apply_resource_aggregate_delta(changed, -extracted)) {
        SNT_LOG_ERROR("Resource ledger extract rejected because its aggregate index is unavailable");
        return 0;
    }
    found->second -= extracted;
    if (found->second == 0) amounts_.erase(found);
    apply_resource_aggregate_delta(changed, -extracted);
    return extracted;
}

std::vector<ResourceKey> ResourceLedgerStorage::stored_keys(
    const ResourceKeyContext& context) const {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context)) return {};
    std::vector<ResourceKey> keys;
    keys.reserve(amounts_.size());
    for (const auto& [key, amount] : amounts_) {
        if (amount > 0) keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end(), [](const ResourceKey& left, const ResourceKey& right) {
        if (left.kind != right.kind) return left.kind < right.kind;
        if (left.runtime_id != right.runtime_id) return left.runtime_id < right.runtime_id;
        return left.variant < right.variant;
    });
    return keys;
}

std::vector<ResourceStack> ResourceLedgerStorage::capture_runtime_contents(
    const ResourceKeyContext& context) const {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context)) return {};
    std::vector<ResourceStack> contents;
    contents.reserve(amounts_.size());
    for (const auto& [key, amount] : amounts_) {
        if (amount > 0) contents.push_back({.key = key, .amount = amount});
    }
    std::sort(contents.begin(), contents.end(), [](const ResourceStack& left,
                                                   const ResourceStack& right) {
        if (left.key.kind != right.key.kind) return left.key.kind < right.key.kind;
        if (left.key.runtime_id != right.key.runtime_id) {
            return left.key.runtime_id < right.key.runtime_id;
        }
        return left.key.variant < right.key.variant;
    });
    return contents;
}

bool ResourceLedgerStorage::set_resource_aggregate_observer(
    ResourceAggregateStorageHandle handle,
    IResourceAggregateMutationObserver& observer) noexcept {
    if (!handle.is_valid() || !context_.is_valid() || resource_aggregate_observer_ != nullptr) {
        return false;
    }
    resource_aggregate_handle_ = handle;
    resource_aggregate_observer_ = &observer;
    return true;
}

void ResourceLedgerStorage::clear_resource_aggregate_observer() noexcept {
    resource_aggregate_observer_ = nullptr;
    resource_aggregate_handle_ = {};
}

snt::core::Expected<void> ResourceLedgerStorage::rebind(
    const IResourceKeyResolver& previous_resolver,
    const IResourceKeyResolver& next_resolver) {
    const ResourceKeyContext previous_context = previous_resolver.key_context();
    const ResourceKeyContext next_context = next_resolver.key_context();
    if (!context_.is_valid() || !context_.matches(previous_context)) {
        return invalid_state("Resource ledger rebind received a mismatched previous snapshot");
    }
    if (!next_context.is_valid()) {
        return invalid_argument("Resource ledger rebind requires a valid next snapshot");
    }
    if (has_resource_aggregate_observer()) {
        return invalid_state("Resource ledger must detach from its aggregate before rebind");
    }
    if (context_.matches(next_context)) return {};

    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> rebound;
    rebound.reserve(amounts_.size());
    for (const auto& [key, amount] : amounts_) {
        const auto stack = rebind_resource_stack(
            ResourceStack{.key = key, .amount = amount}, previous_resolver, next_resolver);
        if (!stack) {
            return invalid_state("Resource ledger rebind could not resolve a stored key");
        }
        const int64_t current = rebound[stack->key];
        if (stack->amount > std::numeric_limits<int64_t>::max() - current) {
            return invalid_state("Resource ledger rebind would overflow a stored amount");
        }
        rebound[stack->key] = current + stack->amount;
    }

    amounts_ = std::move(rebound);
    context_ = next_context;
    return {};
}

bool ResourceLedgerStorage::can_apply_resource_aggregate_delta(
    const ResourceStack& changed, int64_t delta) const noexcept {
    return resource_aggregate_observer_ == nullptr ||
        resource_aggregate_observer_->can_apply_resource_aggregate_delta(
            resource_aggregate_handle_, context_, changed, delta);
}

void ResourceLedgerStorage::apply_resource_aggregate_delta(
    const ResourceStack& changed, int64_t delta) noexcept {
    if (resource_aggregate_observer_ == nullptr) return;
    resource_aggregate_observer_->apply_resource_aggregate_delta(
        resource_aggregate_handle_, context_, changed, delta);
}

}  // namespace snt::game
