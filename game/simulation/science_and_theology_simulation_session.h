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
#include "game/simulation/machine_interaction_service.h"
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

class ScienceAndTheologySimulationSession final : public snt::engine::ISimulationSession {
public:
    explicit ScienceAndTheologySimulationSession(GameSessionConfig config);
    ~ScienceAndTheologySimulationSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void shutdown() noexcept override;

    QuestRegistry& quests() noexcept { return quest_registry_; }
    MachineInteractionService& machine_interactions() noexcept {
        return machine_interactions_;
    }

private:
    GameSessionConfig config_;
    snt::engine::SimulationServices* services_ = nullptr;
    GameContentRegistry content_registry_;
    QuestRegistry quest_registry_;
    MachineInteractionService machine_interactions_;
    GameChunkSidecarRegistry chunk_sidecars_;
    std::unique_ptr<GameWorldPersistenceLifecycle> world_persistence_;
    snt::ecs::World* world_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    bool world_ready_ = false;
    bool scripts_started_ = false;
};

}  // namespace snt::game
