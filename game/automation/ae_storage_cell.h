// Compact AE-style digital storage.
//
// AeStorageCell is a game automation consumer of the generic resource
// contract. Live transfers use only ResourceKey/ResourceStack values from one
// ResourceRuntimeIndex snapshot. Stable content type strings are retained only
// in the cell definition and resolved to numeric ResourceKind filters at cell
// creation or reload, never while inserting or extracting.

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"
#include "game/resources/resource_aggregate_storage.h"
#include "game/resources/resource_runtime_index.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace snt::game {

// Authored cell configuration. The resource-type allowlist is content-facing
// configuration, not hot-path state: create() and rebind() resolve it to
// compact ResourceKind values before the cell becomes live.
struct AeStorageCellConfig {
    int64_t byte_capacity = 0;
    uint32_t max_distinct_resources = 0;
    int64_t bytes_per_distinct_resource = 8;
    int64_t units_per_byte = 1;
    std::vector<std::string> accepted_resource_types;
};

// Durable contents of one AE storage cell. The drive configuration belongs to
// authored content and is deliberately supplied again during restore, so a
// save never persists compact runtime IDs or silently keeps an obsolete drive
// definition after a content update.
struct AeStorageCellPersistenceRecord {
    std::vector<ResourceContentStack> stored_resources;
};

// Snapshot-bound AE drive cell. The amount map and used-byte counter make
// amount_of(), insert(), and extract() expected O(1) operations. Enumeration
// remains an inspection/UI boundary and is therefore intentionally separate.
class AeStorageCell final : public IResourceAggregateStorage,
                            public IResourceRuntimeSnapshotParticipant {
public:
    [[nodiscard]] static snt::core::Expected<AeStorageCell> create(
        AeStorageCellConfig config,
        const ResourceRuntimeIndex::Snapshot& resource_runtime_index);

    // Converts durable content stacks to compact keys once while loading. A
    // malformed, unresolved, filtered, duplicate, or over-capacity entry
    // rejects the whole restore; the caller retains the original record and
    // no partially restored live cell escapes this function.
    [[nodiscard]] static snt::core::Expected<AeStorageCell> restore_persistence_record(
        AeStorageCellConfig config,
        const AeStorageCellPersistenceRecord& record,
        const ResourceRuntimeIndex::Snapshot& resource_runtime_index);

    AeStorageCell(const AeStorageCell&) = delete;
    AeStorageCell& operator=(const AeStorageCell&) = delete;
    AeStorageCell(AeStorageCell&&) noexcept = default;
    AeStorageCell& operator=(AeStorageCell&&) noexcept = default;

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override;
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const override;
    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override;
    [[nodiscard]] int64_t extract(const ResourceKeyContext& context,
                                  const ResourceStack& requested,
                                  ResourceTransferMode mode) override;
    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const override;

    // Captures compact values for an AE network attachment boundary.  This
    // enumeration is intentionally O(n) and must only run while attaching,
    // detaching, or rebuilding an aggregate network index, never per tick.
    [[nodiscard]] std::vector<ResourceStack> capture_runtime_contents(
        const ResourceKeyContext& context) const override;

    // Aggregate ownership is opt-in. Once attached, every execute-mode
    // insert/extract preflights and publishes exactly one delta to the owning
    // index. The owner must detach before content reload because compact keys
    // are snapshot-scoped and the aggregate is rebuilt at that boundary.
    [[nodiscard]] bool set_resource_aggregate_observer(
        ResourceAggregateStorageHandle handle,
        IResourceAggregateMutationObserver& observer) noexcept override;
    void clear_resource_aggregate_observer() noexcept override;
    [[nodiscard]] bool has_resource_aggregate_observer() const noexcept override {
        return resource_aggregate_observer_ != nullptr;
    }

    [[nodiscard]] int64_t byte_capacity() const noexcept {
        return config_.byte_capacity;
    }
    [[nodiscard]] int64_t used_bytes() const noexcept { return used_bytes_; }
    [[nodiscard]] int64_t free_bytes() const noexcept {
        return config_.byte_capacity - used_bytes_;
    }
    [[nodiscard]] size_t distinct_resource_count() const noexcept {
        return amounts_.size();
    }

    // Converts compact live contents back to stable keys for a save or other
    // durable boundary. The output is sorted deterministically; it is not a
    // runtime enumeration API and therefore may be O(n log n).
    [[nodiscard]] snt::core::Expected<AeStorageCellPersistenceRecord>
    capture_persistence_record() const;

    // Rebinds every retained compact key through its stable content identity.
    // This is intentionally a reload-boundary O(n) operation; normal transfer
    // methods never do this conversion or scan the stored resource map.
    [[nodiscard]] snt::core::Expected<void> rebind(
        const ResourceRuntimeIndex::Snapshot& previous_resource_runtime_index,
        const ResourceRuntimeIndex::Snapshot& next_resource_runtime_index);

    // Allows the live cell to be registered directly with GameContentRegistry.
    // prepare() constructs all rebound state without touching live amounts;
    // commit() only installs that prebuilt state after the whole registry has
    // accepted the candidate snapshot.
    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

private:
    struct PendingResourceRuntimeSnapshot {
        ResourceRuntimeIndex::Snapshot resource_runtime_index;
        ResourceKeyContext context;
        std::unordered_set<ResourceKind> accepted_kinds;
        std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts;
        int64_t used_bytes = 0;
    };

    AeStorageCell(AeStorageCellConfig config,
                  ResourceRuntimeIndex::Snapshot resource_runtime_index,
                  std::unordered_set<ResourceKind> accepted_kinds);

    [[nodiscard]] static snt::core::Expected<std::unordered_set<ResourceKind>>
    resolve_accepted_kinds(const AeStorageCellConfig& config,
                           const ResourceRuntimeIndex::Snapshot& resource_runtime_index);
    [[nodiscard]] static int64_t bytes_for_amount(
        int64_t amount, int64_t units_per_byte) noexcept;
    [[nodiscard]] static int64_t saturating_multiply(
        int64_t left, int64_t right) noexcept;
    [[nodiscard]] snt::core::Expected<PendingResourceRuntimeSnapshot>
    build_rebound_snapshot(
        const ResourceRuntimeIndex::Snapshot& previous_resource_runtime_index,
        const ResourceRuntimeIndex::Snapshot& next_resource_runtime_index) const;
    void install_rebound_snapshot(PendingResourceRuntimeSnapshot snapshot) noexcept;
    [[nodiscard]] bool accepts_key(const ResourceKey& key) const noexcept;
    [[nodiscard]] static bool accepts_key(
        const ResourceKey& key,
        bool accepts_all_resource_types,
        const std::unordered_set<ResourceKind>& accepted_kinds) noexcept;
    [[nodiscard]] bool can_apply_network_delta(
        const ResourceStack& changed, int64_t delta) const noexcept;
    void apply_network_delta(const ResourceStack& changed, int64_t delta) noexcept;

    AeStorageCellConfig config_;
    ResourceRuntimeIndex::Snapshot resource_runtime_index_;
    ResourceKeyContext context_;
    std::unordered_set<ResourceKind> accepted_kinds_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts_;
    int64_t used_bytes_ = 0;
    std::optional<PendingResourceRuntimeSnapshot> pending_resource_runtime_snapshot_;
    IResourceAggregateMutationObserver* resource_aggregate_observer_ = nullptr;
    ResourceAggregateStorageHandle resource_aggregate_handle_;
};

}  // namespace snt::game
