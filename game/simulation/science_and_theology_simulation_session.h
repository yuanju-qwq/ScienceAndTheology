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
#include "game/simulation/region_topology.h"
#include "game/simulation/season_cycle.h"
#include "game/simulation/tree_growth_events.h"
#include "game/quest/quest_registry.h"
#include "game/world/game_chunk.h"

#include <memory>
#include <optional>

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
class GameFluidSystem;
class GameTreeGrowthSystem;
class IFluidComputeBackend;
class IMachineTickEventSink;
class MachineTickSystem;
struct WorldGenConfigSnapshot;

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

    // Client presentation may read immutable game definitions through this
    // narrow accessor. It never receives mutable QuestRegistry progress.
    const GameContentRegistry& content() const noexcept { return content_registry_; }
    // The immutable worldgen snapshot is shared by client presentation and
    // dedicated-server authority. Callers may resolve runtime IDs from it but
    // cannot mutate content after the session has published the snapshot.
    [[nodiscard]] const WorldGenConfigSnapshot* worldgen_config() const noexcept {
        return worldgen_config_.get();
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

private:
    GameSessionConfig config_;
    snt::engine::SimulationServices* services_ = nullptr;
    GameContentRegistry content_registry_;
    GameContentReloadService content_reload_service_;
    std::optional<GameContentReloadTarget> pending_content_reload_;
    std::optional<GameContentReloadResult> last_content_reload_result_;
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
    GameChunkSidecarRegistry chunk_sidecars_;
    std::unique_ptr<GameWorldPersistenceLifecycle> world_persistence_;
    std::shared_ptr<MachineTickSystem> machine_tick_system_;
    IMachineTickEventSink* machine_tick_event_sink_ = nullptr;
    snt::ecs::World* world_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    bool world_ready_ = false;
    bool scripts_started_ = false;
};

}  // namespace snt::game
