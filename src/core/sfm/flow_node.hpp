#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "flow_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// FlowPort — a single input or output socket on a node
// ============================================================

struct FlowPort {
    FlowPortId id = 0;
    FlowPortType type = FlowPortType::NONE;
    std::string name;       // Display name shown in the editor
    bool is_input = true;   // true = input, false = output

    FlowPort() = default;
    FlowPort(FlowPortId id_, FlowPortType type_, std::string name_, bool is_input_)
        : id(id_), type(type_), name(std::move(name_)), is_input(is_input_) {}
};

// ============================================================
// FlowNode — a single node in the flow graph
// ============================================================
//
// Each node has a fixed set of input and output ports determined by
// its type (see flow_node_factory). Node-specific behavior is driven
// by `params` (key-value strings) and embedded filter definitions.
//
// For filter nodes, `item_filter` / `fluid_filter` hold the filter
// data directly so the executor can apply them without parsing params.

struct FlowNode {
    FlowNodeId id = kInvalidFlowNodeId;
    FlowNodeType type = FlowNodeType::TRIGGER_TIMER;

    // Editor position (graph-space, not world-space).
    float editor_x = 0.0f;
    float editor_y = 0.0f;

    // Ports.
    std::vector<FlowPort> input_ports;
    std::vector<FlowPort> output_ports;

    // Generic key-value parameters.
    // Common keys per node type:
    //   TRIGGER_TIMER:   "interval_ticks" = N
    //   TRIGGER_REDSTONE:"signal_mode" = "rising"|"falling"|"any"
    //   TRIGGER_ITEM:    "container_index" = N, "item_id" = N, "threshold" = N
    //   ITEM_INPUT:      "container_index" = N, "max_items" = N
    //   ITEM_OUTPUT:     "container_index" = N
    //   FLUID_INPUT:     "container_index" = N, "max_mb" = N
    //   FLUID_OUTPUT:    "container_index" = N
    //   ENERGY_INPUT:    "container_index" = N, "max_eu" = N
    //   ENERGY_OUTPUT:   "container_index" = N
    //   REDSTONE_INPUT:  "container_index" = N
    //   REDSTONE_OUTPUT: "container_index" = N
    //   TRIGGER_SIGNAL:  "container_index" = N, "signal_mode" = "rising"|"falling"|"any"
    //   SIGNAL_INPUT:    "container_index" = N
    //   SIGNAL_OUTPUT:   "container_index" = N
    //   CONDITION:       "compare_op" = CompareOp index
    //   LOOP:            "iterations" = N
    //   MATH:            "op" = MathOp index
    //   VARIABLE_GET:    "variable_id" = N
    //   VARIABLE_SET:    "variable_id" = N
    //   TEXT_LABEL:      "text" = "..."
    std::unordered_map<std::string, std::string> params;

    // Embedded filters (used by ITEM_FILTER / FLUID_FILTER nodes).
    ItemFilterDef item_filter;
    FluidFilterDef fluid_filter;

    // Convenience port lookup.
    const FlowPort* find_input_port(FlowPortId pid) const {
        for (const auto& p : input_ports) {
            if (p.id == pid) return &p;
        }
        return nullptr;
    }
    const FlowPort* find_output_port(FlowPortId pid) const {
        for (const auto& p : output_ports) {
            if (p.id == pid) return &p;
        }
        return nullptr;
    }
};

} // namespace science_and_theology::sfm
