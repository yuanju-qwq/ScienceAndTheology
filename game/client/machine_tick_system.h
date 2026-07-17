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
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/runtime_key_index.h"
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
    // Filled only on the main-thread capture boundary. Persistent storage,
    // command payloads, and events retain item_id; worker calculations use
    // item_runtime_id from one immutable RuntimeKeyIndex snapshot.
    snt::core::RuntimeKeyId item_runtime_id = snt::core::kInvalidRuntimeKeyId;

    bool empty() const { return item_id.empty() || count <= 0; }
};

// Game-owned recipe copy. It deliberately mirrors only the data needed by
// the tick loop and never contains GameContentRegistry or AngelScript references.
struct MachineRecipeSnapshot {
    std::string id;
    std::vector<MachineItemStack> inputs;
    std::vector<MachineItemStack> outputs;
    int32_t duration_ticks = 0;
    int32_t energy_per_tick = 0;
    // An active job keeps this immutable mapping across a content reload so
    // it never combines IDs from different generations. Persistence excludes
    // the snapshot and restores it from the current catalog on first capture.
    snt::core::RuntimeKeyIndex::Snapshot item_runtime_index;
    uint64_t item_runtime_generation = 0;
};

enum class MachineRunState : uint8_t {
    Idle,
    Running,
    NoMatchingRecipe,
    WaitingForActivation,
    WaitingForEnergy,
    WaitingForOutput,
};

// ECS component for one game machine controller. Input is reserved when a
// recipe starts, so a hot reload or an inventory mutation cannot change a job
// that is already in progress. Game save code captures it only through a
// same-chunk BlockEntityPlacement anchor; unanchored instances are rejected
// at the controlled save boundary instead of being silently omitted.
struct MachineRuntimeComponent {
    std::string machine_id;
    std::vector<MachineItemStack> input_slots;
    std::vector<MachineItemStack> output_slots;

    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t max_input_slots = 4;
    int32_t max_output_slots = 4;
    int32_t max_stack_size = 64;

    int32_t progress_ticks = 0;
    std::optional<MachineRecipeSnapshot> active_recipe;
    // MachineInteractionService sets this only on a manual machine after the
    // main-thread player-command boundary validates interaction, structure,
    // tools, cover, and ignition. The worker consumes it once when it
    // reserves a matching recipe's inputs.
    bool activation_requested = false;
    // A manual machine job is credited to the authenticated account that
    // queued it. The value persists while the request or active recipe exists
    // so a restart cannot silently lose a completed craft quest event.
    std::string job_owner_account_id;
    MachineRunState state = MachineRunState::Idle;
};

enum class MachineTickEventKind : uint8_t {
    StateChanged,
    RecipeCompleted,
};

struct MachineTickEvent {
    MachineTickEventKind kind = MachineTickEventKind::StateChanged;
    uint64_t tick_index = 0;
    snt::ecs::EntityGuid entity_guid;
    std::string machine_id;
    std::string recipe_id;
    std::string account_id;
    std::vector<MachineItemStack> outputs;
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

    // Set only on the simulation main thread before capture(). The value is
    // copied into barrier-published events so consumers never infer a tick
    // from wall-clock time or worker scheduling order.
    void set_tick_index(uint64_t tick_index) noexcept { tick_index_ = tick_index; }
    void set_event_sink(IMachineTickEventSink* event_sink) noexcept {
        event_sink_ = event_sink;
    }

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
    void log_unresolved_item_key(uint64_t generation, std::string_view key);

    GameContentRegistry& content_registry_;
    IMachineTickEventSink* event_sink_ = nullptr;
    uint64_t tick_index_ = 0;
    uint64_t unresolved_item_log_generation_ = 0;
    std::set<std::string, std::less<>> unresolved_item_keys_logged_;
};

}  // namespace snt::game
