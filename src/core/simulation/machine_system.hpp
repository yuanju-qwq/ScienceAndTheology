#pragma once

#include <unordered_map>

#include "simulation_system.hpp"
#include "../machine/machine.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

// Machine simulation subsystem — the main consumer for the Machine class.
//
// Maintains a registry of MachineId -> Machine* for active machines.
// During tick_active(), iterates over machines in ACTIVE chunks,
// calls Machine::tick() on each, and emits lifecycle events via EventBus.
// During tick_sleeping(), applies low-frequency approximate processing.
class MachineSystem : public SimulationSystem {
public:
    MachineSystem() = default;

    SIMULATION_SYSTEM_NAME(MachineSystem, "MachineSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;
    int priority() const override { return 1; }

    // Register a machine for simulation. Ownership remains with caller.
    void register_machine(MachineId id, gt::Machine* machine);
    void unregister_machine(MachineId id);

    // Returns the number of registered machines.
    size_t machine_count() const { return machine_registry_.size(); }

private:
    void wire_callbacks(MachineId id, gt::Machine* machine);

    std::unordered_map<MachineId, gt::Machine*> machine_registry_;
};

} // namespace science_and_theology
