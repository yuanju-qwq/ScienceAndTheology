// AE autocrafting planning and execution.
//
// Patterns retain stable ResourceContentStack values so authored content and
// save-facing job requests never depend on a runtime numeric id.  Planning
// compiles them once into compact ResourceKey values for one immutable
// ResourceRuntimeIndex snapshot.  Normal storage amount reads remain O(1)
// through the supplied resource owner; recursive recipe planning is
// intentionally graph work rather than an AE aggregate hot-path query.

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"
#include "game/resources/resource_runtime_index.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace snt::game {

class AeAutocraftingPlanner;

// Content-facing AE pattern.  Inputs and outputs are intentionally generic
// resource stacks: a pattern can later consume fluid, gas, or power without
// adding a parallel item-only crafting system.
struct AeAutocraftingPatternDefinition {
    std::string id;
    std::vector<ResourceContentStack> inputs;
    std::vector<ResourceContentStack> outputs;
    uint32_t ticks_per_operation = 1;
};

struct AeAutocraftingPlanStep {
    std::string pattern_id;
    uint64_t operations = 0;
};

struct AeAutocraftingPlan {
    ResourceContentStack requested;
    std::vector<AeAutocraftingPlanStep> steps;
};

// A job handle is runtime-only.  Save systems retain the stable request and
// plan data that they own; no slot/generation value crosses a persistence or
// network boundary.
struct AeAutocraftingJobHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const AeAutocraftingJobHandle&,
                           const AeAutocraftingJobHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AeAutocraftingJobHandle>);

enum class AeAutocraftingJobState : uint8_t {
    kRunning = 0,
    kBlockedMissingInput = 1,
    kBlockedOutputCapacity = 2,
    kCompleted = 3,
    kCancelledByContentReload = 4,
    kFailed = 5,
};

struct AeAutocraftingJobSnapshot {
    AeAutocraftingJobHandle handle;
    ResourceContentStack requested;
    AeAutocraftingJobState state = AeAutocraftingJobState::kRunning;
    size_t next_step_index = 0;
    uint64_t remaining_operations_in_step = 0;
    uint64_t completed_operations = 0;
};

// The real resource owner performs every mutation.  This is deliberately not
// an aggregate index API: AE totals answer availability reads, while the
// owner implements extraction/insertion policy and transaction boundaries.
// A component router may implement this interface without exposing its live
// endpoint pointers to the autocrafting planner.
class IAeAutocraftingResourceAccess {
public:
    virtual ~IAeAutocraftingResourceAccess() = default;

    [[nodiscard]] virtual ResourceKeyContext key_context() const noexcept = 0;
    [[nodiscard]] virtual int64_t amount_of(const ResourceKeyContext& context,
                                            const ResourceKey& key) const = 0;
    [[nodiscard]] virtual int64_t insert(const ResourceKeyContext& context,
                                         const ResourceStack& stack,
                                         ResourceTransferMode mode) = 0;
    [[nodiscard]] virtual int64_t extract(const ResourceKeyContext& context,
                                          const ResourceStack& requested,
                                          ResourceTransferMode mode) = 0;
};

// Minimal bridge for a concrete storage owner.  It is useful for a drive,
// temporary crafting buffer, or deterministic test ledger, while a future
// component router can implement IAeAutocraftingResourceAccess directly.
class AeAutocraftingStorageAccess final : public IAeAutocraftingResourceAccess {
public:
    explicit AeAutocraftingStorageAccess(IResourceStorage& storage) noexcept
        : storage_(&storage) {}

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override;
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const override;
    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override;
    [[nodiscard]] int64_t extract(const ResourceKeyContext& context,
                                  const ResourceStack& requested,
                                  ResourceTransferMode mode) override;

private:
    IResourceStorage* storage_ = nullptr;
};

// Main-thread AE autocrafting owner.  It makes no assumption about where the
// actual resources live and never scans all attached storage for a normal
// amount query.  Each job executes at most the caller's requested operation
// budget, so a fixed tick can retain a bounded mutation budget.
class AeAutocraftingService final : public IResourceRuntimeSnapshotParticipant {
public:
    explicit AeAutocraftingService(ResourceRuntimeIndex::Snapshot resource_runtime_index);
    ~AeAutocraftingService() override;

    AeAutocraftingService(const AeAutocraftingService&) = delete;
    AeAutocraftingService& operator=(const AeAutocraftingService&) = delete;

    // Replaces the complete authored pattern catalog atomically.  Existing
    // active jobs are cancelled because their graph was planned against the
    // previous catalog, not silently reinterpreted against new recipes.
    [[nodiscard]] snt::core::Expected<void> replace_patterns(
        std::vector<AeAutocraftingPatternDefinition> definitions);
    [[nodiscard]] std::vector<AeAutocraftingPatternDefinition> pattern_definitions() const;

    // Resolves a stable request into the current compact snapshot and plans a
    // deterministic post-order recipe graph.  The selected producer is the
    // lexically first matching pattern id; recipe graph traversal is O(V+E),
    // whereas each individual source amount query remains O(1) by contract.
    [[nodiscard]] snt::core::Expected<AeAutocraftingPlan> plan(
        IAeAutocraftingResourceAccess& resources,
        ResourceContentStack requested) const;

    [[nodiscard]] snt::core::Expected<AeAutocraftingJobHandle> submit_job(
        IAeAutocraftingResourceAccess& resources,
        ResourceContentStack requested);
    // Performs up to max_operations individual pattern operations.  A zero
    // budget is invalid.  A temporary missing input/output condition leaves
    // the job blocked and resource ownership unchanged.
    [[nodiscard]] snt::core::Expected<AeAutocraftingJobSnapshot> tick(
        AeAutocraftingJobHandle handle,
        IAeAutocraftingResourceAccess& resources,
        uint32_t max_operations = 1);
    [[nodiscard]] std::optional<AeAutocraftingJobSnapshot> find_job(
        AeAutocraftingJobHandle handle) const noexcept;
    [[nodiscard]] bool remove_job(AeAutocraftingJobHandle handle) noexcept;

    [[nodiscard]] ResourceRuntimeIndex::Snapshot resource_runtime_index() const noexcept {
        return resource_runtime_index_;
    }

    // Resource reload is a strict snapshot boundary.  Compiled ResourceKey
    // values are rebuilt before publication; already submitted jobs are
    // cancelled at commit rather than executing mixed-generation keys.
    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

private:
    friend class AeAutocraftingPlanner;

    struct CompiledPattern;
    struct CompiledCatalog;
    struct Job;
    struct JobSlot;
    struct PendingResourceRuntimeSnapshot;

    enum class OperationResult : uint8_t {
        kCompleted,
        kBlockedMissingInput,
        kBlockedOutputCapacity,
    };

    [[nodiscard]] static snt::core::Expected<CompiledCatalog> compile_catalog(
        const std::map<std::string, AeAutocraftingPatternDefinition, std::less<>>& definitions,
        const ResourceRuntimeIndex::Snapshot& resource_runtime_index);
    [[nodiscard]] snt::core::Expected<OperationResult> execute_operation(
        const CompiledPattern& pattern,
        IAeAutocraftingResourceAccess& resources);
    [[nodiscard]] JobSlot* find_slot(AeAutocraftingJobHandle handle) noexcept;
    [[nodiscard]] const JobSlot* find_slot(AeAutocraftingJobHandle handle) const noexcept;
    [[nodiscard]] AeAutocraftingJobSnapshot snapshot_of(
        AeAutocraftingJobHandle handle, const Job& job) const;
    void cancel_active_jobs_for_catalog_boundary() noexcept;

    ResourceRuntimeIndex::Snapshot resource_runtime_index_;
    std::map<std::string, AeAutocraftingPatternDefinition, std::less<>> definitions_;
    std::unique_ptr<CompiledCatalog> compiled_catalog_;
    std::unique_ptr<PendingResourceRuntimeSnapshot> pending_resource_runtime_snapshot_;
    std::vector<std::unique_ptr<JobSlot>> jobs_;
    std::vector<uint32_t> reusable_job_slots_;
};

}  // namespace snt::game
