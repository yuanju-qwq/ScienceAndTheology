// Active AE interface-to-machine pattern-provider owner.
//
// Durable bindings belong to chunk sidecars.  This main-thread service turns
// currently materialized bindings into IAePatternProviderEndpoint instances,
// routes jobs through the live AE component owner, and leaves actual recipe
// execution to MachineTickSystem.  Normal AE amount reads remain O(1): every
// dispatch operation uses AeNetworkComponentStorage rather than scanning
// machines, drives, or sidecars.

#pragma once

#include "core/expected.h"
#include "game/automation/ae_pattern_provider.h"
#include "game/resources/resource_runtime_index.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace snt::ecs {
class World;
}

namespace snt::game {

class AeNetworkRuntimeService;
class GameContentRegistry;
struct MachineRuntimeComponent;

class AeMachinePatternProviderRuntimeService final
    : public IResourceRuntimeSnapshotParticipant {
public:
    AeMachinePatternProviderRuntimeService(GameContentRegistry& content,
                                           snt::ecs::World& world,
                                           GameChunkSidecarRegistry& sidecars,
                                           AeNetworkRuntimeService& network_runtime);
    ~AeMachinePatternProviderRuntimeService() override;

    AeMachinePatternProviderRuntimeService(const AeMachinePatternProviderRuntimeService&) = delete;
    AeMachinePatternProviderRuntimeService& operator=(
        const AeMachinePatternProviderRuntimeService&) = delete;

    // The physical AE topology and target machine runtime must already be
    // materialized when an operation begins. A target machine in another
    // unloaded chunk remains unavailable instead of being reconstructed by
    // the AE hot path.
    [[nodiscard]] snt::core::Expected<void> materialize_chunk(
        const ChunkKey& chunk_key, const GameChunkSidecar& sidecar);
    [[nodiscard]] snt::core::Expected<void> dematerialize_chunk(
        const ChunkKey& chunk_key);

    // Call after any AE topology boundary. It maps interface anchors to live
    // component ids through O(1) runtime lookup and rebuilds only changed
    // provider scopes.
    [[nodiscard]] snt::core::Expected<void> refresh_topology();
    // Call after a committed recipe/machine content change. Queued plans are
    // cancelled and future work uses newly derived RecipeDefinition patterns.
    [[nodiscard]] snt::core::Expected<void> refresh_content_definitions();

    [[nodiscard]] snt::core::Expected<AePatternProviderPlan> plan(
        EntityId interface_anchor_entity_id,
        ResourceContentStack requested) const;
    [[nodiscard]] snt::core::Expected<AePatternProviderJobHandle> submit_job(
        EntityId interface_anchor_entity_id,
        ResourceContentStack requested);
    [[nodiscard]] snt::core::Expected<AePatternProviderJobSnapshot> tick_job(
        AePatternProviderJobHandle handle, uint32_t max_operations = 1);
    [[nodiscard]] std::optional<AePatternProviderJobSnapshot> find_job(
        AePatternProviderJobHandle handle) const noexcept;
    [[nodiscard]] bool remove_job(AePatternProviderJobHandle handle) noexcept;

    // Session callers invoke this after worker commands have reached the ECS
    // barrier. It polls completed machine work and performs at most the given
    // number of provider operations per active AE job.
    [[nodiscard]] snt::core::Expected<void> tick(
        uint64_t tick_index, uint32_t max_operations_per_job = 1);

    [[nodiscard]] size_t active_provider_count() const noexcept { return providers_.size(); }
    [[nodiscard]] size_t active_job_count() const noexcept { return active_jobs_.size(); }
    [[nodiscard]] const AePatternProviderService& dispatcher() const noexcept {
        return dispatcher_;
    }

    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

private:
    class MachineEndpoint;

    struct WorkOrderIdentityHash {
        [[nodiscard]] size_t operator()(
            const MachineAutomationWorkOrderIdentity& identity) const noexcept {
            size_t hash = std::hash<uint64_t>{}(identity.provider_anchor_entity_id.id);
            hash ^= std::hash<uint64_t>{}(identity.provider_job_serial) + 0x9e3779b9u +
                (hash << 6u) + (hash >> 2u);
            return hash;
        }
    };

    struct MachineLocation;
    struct LiveProvider;
    struct ActiveJob;
    struct ResolvedOperation;

    [[nodiscard]] snt::core::Expected<void> rebuild_machine_index();
    [[nodiscard]] const MachineLocation* find_machine_location(
        EntityId machine_anchor_entity_id) const noexcept;
    [[nodiscard]] MachineRuntimeComponent* find_live_machine(
        EntityId machine_anchor_entity_id) noexcept;
    [[nodiscard]] const MachineRuntimeComponent* find_live_machine(
        EntityId machine_anchor_entity_id) const noexcept;
    [[nodiscard]] LiveProvider* find_provider(
        EntityId interface_anchor_entity_id) noexcept;
    [[nodiscard]] const LiveProvider* find_provider(
        EntityId interface_anchor_entity_id) const noexcept;
    [[nodiscard]] AeMachinePatternProviderPersistenceRecord* find_binding(
        EntityId interface_anchor_entity_id) noexcept;
    [[nodiscard]] const AeMachinePatternProviderPersistenceRecord* find_binding(
        EntityId interface_anchor_entity_id) const noexcept;
    [[nodiscard]] snt::core::Expected<std::vector<AeAutocraftingPatternDefinition>>
    make_patterns(EntityId interface_anchor_entity_id) const;
    [[nodiscard]] snt::core::Expected<ResolvedOperation> resolve_operation(
        EntityId interface_anchor_entity_id,
        const AePatternProviderOperationRequest& request) const;
    [[nodiscard]] snt::core::Expected<void> refresh_provider_registration(
        LiveProvider& provider);
    [[nodiscard]] snt::core::Expected<void> recover_persisted_work_orders();
    void note_work_order_started(MachineAutomationWorkOrderIdentity identity) noexcept;
    void forget_work_order(MachineAutomationWorkOrderIdentity identity) noexcept;
    [[nodiscard]] snt::core::Expected<AeNetworkComponentStorage> component_storage_for(
        EntityId interface_anchor_entity_id,
        uint64_t expected_scope_id) const;
    [[nodiscard]] static uint64_t job_token(AePatternProviderJobHandle handle) noexcept;

    GameContentRegistry* content_ = nullptr;
    snt::ecs::World* world_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    AeNetworkRuntimeService* network_runtime_ = nullptr;
    AePatternProviderService dispatcher_;
    std::unordered_map<uint64_t, MachineLocation> machines_;
    std::unordered_map<uint64_t, std::unique_ptr<LiveProvider>> providers_;
    std::unordered_map<uint64_t, ActiveJob> active_jobs_;
    // In-process operations are already represented by dispatcher jobs. This
    // set prevents a later chunk materialization from adopting them again;
    // after a full session restart the set starts empty and durable orders
    // are reattached through recover_persisted_work_orders().
    std::unordered_set<MachineAutomationWorkOrderIdentity, WorkOrderIdentityHash>
        known_work_orders_;
};

}  // namespace snt::game
