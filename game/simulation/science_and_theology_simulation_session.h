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
#include "game/simulation/day_night_cycle.h"
#include "game/simulation/machine_interaction_service.h"
#include "game/simulation/season_cycle.h"
#include "game/quest/quest_registry.h"
#include "game/world/game_chunk.h"

#include <memory>

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
class IMachineTickEventSink;
class MachineTickSystem;
struct WorldGenConfigSnapshot;

class ScienceAndTheologySimulationSession final : public snt::engine::ISimulationSession,
                                                  public IBlockPhysicsTrigger {
public:
    explicit ScienceAndTheologySimulationSession(GameSessionConfig config);
    ~ScienceAndTheologySimulationSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void shutdown() noexcept override;

    // Client presentation may read immutable game definitions through this
    // narrow accessor. It never receives mutable QuestRegistry progress.
    const GameContentRegistry& content() const noexcept { return content_registry_; }
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
    void schedule_block_physics_after_terrain_mutation(
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
    QuestRegistry quest_registry_;
    MachineInteractionService machine_interactions_;
    DayNightCycle day_night_cycle_;
    SeasonCycle season_cycle_;
    std::shared_ptr<const WorldGenConfigSnapshot> worldgen_config_;
    std::unique_ptr<GameBlockPhysicsSystem> block_physics_system_;
    IBlockPhysicsMutationSink* block_physics_mutation_sink_ = nullptr;
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
