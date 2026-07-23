// AE pattern-provider dispatch contracts.
//
// The planner consumes stable ResourceContentStack pattern definitions and
// compiles them into compact ResourceKey routes for one resource snapshot.
// A provider is the only party allowed to accept an operation; it may back
// that operation with a machine, a crafting table, or another future runtime.
// Providers never mutate the AE aggregate directly.  Inputs first leave the
// network owner, and completed outputs return through the same owner only
// after an explicit acknowledgement.

#pragma once

#include "core/expected.h"
#include "game/automation/ae_autocrafting.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace snt::game {

struct AePatternProviderHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const AePatternProviderHandle&,
                           const AePatternProviderHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AePatternProviderHandle>);

// Provider-owned operation identity.  Its value may be a durable sidecar
// serial (for a machine provider) or an in-memory handle (for an instant
// provider), but it is never an AE aggregate slot.
struct AePatternProviderOperationHandle {
    uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept { return value != 0; }

    friend bool operator==(const AePatternProviderOperationHandle&,
                           const AePatternProviderOperationHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AePatternProviderOperationHandle>);

enum class AePatternProviderStartState : uint8_t {
    kReady = 0,
    kBusy = 1,
    kUnavailable = 2,
};

enum class AePatternProviderOperationState : uint8_t {
    kRunning = 0,
    kBlocked = 1,
    kCompleted = 2,
    kFailed = 3,
    kMissing = 4,
};

// Compact request handed to an already selected provider.  Inputs belong to
// the provider after start_operation succeeds.  expected_outputs remains a
// promise until the provider reports completion; it is not an aggregate
// mutation and may not be inserted into the network early.
struct AePatternProviderOperationRequest {
    std::string pattern_id;
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> expected_outputs;
};

class IAePatternProviderEndpoint {
public:
    virtual ~IAePatternProviderEndpoint() = default;

    [[nodiscard]] virtual AePatternProviderStartState can_start_operation(
        const AePatternProviderOperationRequest& request) const noexcept = 0;
    [[nodiscard]] virtual snt::core::Expected<AePatternProviderOperationHandle>
    start_operation(AePatternProviderOperationRequest request) = 0;
    [[nodiscard]] virtual AePatternProviderOperationState operation_state(
        AePatternProviderOperationHandle handle) const noexcept = 0;
    // Returns the physical outputs without removing them.  The dispatcher
    // inserts them into the AE owner first, then calls acknowledge_completion
    // to remove the producer-side copies.
    [[nodiscard]] virtual snt::core::Expected<std::vector<ResourceStack>>
    completed_outputs(AePatternProviderOperationHandle handle,
                      const ResourceKeyContext& context) const = 0;
    [[nodiscard]] virtual snt::core::Expected<void> acknowledge_completion(
        AePatternProviderOperationHandle handle,
        const ResourceKeyContext& context) = 0;
};

// `scope_id` is the currently live AE component id.  The simulation bridge
// updates it at topology boundaries; the automation planner only uses it as
// a compact routing partition and never scans physical AE nodes.
struct AePatternProviderRegistration {
    std::string provider_key;
    uint64_t scope_id = 0;
    int32_t priority = 0;
    std::vector<AeAutocraftingPatternDefinition> patterns;
    IAePatternProviderEndpoint* endpoint = nullptr;
};

struct AePatternProviderPlanStep {
    std::string provider_key;
    std::string pattern_id;
    uint64_t operations = 0;
};

struct AePatternProviderPlan {
    ResourceContentStack requested;
    std::vector<AePatternProviderPlanStep> steps;
};

struct AePatternProviderJobHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const AePatternProviderJobHandle&,
                           const AePatternProviderJobHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AePatternProviderJobHandle>);

enum class AePatternProviderJobState : uint8_t {
    kQueued = 0,
    kWaitingForProvider = 1,
    kBlockedMissingInput = 2,
    kBlockedProvider = 3,
    kBlockedOutputCapacity = 4,
    kCompleted = 5,
    kCancelledByContentReload = 6,
    kFailed = 7,
};

struct AePatternProviderJobSnapshot {
    AePatternProviderJobHandle handle;
    ResourceContentStack requested;
    AePatternProviderJobState state = AePatternProviderJobState::kQueued;
    size_t next_step_index = 0;
    uint64_t remaining_operations_in_step = 0;
    uint64_t completed_operations = 0;
    std::string active_provider_key;
    std::string active_pattern_id;
};

// Main-thread provider scheduler.  Resource reads come from the caller's AE
// component owner and retain its O(1) aggregate behavior.  This service only
// performs graph planning and one selected provider operation at a time; it
// never treats a recipe definition as an instant resource conversion.
class AePatternProviderService final : public IResourceRuntimeSnapshotParticipant {
public:
    explicit AePatternProviderService(ResourceRuntimeIndex::Snapshot resource_runtime_index);
    ~AePatternProviderService() override;

    AePatternProviderService(const AePatternProviderService&) = delete;
    AePatternProviderService& operator=(const AePatternProviderService&) = delete;

    [[nodiscard]] snt::core::Expected<AePatternProviderHandle> register_provider(
        AePatternProviderRegistration registration);
    [[nodiscard]] bool unregister_provider(AePatternProviderHandle handle) noexcept;
    // Replaces one provider's pattern catalog at a content boundary. Existing
    // queued jobs in that scope are cancelled; an in-flight physical
    // operation is left with its producer until the endpoint reports it done.
    [[nodiscard]] snt::core::Expected<void> update_provider_patterns(
        AePatternProviderHandle handle,
        std::vector<AeAutocraftingPatternDefinition> patterns);
    [[nodiscard]] snt::core::Expected<void> update_provider_scope(
        AePatternProviderHandle handle, uint64_t scope_id);

    [[nodiscard]] snt::core::Expected<AePatternProviderPlan> plan(
        IAeAutocraftingResourceAccess& resources,
        uint64_t scope_id,
        ResourceContentStack requested) const;
    [[nodiscard]] snt::core::Expected<AePatternProviderJobHandle> submit_job(
        IAeAutocraftingResourceAccess& resources,
        uint64_t scope_id,
        ResourceContentStack requested);
    // Reattaches one durable provider operation after a session/chunk
    // materialization boundary. The operation already owns its physical
    // inputs, so this creates only the output-delivery half of a job and
    // never extracts from the AE aggregate again.
    [[nodiscard]] snt::core::Expected<AePatternProviderJobHandle> adopt_operation(
        AePatternProviderHandle provider,
        AePatternProviderOperationHandle operation,
        ResourceContentStack requested,
        std::string pattern_id,
        std::vector<ResourceStack> expected_outputs);
    [[nodiscard]] snt::core::Expected<AePatternProviderJobSnapshot> tick(
        AePatternProviderJobHandle handle,
        IAeAutocraftingResourceAccess& resources,
        uint32_t max_operations = 1);
    [[nodiscard]] std::optional<AePatternProviderJobSnapshot> find_job(
        AePatternProviderJobHandle handle) const noexcept;
    // A waiting provider operation owns physical input and cannot be removed
    // without the endpoint's explicit cancellation protocol.  Return false
    // in that state so callers do not orphan an in-flight machine operation.
    [[nodiscard]] bool remove_job(AePatternProviderJobHandle handle) noexcept;
    // Marks all jobs in one physical component for cancellation. A pending
    // producer operation is never acknowledged into a changed component; it
    // remains in the producer until a later explicit recovery path handles it.
    void cancel_jobs_for_scope(uint64_t scope_id) noexcept;

    [[nodiscard]] ResourceRuntimeIndex::Snapshot resource_runtime_index() const noexcept {
        return resource_runtime_index_;
    }

    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

private:
    struct ProviderSlot;
    struct ScopeCatalog;
    struct Job;
    struct JobSlot;
    struct PendingResourceRuntimeSnapshot;

    [[nodiscard]] snt::core::Expected<void> rebuild_scope(uint64_t scope_id);
    [[nodiscard]] snt::core::Expected<void> rebuild_all_scopes();
    [[nodiscard]] snt::core::Expected<void> validate_registration(
        const AePatternProviderRegistration& registration) const;
    [[nodiscard]] ProviderSlot* find_provider(AePatternProviderHandle handle) noexcept;
    [[nodiscard]] const ProviderSlot* find_provider(
        AePatternProviderHandle handle) const noexcept;
    [[nodiscard]] JobSlot* find_job_slot(AePatternProviderJobHandle handle) noexcept;
    [[nodiscard]] const JobSlot* find_job_slot(AePatternProviderJobHandle handle) const noexcept;
    [[nodiscard]] AePatternProviderJobSnapshot snapshot_of(
        AePatternProviderJobHandle handle, const Job& job) const;
    void cancel_jobs_for_scope_rebuild(uint64_t scope_id) noexcept;
    void cancel_jobs_for_resource_reload() noexcept;

    ResourceRuntimeIndex::Snapshot resource_runtime_index_;
    std::vector<std::unique_ptr<ProviderSlot>> providers_;
    std::vector<uint32_t> reusable_provider_slots_;
    std::unordered_map<uint64_t, std::unique_ptr<ScopeCatalog>> scope_catalogs_;
    std::vector<std::unique_ptr<JobSlot>> jobs_;
    std::vector<uint32_t> reusable_job_slots_;
    std::unique_ptr<PendingResourceRuntimeSnapshot> pending_resource_runtime_snapshot_;
};

}  // namespace snt::game
