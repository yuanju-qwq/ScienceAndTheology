#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../power/power_node.hpp"
#include "cable_graph.hpp"
#include "flow_executor.hpp"
#include "flow_program.hpp"
#include "flow_variable.hpp"
#include "i_container_access.hpp"

namespace science_and_theology::sfm {

// ============================================================
// SFMManager — runtime state for one Manager block
// ============================================================
//
// Each Manager block in the world owns one SFMManager instance. It
// bundles:
//   - The flow program (node graph)
//   - The variable store
//   - The container registry (discovered via cables)
//   - The cable graph (topology)
//   - The flow executor (per-tick simulation)
//
// The bindings layer (GDFlowManager) wraps this and exposes it to
// GDScript. The simulation tick system calls tick() each tick.

class SFMManager {
public:
    SFMManager();
    ~SFMManager() = default;

    SFMManager(const SFMManager&) = delete;
    SFMManager& operator=(const SFMManager&) = delete;

    // --- Position ---
    void set_position(gt::MapPosition pos) {
        position_ = pos;
        cable_graph_.set_manager_position(pos);
    }
    gt::MapPosition get_position() const { return position_; }

    // --- Component access ---
    FlowProgram& program() { return program_; }
    const FlowProgram& program() const { return program_; }
    VariableStore& variables() { return variables_; }
    const VariableStore& variables() const { return variables_; }
    ContainerRegistry& containers() { return containers_; }
    const ContainerRegistry& containers() const { return containers_; }
    CableGraph& cable_graph() { return cable_graph_; }
    const CableGraph& cable_graph() const { return cable_graph_; }

    // --- Simulation ---
    void tick(int64_t current_tick) { executor_.tick(current_tick); }
    bool was_triggered_last_tick() const { return executor_.was_triggered_last_tick(); }
    int64_t get_last_execution_node_count() const {
        return executor_.get_last_execution_node_count();
    }

    // --- Cable management ---
    void add_cable(gt::MapPosition pos) { cable_graph_.add_cable(pos); }
    void remove_cable(gt::MapPosition pos) { cable_graph_.remove_cable(pos); }

    // --- Container discovery ---
    // Scans the cable graph and registers all reachable containers.
    // is_container: predicate — is this position a container?
    // factory: creates an IContainerAccess for a container position.
    // Clears and rebuilds the container registry.
    void discover_containers(
        const std::function<bool(gt::MapPosition)>& is_container,
        const std::function<std::unique_ptr<IContainerAccess>(gt::MapPosition)>& factory);

    // --- Serialization ---
    // Serializes program + variables to a string for save/load.
    std::string serialize() const;
    bool deserialize(const std::string& data);

private:
    gt::MapPosition position_;
    FlowProgram program_;
    VariableStore variables_;
    ContainerRegistry containers_;
    CableGraph cable_graph_;
    FlowExecutor executor_;
};

} // namespace science_and_theology::sfm
