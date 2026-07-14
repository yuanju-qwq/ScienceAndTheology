// Game-owned deterministic machine runtime.
//
// Ownership: ScienceAndTheologySimulationSession creates this system and
// registers it with SimulationWorldSession. The machine component, item
// stacks, recipe snapshots and events are ScienceAndTheology gameplay state,
// not engine ECS primitives.
//
// Lifecycle: the supplied GameContentRegistry must outlive this registered
// system.
// Thread affinity: capture() runs on the main thread; its returned task only
// computes from copied values on workers, then applies commands and notifies
// the event sink at the main-thread scheduler barrier.
//
// Dependencies: ECS supplies the generic worker contract, while
// GameContentRegistry supplies value-only recipe definitions. No AngelScript
// VM pointers cross the worker boundary. Future game UI, save and replication code subscribes via
// IMachineTickEventSink instead of reaching into machine state directly.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ecs/entity_guid.h"
#include "ecs/system.h"

namespace snt::ecs {
class World;
}

namespace snt::game {

class GameContentRegistry;

struct MachineItemStack {
    std::string item_id;
    int32_t count = 0;

    bool empty() const { return item_id.empty() || count <= 0; }
};

// Game-owned recipe copy. It deliberately mirrors only the data needed by
// the tick loop and never contains GameContentRegistry or AngelScript references.
struct MachineRecipeSnapshot {
    std::string id;
    std::string input_item_id;
    std::vector<MachineItemStack> outputs;
    int32_t duration_ticks = 0;
    int32_t energy_per_tick = 0;
};

enum class MachineRunState : uint8_t {
    Idle,
    Running,
    NoMatchingRecipe,
    WaitingForEnergy,
    WaitingForOutput,
};

// ECS component for one game machine controller. Input is reserved when a
// recipe starts, so a hot reload or an inventory mutation cannot change a job
// that is already in progress. Game save code will serialize this component.
struct MachineRuntimeComponent {
    std::string machine_id;
    MachineItemStack input;
    std::vector<MachineItemStack> output_slots;

    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t max_output_slots = 4;
    int32_t max_stack_size = 64;

    int32_t progress_ticks = 0;
    std::optional<MachineRecipeSnapshot> active_recipe;
    MachineRunState state = MachineRunState::Idle;
};

enum class MachineTickEventKind : uint8_t {
    StateChanged,
    RecipeCompleted,
};

struct MachineTickEvent {
    MachineTickEventKind kind = MachineTickEventKind::StateChanged;
    snt::ecs::EntityGuid entity_guid;
    std::string machine_id;
    std::string recipe_id;
    MachineRunState previous_state = MachineRunState::Idle;
    MachineRunState state = MachineRunState::Idle;
};

class IMachineTickEventSink {
public:
    virtual ~IMachineTickEventSink() = default;
    virtual void on_machine_tick_event(const MachineTickEvent& event) = 0;
};

class MachineTickSystem final : public snt::ecs::IWorkerSystem {
public:
    explicit MachineTickSystem(GameContentRegistry& content_registry,
                               IMachineTickEventSink* event_sink = nullptr);

    snt::ecs::SystemMetadata metadata() const override {
        return {
            "gameplay.machine_tick",
            snt::ecs::SystemThreadAffinity::Worker,
            {
                {"ecs.machine_runtime", snt::ecs::SystemResourceAccessMode::Write},
                {"game.content_registry", snt::ecs::SystemResourceAccessMode::Read},
                {"gameplay.machine_events", snt::ecs::SystemResourceAccessMode::Write},
            },
        };
    }

    std::unique_ptr<snt::ecs::IWorkerTask> capture(
        const snt::ecs::World& world, float dt) override;

private:
    GameContentRegistry& content_registry_;
    IMachineTickEventSink* event_sink_ = nullptr;
};

}  // namespace snt::game
