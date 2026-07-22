// Compact AE-style digital storage implementation.

#define SNT_LOG_CHANNEL "game.automation.ae_storage"
#include "game/automation/ae_storage_cell.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_valid_type_name(std::string_view type) noexcept {
    return !type.empty() && type.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool content_key_less(const ResourceContentStack& left,
                                    const ResourceContentStack& right) noexcept {
    if (left.key.type != right.key.type) return left.key.type < right.key.type;
    if (left.key.id != right.key.id) return left.key.id < right.key.id;
    return left.key.variant < right.key.variant;
}

}  // namespace

snt::core::Expected<AeStorageCell> AeStorageCell::create(
    AeStorageCellConfig config,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("AE storage cell requires a valid resource runtime snapshot");
    }
    if (config.byte_capacity <= 0 || config.max_distinct_resources == 0 ||
        config.bytes_per_distinct_resource <= 0 || config.units_per_byte <= 0) {
        return invalid_argument("AE storage cell requires positive capacity, type limit, and byte ratios");
    }
    if (config.bytes_per_distinct_resource > config.byte_capacity) {
        return invalid_argument("AE storage cell type overhead exceeds its byte capacity");
    }

    auto accepted_kinds = resolve_accepted_kinds(config, resource_runtime_index);
    if (!accepted_kinds) return accepted_kinds.error();
    return AeStorageCell{std::move(config), resource_runtime_index,
                         std::move(*accepted_kinds)};
}

snt::core::Expected<AeStorageCell> AeStorageCell::restore_persistence_record(
    AeStorageCellConfig config,
    const AeStorageCellPersistenceRecord& record,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    auto created = create(std::move(config), resource_runtime_index);
    if (!created) return created.error();
    AeStorageCell cell = std::move(*created);

    if (record.stored_resources.size() > cell.config_.max_distinct_resources) {
        SNT_LOG_WARN("AE storage persistence restore rejected %zu entries for a %u-type cell",
                     record.stored_resources.size(),
                     static_cast<unsigned int>(cell.config_.max_distinct_resources));
        return invalid_argument("AE storage persistence exceeds the configured distinct-resource limit");
    }

    std::unordered_set<ResourceContentKey, ResourceContentKey::Hash> seen_keys;
    seen_keys.reserve(record.stored_resources.size());
    for (size_t index = 0; index < record.stored_resources.size(); ++index) {
        const ResourceContentStack& stored = record.stored_resources[index];
        if (!stored.is_valid()) {
            SNT_LOG_WARN("AE storage persistence restore rejected invalid entry at index %zu", index);
            return invalid_argument("AE storage persistence contains an invalid resource stack");
        }
        if (!seen_keys.insert(stored.key).second) {
            SNT_LOG_WARN("AE storage persistence restore rejected duplicate entry at index %zu", index);
            return invalid_argument("AE storage persistence contains duplicate resource keys");
        }

        const auto runtime = resolve_resource_stack(stored, resource_runtime_index);
        if (!runtime) {
            SNT_LOG_WARN("AE storage persistence restore could not resolve %s:%s at index %zu",
                         stored.key.type.c_str(), stored.key.id.c_str(), index);
            return invalid_state("AE storage persistence contains an unresolved resource key");
        }
        const int64_t inserted = cell.insert(
            cell.context_, *runtime, ResourceTransferMode::kExecute);
        if (inserted != runtime->amount) {
            SNT_LOG_WARN("AE storage persistence restore rejected %s:%s at index %zu; "
                         "expected=%lld restored=%lld",
                         stored.key.type.c_str(), stored.key.id.c_str(), index,
                         static_cast<long long>(runtime->amount),
                         static_cast<long long>(inserted));
            return invalid_state("AE storage persistence does not fit the configured cell");
        }
    }
    return cell;
}

AeStorageCell::AeStorageCell(AeStorageCellConfig config,
                             ResourceRuntimeIndex::Snapshot resource_runtime_index,
                             std::unordered_set<ResourceKind> accepted_kinds)
    : config_(std::move(config)),
      resource_runtime_index_(std::move(resource_runtime_index)),
      context_(resource_runtime_index_.key_context()),
      accepted_kinds_(std::move(accepted_kinds)) {}

ResourceKeyContext AeStorageCell::key_context() const noexcept {
    return context_;
}

snt::core::Expected<AeStorageCellPersistenceRecord>
AeStorageCell::capture_persistence_record() const {
    if (!context_.is_valid() ||
        !context_.matches(resource_runtime_index_.key_context())) {
        return invalid_state("AE storage cell cannot persist a mismatched resource snapshot");
    }

    AeStorageCellPersistenceRecord record;
    record.stored_resources.reserve(amounts_.size());
    for (const auto& [key, amount] : amounts_) {
        const ResourceStack runtime{.key = key, .amount = amount};
        const auto content = resolve_content_stack(runtime, resource_runtime_index_);
        if (!content || !content->is_valid()) {
            return invalid_state("AE storage cell cannot persist an unresolved compact resource key");
        }
        record.stored_resources.push_back(*content);
    }
    std::sort(record.stored_resources.begin(), record.stored_resources.end(), content_key_less);
    return record;
}

int64_t AeStorageCell::amount_of(const ResourceKeyContext& context,
                                 const ResourceKey& key) const {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !key.is_valid() || !accepts_key(key)) {
        return 0;
    }
    const auto found = amounts_.find(key);
    return found == amounts_.end() ? 0 : found->second;
}

int64_t AeStorageCell::insert(const ResourceKeyContext& context,
                              const ResourceStack& stack,
                              ResourceTransferMode mode) {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !stack.is_valid() || !accepts_key(stack.key)) {
        return 0;
    }

    const auto found = amounts_.find(stack.key);
    const bool is_new_resource = found == amounts_.end();
    const int64_t current_amount = is_new_resource ? 0 : found->second;
    if (is_new_resource && amounts_.size() >= config_.max_distinct_resources) return 0;

    int64_t free_byte_count = free_bytes();
    if (is_new_resource) {
        if (free_byte_count <= config_.bytes_per_distinct_resource) return 0;
        free_byte_count -= config_.bytes_per_distinct_resource;
    }

    const int64_t current_data_bytes =
        bytes_for_amount(current_amount, config_.units_per_byte);
    const int64_t maximum_data_bytes = current_data_bytes + free_byte_count;
    const int64_t maximum_stored_amount =
        saturating_multiply(maximum_data_bytes, config_.units_per_byte);
    if (maximum_stored_amount <= current_amount) return 0;
    const int64_t accepted = std::min(stack.amount, maximum_stored_amount - current_amount);
    if (accepted <= 0 || mode == ResourceTransferMode::kSimulate) return accepted;
    const ResourceStack changed{.key = stack.key, .amount = accepted};
    if (!can_apply_network_delta(changed, accepted)) {
        SNT_LOG_ERROR("AE storage insert rejected because its network aggregate is not prepared");
        return 0;
    }

    const int64_t next_amount = current_amount + accepted;
    const int64_t next_data_bytes = bytes_for_amount(next_amount, config_.units_per_byte);
    if (is_new_resource) {
        amounts_.emplace(stack.key, next_amount);
        used_bytes_ += config_.bytes_per_distinct_resource;
    } else {
        found->second = next_amount;
    }
    used_bytes_ += next_data_bytes - current_data_bytes;
    apply_network_delta(changed, accepted);
    return accepted;
}

int64_t AeStorageCell::extract(const ResourceKeyContext& context,
                               const ResourceStack& requested,
                               ResourceTransferMode mode) {
    if (!context_.is_valid() || !context.is_valid() || !context_.matches(context) ||
        !requested.is_valid() || !accepts_key(requested.key)) {
        return 0;
    }
    const auto found = amounts_.find(requested.key);
    if (found == amounts_.end()) return 0;

    const int64_t extracted = std::min(found->second, requested.amount);
    if (extracted <= 0 || mode == ResourceTransferMode::kSimulate) return extracted;
    const ResourceStack changed{.key = requested.key, .amount = extracted};
    if (!can_apply_network_delta(changed, -extracted)) {
        SNT_LOG_ERROR("AE storage extract rejected because its network aggregate is not prepared");
        return 0;
    }

    const int64_t current_data_bytes =
        bytes_for_amount(found->second, config_.units_per_byte);
    const int64_t next_amount = found->second - extracted;
    const int64_t next_data_bytes =
        bytes_for_amount(next_amount, config_.units_per_byte);
    used_bytes_ -= current_data_bytes - next_data_bytes;
    if (next_amount == 0) {
        used_bytes_ -= config_.bytes_per_distinct_resource;
        amounts_.erase(found);
    } else {
        found->second = next_amount;
    }
    apply_network_delta(changed, -extracted);
    return extracted;
}

std::vector<ResourceKey> AeStorageCell::stored_keys(const ResourceKeyContext& context) const {
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

std::vector<ResourceStack> AeStorageCell::capture_runtime_contents(
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

bool AeStorageCell::set_resource_aggregate_observer(
    ResourceAggregateStorageHandle handle,
    IResourceAggregateMutationObserver& observer) noexcept {
    if (!handle.is_valid() || !context_.is_valid()) return false;
    if (resource_aggregate_observer_ != nullptr) {
        SNT_LOG_ERROR("AE storage cell rejected a second aggregate observer without an explicit detach");
        return false;
    }
    resource_aggregate_handle_ = handle;
    resource_aggregate_observer_ = &observer;
    return true;
}

void AeStorageCell::clear_resource_aggregate_observer() noexcept {
    resource_aggregate_observer_ = nullptr;
    resource_aggregate_handle_ = {};
}

snt::core::Expected<void> AeStorageCell::rebind(
    const ResourceRuntimeIndex::Snapshot& previous_resource_runtime_index,
    const ResourceRuntimeIndex::Snapshot& next_resource_runtime_index) {
    if (pending_resource_runtime_snapshot_) {
        return invalid_state("AE storage cell cannot rebind while a snapshot is pending");
    }
    if (has_resource_aggregate_observer()) {
        return invalid_state("AE storage cell must detach from its network aggregate before rebind");
    }
    auto rebound = build_rebound_snapshot(
        previous_resource_runtime_index, next_resource_runtime_index);
    if (!rebound) return rebound.error();
    install_rebound_snapshot(std::move(*rebound));
    return {};
}

snt::core::Expected<void> AeStorageCell::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (pending_resource_runtime_snapshot_) {
        return invalid_state("AE storage cell already has a pending resource snapshot");
    }
    if (has_resource_aggregate_observer()) {
        return invalid_state("AE storage cell must detach from its network aggregate before reload");
    }
    auto rebound = build_rebound_snapshot(resource_runtime_index_, next_snapshot);
    if (!rebound) return rebound.error();
    pending_resource_runtime_snapshot_ = std::move(*rebound);
    return {};
}

void AeStorageCell::commit_resource_runtime_snapshot() noexcept {
    if (!pending_resource_runtime_snapshot_) return;
    install_rebound_snapshot(std::move(*pending_resource_runtime_snapshot_));
    pending_resource_runtime_snapshot_.reset();
}

void AeStorageCell::cancel_resource_runtime_snapshot() noexcept {
    pending_resource_runtime_snapshot_.reset();
}

snt::core::Expected<AeStorageCell::PendingResourceRuntimeSnapshot>
AeStorageCell::build_rebound_snapshot(
    const ResourceRuntimeIndex::Snapshot& previous_resource_runtime_index,
    const ResourceRuntimeIndex::Snapshot& next_resource_runtime_index) const {
    const ResourceKeyContext previous_context = previous_resource_runtime_index.key_context();
    const ResourceKeyContext next_context = next_resource_runtime_index.key_context();
    if (!context_.is_valid() || !context_.matches(previous_context)) {
        return invalid_state("AE storage cell rebind received a mismatched previous snapshot");
    }
    if (!next_context.is_valid()) {
        return invalid_argument("AE storage cell rebind requires a valid next resource snapshot");
    }
    auto next_accepted_kinds =
        resolve_accepted_kinds(config_, next_resource_runtime_index);
    if (!next_accepted_kinds) return next_accepted_kinds.error();

    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> rebound_amounts;
    rebound_amounts.reserve(amounts_.size());
    int64_t rebound_used_bytes = 0;
    for (const auto& [key, amount] : amounts_) {
        const auto rebound = rebind_resource_stack(
            ResourceStack{.key = key, .amount = amount},
            previous_resource_runtime_index, next_resource_runtime_index);
        if (!rebound || !accepts_key(rebound->key, config_.accepted_resource_types.empty(),
                                     *next_accepted_kinds)) {
            return invalid_state("AE storage cell rebind could not resolve an accepted stored key");
        }
        const bool was_inserted =
            rebound_amounts.emplace(rebound->key, rebound->amount).second;
        if (!was_inserted || rebound_amounts.size() > config_.max_distinct_resources) {
            return invalid_state("AE storage cell rebind produced a duplicate or excess resource key");
        }

        const int64_t data_bytes = bytes_for_amount(
            rebound->amount, config_.units_per_byte);
        if (data_bytes > config_.byte_capacity - rebound_used_bytes) {
            return invalid_state("AE storage cell rebind would exceed byte capacity");
        }
        rebound_used_bytes += data_bytes;
        if (config_.bytes_per_distinct_resource >
            config_.byte_capacity - rebound_used_bytes) {
            return invalid_state("AE storage cell rebind would exceed type overhead capacity");
        }
        rebound_used_bytes += config_.bytes_per_distinct_resource;
    }

    PendingResourceRuntimeSnapshot pending;
    pending.resource_runtime_index = next_resource_runtime_index;
    pending.context = next_context;
    pending.accepted_kinds = std::move(*next_accepted_kinds);
    pending.amounts = std::move(rebound_amounts);
    pending.used_bytes = rebound_used_bytes;
    return pending;
}

void AeStorageCell::install_rebound_snapshot(PendingResourceRuntimeSnapshot snapshot) noexcept {
    resource_runtime_index_ = std::move(snapshot.resource_runtime_index);
    context_ = std::move(snapshot.context);
    accepted_kinds_ = std::move(snapshot.accepted_kinds);
    amounts_ = std::move(snapshot.amounts);
    used_bytes_ = snapshot.used_bytes;
}

snt::core::Expected<std::unordered_set<ResourceKind>> AeStorageCell::resolve_accepted_kinds(
    const AeStorageCellConfig& config,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    std::unordered_set<ResourceKind> accepted_kinds;
    if (config.accepted_resource_types.empty()) return accepted_kinds;
    if (!resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("AE storage cell type filter requires a valid resource snapshot");
    }
    accepted_kinds.reserve(config.accepted_resource_types.size());
    for (const std::string& type : config.accepted_resource_types) {
        if (!is_valid_type_name(type)) {
            return invalid_argument("AE storage cell contains an invalid accepted resource type");
        }
        const auto kind = resource_runtime_index.resource_kind(type);
        if (!kind || *kind == kInvalidResourceKind) {
            return invalid_argument("AE storage cell accepted resource type is absent from snapshot: " +
                                    type);
        }
        accepted_kinds.insert(*kind);
    }
    return accepted_kinds;
}

int64_t AeStorageCell::bytes_for_amount(int64_t amount, int64_t units_per_byte) noexcept {
    if (amount <= 0) return 0;
    return 1 + (amount - 1) / units_per_byte;
}

int64_t AeStorageCell::saturating_multiply(int64_t left, int64_t right) noexcept {
    if (left <= 0 || right <= 0) return 0;
    if (left > std::numeric_limits<int64_t>::max() / right) {
        return std::numeric_limits<int64_t>::max();
    }
    return left * right;
}

bool AeStorageCell::accepts_key(const ResourceKey& key) const noexcept {
    return accepts_key(key, config_.accepted_resource_types.empty(), accepted_kinds_);
}

bool AeStorageCell::accepts_key(
    const ResourceKey& key,
    bool accepts_all_resource_types,
    const std::unordered_set<ResourceKind>& accepted_kinds) noexcept {
    return key.is_valid() &&
           (accepts_all_resource_types || accepted_kinds.contains(key.kind));
}

bool AeStorageCell::can_apply_network_delta(
    const ResourceStack& changed, int64_t delta) const noexcept {
    return resource_aggregate_observer_ == nullptr ||
        resource_aggregate_observer_->can_apply_resource_aggregate_delta(
            resource_aggregate_handle_, context_, changed, delta);
}

void AeStorageCell::apply_network_delta(
    const ResourceStack& changed, int64_t delta) noexcept {
    if (resource_aggregate_observer_ == nullptr) return;
    resource_aggregate_observer_->apply_resource_aggregate_delta(
        resource_aggregate_handle_, context_, changed, delta);
}

}  // namespace snt::game
