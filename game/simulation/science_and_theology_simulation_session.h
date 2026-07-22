// ScienceAndTheology deterministic simulation-session implementation.
//
// Ownership: a game host creates one instance for either ClientRuntime or
// SimulationRuntime. It owns game definitions, transient script state, and
// game-world sidecars; client presentation state deliberately lives elsewhere.
//
// Thread affinity: all lifecycle callbacks run on the simulation main thread.
// Registered machine systems publish worker-safe value snapshots through the
// engine scheduler and never retain this session's World or script VM.

#pragma once

#include "engine/simulation_session.h"
#include "game/client/game_content_registry.h"
#include "game/client/game_session_config.h"
#include "game/simulation/block_physics_events.h"
#include "game/simulation/crop_growth_events.h"
#include "game/simulation/day_night_cycle.h"
#include "game/simulation/game_content_reload_service.h"
#include "game/simulation/game_fluid_system_events.h"
#include "game/simulation/machine_interaction_service.h"
#include "game/simulation/offline_machine_simulation.h"
#include "game/simulation/region_topology.h"
#include "game/simulation/season_cycle.h"
#include "game/simulation/tree_growth_events.h"
#include "game/quest/quest_registry.h"
#include "game/world/game_chunk.h"

#include <memory>
#include <optional>
#include <span>

namespace snt::engine {
class SimulationServices;
class SimulationWorldSession;
class FixedTickContext;
}

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::ecs {
class World;
}

namespace snt::game {

class GameWorldPersistenceLifecycle;
class GameBlockPhysicsSystem;
class GameCropGrowthSystem;
class GameEcosystemSystem;
class GameWildCreatureSystem;
class GameFluidSystem;
class AutomationControllerRuntimeService;
class GameTreeGrowthSystem;
class IGameEcosystemEnvironmentProvider;
class IGameEcosystemInterestProvider;
class IGameEcosystemMutationSink;
class IGameCreaturePresentationSink;
class IFluidComputeBackend;
class IMachineTickEventSink;
class MachineTickSystem;
class OfflineMachineSimulationService;
class OfflineIndustrialNetworkIslandProvider;
class OfflineIndustrialNetworkIslandSimulator;
struct WorldGenConfigSnapshot;

// Result of one authoritative terrain-ticket reconciliation. The session owns
// terrain, sidecar persistence, and machine ownership; hosts own only the
// policy that produces requested ChunkKey values.
struct GameChunkTicketReconciliation {
    size_t requested_chunk_count = 0;
    size_t expanded_ticket_chunk_count = 0;
    size_t terrain_materialized_count = 0;
    size_t terrain_dematerialized_count = 0;
    OfflineChunkMachineTransition machine_transition;
};

class ScienceAndTheologySimulationSession final : public snt::engine::ISimulationSession,
                                                  public IBlockPhysicsTrigger,
                                                  public IFluidTrigger {
public:
    explicit ScienceAndTheologySimulationSession(GameSessionConfig config);
    ~ScienceAndTheologySimulationSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void shutdown() noexcept override;

    // UI and keyboard paths enqueue an editor-only reload request. It is
    // consumed on the simulation main thread at the next fixed-tick boundary.
    void request_content_reload(GameContentReloadTarget target) noexcept;
    [[nodiscard]] std::vector<GameContentReloadTargetInfo> content_reload_targets() const;
    [[nodiscard]] const GameContentReloadResult* last_content_reload_result() const noexcept {
        return last_content_reload_result_ ? &*last_content_reload_result_ : nullptr;
    }
    [[nodiscard]] const GameContentReloadFailure* last_content_reload_failure() const noexcept {
        return last_content_reload_failure_ ? &*last_content_reload_failure_ : nullptr;
    }

    // Client presentation may read immutable game definitions through this
    // narrow accessor. It never receives mutable QuestRegistry progress.
    const GameContentRegistry& content() const noexcept { return content_registry_; }
    // Runtime owners attach only during their active session lifetime. The
    // registry calls them transactionally before publishing a content reload
    // snapshot, keeping all live ResourceKey values in one generation.
    [[nodiscard]] snt::core::Expected<void> add_resource_runtime_snapshot_participant(
        IResourceRuntimeSnapshotParticipant& participant);
    void remove_resource_runtime_snapshot_participant(
        IResourceRuntimeSnapshotParticipant& participant) noexcept;
    // The immutable worldgen snapshot is shared by client presentation and
    // dedicated-server authority. Callers may resolve runtime IDs from it but
    // cannot mutate content after the session has published the snapshot.
    [[nodiscard]] const WorldGenConfigSnapshot* worldgen_config() const noexcept {
        return worldgen_config_.get();
    }
    // The server interaction composition may invoke immediate player farming
    // mutations through this authoritative, session-owned system. It remains
    // null before world creation and after shutdown.
    [[nodiscard]] GameCropGrowthSystem* crop_growth_system() noexcept {
        return crop_growth_system_.get();
    }
    [[nodiscard]] const GameCropGrowthSystem* crop_growth_system() const noexcept {
        return crop_growth_system_.get();
    }
    // Ecosystem state is session-owned just like crops. Server combat and
    // future presentation adapters access it through this typed boundary.
    [[nodiscard]] GameEcosystemSystem* ecosystem_system() noexcept {
        return ecosystem_system_.get();
    }
    [[nodiscard]] const GameEcosystemSystem* ecosystem_system() const noexcept {
        return ecosystem_system_.get();
    }
    [[nodiscard]] GameWildCreatureSystem* wild_creature_system() noexcept {
        return wild_creature_system_.get();
    }
    [[nodiscard]] const GameWildCreatureSystem* wild_creature_system() const noexcept {
        return wild_creature_system_.get();
    }
    QuestRegistry& quests() noexcept { return quest_registry_; }
    MachineInteractionService& machine_interactions() noexcept {
        return machine_interactions_;
    }
    // Read-only snapshot for client presentation and future authoritative
    // replication. Game logic must use this rather than a wall-clock value.
    [[nodiscard]] const DayNightState& day_night_state() const noexcept {
        return day_night_cycle_.state();
    }
    // Seasonal values use the same authoritative tick as day/night and are
    // safe for read-only gameplay, replication, and presentation consumers.
    [[nodiscard]] const SeasonState& season_state() const noexcept {
        return season_cycle_.state();
    }
    // Typed topology is game-owned and advances from the same authoritative
    // clock as every other simulation system. Hosts may attach observers but
    // do not receive a mutable Godot or engine EventBus dependency.
    RegionTopology& region_topology() noexcept { return region_topology_; }
    const RegionTopology& region_topology() const noexcept { return region_topology_; }
    void set_region_topology_event_sink(IRegionTopologyEventSink* event_sink) noexcept;
    // Server composition installs host-only consumers before world creation.
    // Runtime calls are main-thread lifecycle operations; the session updates
    // an already registered machine system only when no worker task is live.
    void set_machine_tick_event_sink(IMachineTickEventSink* event_sink) noexcept;
    void set_quest_reward_sink(IQuestRewardSink* reward_sink) noexcept {
        quest_registry_.set_reward_sink(reward_sink);
    }
    // Host composition binds its terrain-delta consumer before world creation.
    // The simulation retains no server or transport dependency.
    void set_block_physics_mutation_sink(IBlockPhysicsMutationSink* mutation_sink) noexcept;
    void set_fluid_mutation_sink(IFluidMutationSink* mutation_sink) noexcept;
    void set_fluid_presentation_sink(IFluidPresentationSink* presentation_sink) noexcept;
    void set_fluid_telemetry_sink(IFluidSimulationTelemetrySink* telemetry_sink) noexcept;
    // Client composition may attach a deterministic accelerator here. GPU
    // presentation adapters remain separate from this authoritative contract.
    void set_fluid_compute_backend(IFluidComputeBackend* backend) noexcept;
    // Host composition binds the consumer for authoritative tree growth after
    // world creation. Tree simulation remains transport-neutral.
    void set_tree_growth_mutation_sink(ITreeGrowthMutationSink* mutation_sink) noexcept;
    // Crop and farmland changes use the same narrow host composition boundary
    // as tree growth, while keeping crop simulation transport-neutral.
    void set_crop_growth_mutation_sink(ICropGrowthMutationSink* mutation_sink) noexcept;
    // Ecosystem composition accepts value-only observers. An external
    // environment provider supersedes the session's GameplayConfig/day-night
    // adapter and therefore owns every field in its returned sample.
    void set_ecosystem_environment_provider(
        const IGameEcosystemEnvironmentProvider* environment_provider) noexcept;
    // The authoritative host supplies player-centered ecology circles here.
    // Without a provider, wild population and proxies deliberately remain
    // inactive rather than silently simulating every loaded chunk.
    void set_ecosystem_interest_provider(
        const IGameEcosystemInterestProvider* interest_provider) noexcept;
    void set_ecosystem_mutation_sink(IGameEcosystemMutationSink* mutation_sink) noexcept;
    // Presentation receives creature values/events, never a raw ecosystem
    // proxy plan. The native wildlife system remains the sole owner of
    // interactive wild representatives and captive projection.
    void set_creature_presentation_sink(IGameCreaturePresentationSink* sink) noexcept;
    void schedule_block_physics_after_terrain_mutation(
        std::string_view dimension_id, int32_t block_x, int32_t block_y,
        int32_t block_z, uint64_t source_tick) override;
    void schedule_fluid_after_terrain_mutation(
        std::string_view dimension_id, int32_t block_x, int32_t block_y,
        int32_t block_z, uint64_t source_tick) override;
    // Server-owned world services may bind durable block-sidecar state here;
    // caller remains on the simulation main thread and must not retain it
    // past this session's shutdown.
    GameChunkSidecarRegistry& world_sidecars() noexcept { return chunk_sidecars_; }
    const GameChunkSidecarRegistry& world_sidecars() const noexcept { return chunk_sidecars_; }
    // Active controller executors are a separate block-owner subsystem. Server
    // interaction and replication composition may use this narrow accessor;
    // they never receive a machine ECS component or mutable sidecar container.
    [[nodiscard]] AutomationControllerRuntimeService* automation_controller_runtime() noexcept {
        return automation_controller_runtime_.get();
    }
    [[nodiscard]] const AutomationControllerRuntimeService*
    automation_controller_runtime() const noexcept {
        return automation_controller_runtime_.get();
    }

    // Chunk streaming calls these only at an authoritative fixed-tick barrier.
    // They transfer machine ownership before terrain is removed or restored.
    [[nodiscard]] snt::core::Expected<OfflineChunkMachineTransition>
    dematerialize_chunk_machines(const ChunkKey& chunk_key, uint64_t current_tick);
    [[nodiscard]] snt::core::Expected<OfflineChunkMachineTransition>
    dematerialize_chunks_machines(std::span<const ChunkKey> chunk_keys,
                                  uint64_t current_tick);
    [[nodiscard]] snt::core::Expected<void>
    materialize_chunk_machines(const ChunkKey& chunk_key, uint64_t current_tick);

    // Reconciles terrain and machine ownership at a fixed-tick barrier. The
    // supplied tickets are expanded to complete offline industrial islands
    // before terrain loading or ECS restoration. Terrain unloads first save
    // the chunk, then retain its semantic sidecar for offline simulation.
    [[nodiscard]] snt::core::Expected<GameChunkTicketReconciliation>
    reconcile_chunk_tickets(uint64_t current_tick,
                            std::span<const ChunkKey> requested_chunk_keys);

private:
    class SessionEcosystemEnvironmentProvider;

    GameSessionConfig config_;
    snt::engine::SimulationServices* services_ = nullptr;
    GameContentRegistry content_registry_;
    GameContentReloadService content_reload_service_;
    std::optional<GameContentReloadTarget> pending_content_reload_;
    std::optional<GameContentReloadResult> last_content_reload_result_;
    std::optional<GameContentReloadFailure> last_content_reload_failure_;
    QuestRegistry quest_registry_;
    MachineInteractionService machine_interactions_;
    DayNightCycle day_night_cycle_;
    SeasonCycle season_cycle_;
    RegionTopology region_topology_;
    std::shared_ptr<const WorldGenConfigSnapshot> worldgen_config_;
    std::unique_ptr<GameBlockPhysicsSystem> block_physics_system_;
    IBlockPhysicsMutationSink* block_physics_mutation_sink_ = nullptr;
    std::unique_ptr<GameFluidSystem> fluid_system_;
    IFluidMutationSink* fluid_mutation_sink_ = nullptr;
    IFluidPresentationSink* fluid_presentation_sink_ = nullptr;
    IFluidSimulationTelemetrySink* fluid_telemetry_sink_ = nullptr;
    IFluidComputeBackend* fluid_compute_backend_ = nullptr;
    std::unique_ptr<GameTreeGrowthSystem> tree_growth_system_;
    ITreeGrowthMutationSink* tree_growth_mutation_sink_ = nullptr;
    std::unique_ptr<GameCropGrowthSystem> crop_growth_system_;
    ICropGrowthMutationSink* crop_growth_mutation_sink_ = nullptr;
    std::unique_ptr<GameEcosystemSystem> ecosystem_system_;
    std::unique_ptr<SessionEcosystemEnvironmentProvider>
        session_ecosystem_environment_provider_;
    const IGameEcosystemEnvironmentProvider* ecosystem_environment_provider_ = nullptr;
    const IGameEcosystemInterestProvider* ecosystem_interest_provider_ = nullptr;
    IGameEcosystemMutationSink* ecosystem_mutation_sink_ = nullptr;
    std::unique_ptr<GameWildCreatureSystem> wild_creature_system_;
    IGameCreaturePresentationSink* creature_presentation_sink_ = nullptr;
    GameChunkSidecarRegistry chunk_sidecars_;
    std::unique_ptr<GameWorldPersistenceLifecycle> world_persistence_;
    std::shared_ptr<MachineTickSystem> machine_tick_system_;
    std::unique_ptr<OfflineIndustrialNetworkIslandProvider>
        offline_industrial_network_provider_;
    std::unique_ptr<OfflineIndustrialNetworkIslandSimulator>
        offline_industrial_network_simulator_;
    std::unique_ptr<OfflineMachineSimulationService> offline_machine_simulation_;
    std::unique_ptr<AutomationControllerRuntimeService> automation_controller_runtime_;
    IMachineTickEventSink* machine_tick_event_sink_ = nullptr;
    snt::ecs::World* world_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    bool world_ready_ = false;
    bool scripts_started_ = false;
    bool gameplay_content_loaded_ = false;
};

}  // namespace snt::game
