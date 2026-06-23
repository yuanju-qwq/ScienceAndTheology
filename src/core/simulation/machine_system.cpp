#include "machine_system.hpp"
#include "event_bus.hpp"
#include "../multiblock/multiblock_machine_service.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology {

void MachineSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void MachineSystem::tick_active(const ChunkKey& chunk, float delta,
                                const TickContext* ctx) {
    if (!world_ || !event_bus_) return;

    auto* chunk_data = world_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (!chunk_data) return;

    // Iterate over all machines in this chunk and tick only formed machines.
    for (const auto& mid : chunk_data->machines) {
        auto it = machine_registry_.find(mid);
        if (it == machine_registry_.end()) continue;

        gt::Machine* machine = it->second;
        if (!machine) continue;
        if (!multiblock::MultiblockMachineService::can_tick_machine(*world_, mid)) {
            continue;
        }

        // Power injection: machines draw power from their connected network.
        // This is mediated by the game loop; for now, assume power is set
        // externally before tick.
        machine->tick();
    }

    // Mark chunk as dirty for machine state sync.
    (void)delta;
    (void)ctx;
}

void MachineSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                 const TickContext* ctx) {
    if (!world_ || !event_bus_) return;

    auto* chunk_data = world_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (!chunk_data) return;

    // Compute the number of ticks to advance.
    // delta is scaled by the sleep interval (e.g., delta=0.05 * interval=20 = 1s).
    // At 20 TPS, one tick = 0.05s, so ticks = delta / 0.05 = delta * kTicksPerSecond.
    constexpr float kTicksPerSecond = 20.0f;
    const int64_t ticks_to_advance = static_cast<int64_t>(
        std::max(1.0f, delta * kTicksPerSecond));

    for (const auto& mid : chunk_data->machines) {
        auto it = machine_registry_.find(mid);
        if (it == machine_registry_.end()) continue;

        gt::Machine* machine = it->second;
        if (!machine) continue;
        if (!multiblock::MultiblockMachineService::can_tick_machine(*world_, mid)) {
            continue;
        }

        // Sleeping machines advance by the accumulated tick count.
        // This keeps their production rate identical to active machines;
        // only the update frequency is lower.
        machine->tick_n(ticks_to_advance);
    }
    (void)ctx;
}

void MachineSystem::shutdown() {
    machine_registry_.clear();
    world_ = nullptr;
    event_bus_ = nullptr;
}

void MachineSystem::register_machine(MachineId id, gt::Machine* machine) {
    if (!machine) return;
    wire_callbacks(id, machine);
    machine_registry_[id] = machine;
}

void MachineSystem::unregister_machine(MachineId id) {
    auto it = machine_registry_.find(id);
    if (it != machine_registry_.end() && it->second) {
        it->second->set_state_change_callback(nullptr);
        it->second->set_recipe_complete_callback(nullptr);
        it->second->set_recipe_started_callback(nullptr);
        it->second->set_error_callback(nullptr);
    }
    machine_registry_.erase(id);
}

void MachineSystem::wire_callbacks(MachineId id, gt::Machine* machine) {
    machine->set_machine_id(id);

    machine->set_state_change_callback(
        [this](MachineId mid, gt::MachineState old_s, gt::MachineState new_s) {
            if (!event_bus_) return;
            event_bus_->emit(GameEvent::machine_state_changed(
                mid.id,
                static_cast<int>(old_s),
                static_cast<int>(new_s)));
        });

    machine->set_recipe_complete_callback(
        [this](MachineId mid, const gt::Recipe& recipe) {
            if (!event_bus_) return;
            event_bus_->emit(GameEvent::machine_recipe_completed(
                mid.id, recipe.name));
        });

    machine->set_recipe_started_callback(
        [this](MachineId mid, const gt::Recipe& recipe) {
            if (!event_bus_) return;
            GameEvent ev;
            ev.type = GameEventType::MACHINE_RECIPE_STARTED;
            ev.source_id = mid.id;
            ev.string_data["recipe"] = recipe.name;
            event_bus_->emit(ev);
        });

    machine->set_error_callback(
        [this](MachineId mid, const char* code, const char* msg) {
            if (!event_bus_) return;
            event_bus_->emit(GameEvent::machine_error(mid.id, code, msg));
        });
}

} // namespace science_and_theology
