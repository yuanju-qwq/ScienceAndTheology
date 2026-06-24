#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "flow_node.hpp"
#include "flow_program.hpp"
#include "flow_variable.hpp"
#include "i_container_access.hpp"

namespace science_and_theology::sfm {

// ============================================================
// FlowExecutor — per-tick flow program execution engine
// ============================================================
//
// Execution model (mirrors Steve's Factory Manager):
//
//   1. Each tick, the executor checks all trigger nodes. A trigger
//      fires when its condition is met (timer interval elapsed,
//      redstone signal changed, item threshold reached).
//
//   2. When a trigger fires, the executor "activates" it: the trigger
//      produces a FLOW signal on its output port.
//
//   3. The FLOW signal propagates along FLOW connections to downstream
//      nodes. Each downstream node is activated in turn.
//
//   4. When a node is activated, it executes its logic:
//      - Reads values from its input DATA ports (traced back through
//        data connections to the source node's output port values,
//        which were set when that source node was activated earlier
//        in the same cycle).
//      - Performs its operation (I/O, filter, math, condition, etc.).
//      - Sets values on its output DATA ports.
//      - Determines which of its FLOW output ports propagate (most
//        nodes propagate all; CONDITION propagates only the matching
//        branch).
//
//   5. Propagation continues until no more FLOW connections remain.
//
// Data flow is "pull-on-activate": a node reads its input data only
// when activated, and the value comes from whatever the source node
// produced when IT was activated. If the source was not activated in
// this cycle, the input is empty.
//
// Cycle protection: a node activated once in a cycle is not activated
// again (prevents infinite loops in cyclic flow graphs). LOOP nodes
// use a bounded iteration counter instead of re-activation.

class FlowExecutor {
public:
    // Maximum flow propagation depth per trigger (prevents runaway).
    static constexpr int kMaxDepth = 512;

    FlowExecutor(FlowProgram& program, VariableStore& variables,
                 ContainerRegistry& containers);

    // Advances the simulation by one tick. Evaluates triggers and
    // executes any triggered flow programs.
    void tick(int64_t current_tick);

    // --- Trigger state inspection (for UI / debugging) ---
    bool was_triggered_last_tick() const { return triggered_last_tick_; }
    int64_t get_last_execution_node_count() const { return last_node_count_; }

private:
    // Activate a node: execute its logic, then propagate FLOW outputs.
    void activate(FlowNodeId node_id, int depth);

    // Execute a single node's logic. Sets output data port values and
    // marks which FLOW output ports should propagate.
    void execute_node(const FlowNode& node);

    // Propagate FLOW signal from a specific output port.
    void propagate_flow(FlowNodeId node_id, FlowPortId port_id, int depth);

    // Evaluate the value on a node's input DATA port by tracing its
    // incoming data connection to the source output port.
    FlowValue evaluate_input(const FlowNode& node, FlowPortId port_id);

    // Check if a trigger node should fire this tick.
    bool should_trigger(const FlowNode& trigger, int64_t tick);

    // --- Per-node-type execution handlers ---
    void exec_item_input(const FlowNode& node);
    void exec_item_output(const FlowNode& node);
    void exec_fluid_input(const FlowNode& node);
    void exec_fluid_output(const FlowNode& node);
    void exec_energy_input(const FlowNode& node);
    void exec_energy_output(const FlowNode& node);
    void exec_redstone_input(const FlowNode& node);
    void exec_redstone_output(const FlowNode& node);
    void exec_item_filter(const FlowNode& node);
    void exec_fluid_filter(const FlowNode& node);
    void exec_condition(const FlowNode& node);
    void exec_loop(const FlowNode& node);
    void exec_variable_get(const FlowNode& node);
    void exec_variable_set(const FlowNode& node);
    void exec_math(const FlowNode& node);

    // --- Port value cache (per execution cycle) ---
    void set_output_value(FlowNodeId node, FlowPortId port, FlowValue value);
    const FlowValue* get_output_value(FlowNodeId node, FlowPortId port) const;

    // --- FLOW output activation (per execution cycle) ---
    // Marks a FLOW output port as "should propagate".
    void mark_flow_active(FlowNodeId node, FlowPortId port);
    bool is_flow_active(FlowNodeId node, FlowPortId port) const;

    // --- Helpers ---
    int64_t get_param_int(const FlowNode& node, const std::string& key,
                          int64_t default_val) const;
    IContainerAccess* get_container(const FlowNode& node);

    static uint64_t port_key(FlowNodeId node, FlowPortId port) {
        return (static_cast<uint64_t>(node) << 8) | static_cast<uint64_t>(port);
    }

    // --- External references ---
    FlowProgram& program_;
    VariableStore& variables_;
    ContainerRegistry& containers_;

    // --- Per-tick state ---
    int64_t current_tick_ = 0;
    bool triggered_last_tick_ = false;
    int64_t last_node_count_ = 0;

    // --- Per-execution-cycle state (reset each trigger) ---
    std::unordered_set<FlowNodeId> activated_;
    std::unordered_map<uint64_t, FlowValue> port_values_;
    std::unordered_set<uint64_t> active_flow_outputs_;

    // --- Persistent trigger state ---
    // Last tick each timer trigger fired.
    std::unordered_map<FlowNodeId, int64_t> last_timer_fire_;
    // Last redstone signal seen per redstone trigger (for edge detection).
    std::unordered_map<FlowNodeId, int32_t> last_redstone_signal_;
};

} // namespace science_and_theology::sfm
