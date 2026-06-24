#pragma once

#include "flow_node.hpp"
#include "flow_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// FlowNodeFactory — creates nodes with correct port layouts
// ============================================================
//
// Each node type has a fixed set of input and output ports. This
// factory populates a FlowNode with the correct ports for its type.
// The executor relies on these port layouts to read/write values.
//
// Port layout conventions (port id 0 is always the first):
//
//   TRIGGER_TIMER:     out[0]=FLOW
//   TRIGGER_REDSTONE:  out[0]=FLOW
//   TRIGGER_ITEM:      out[0]=FLOW
//
//   ITEM_INPUT:        out[0]=ITEM_STREAM
//   ITEM_OUTPUT:       in[0]=ITEM_STREAM
//
//   FLUID_INPUT:       out[0]=FLUID_STREAM
//   FLUID_OUTPUT:      in[0]=FLUID_STREAM
//
//   ENERGY_INPUT:      out[0]=ENERGY
//   ENERGY_OUTPUT:     in[0]=ENERGY
//
//   REDSTONE_INPUT:    out[0]=REDSTONE
//   REDSTONE_OUTPUT:   in[0]=REDSTONE
//
//   ITEM_FILTER:       in[0]=ITEM_STREAM, out[0]=ITEM_STREAM
//   FLUID_FILTER:      in[0]=FLUID_STREAM, out[0]=FLUID_STREAM
//
//   CONDITION:         in[0]=NUMBER(a), in[1]=NUMBER(b), out[0]=FLOW(true), out[1]=FLOW(false)
//   LOOP:              in[0]=FLOW(body), out[0]=FLOW(start), out[1]=FLOW(done)
//
//   GROUP_INPUT:       out[0]=FLOW
//   GROUP_OUTPUT:      in[0]=FLOW
//
//   VARIABLE_GET:      out[0]=(variable type)
//   VARIABLE_SET:      in[0]=(variable type), out[0]=FLOW
//
//   MATH:              in[0]=NUMBER(a), in[1]=NUMBER(b), out[0]=NUMBER
//   TEXT_LABEL:        (no ports; decoration only)

class FlowNodeFactory {
public:
    // Creates a node of the given type with default ports and params.
    static FlowNode create(FlowNodeId id, FlowNodeType type);

    // Returns the display name for a node type.
    static const char* type_name(FlowNodeType type) {
        return get_node_type_name(type);
    }

    // Returns true if the node type is a trigger (entry point).
    static bool is_trigger(FlowNodeType type) {
        return type == FlowNodeType::TRIGGER_TIMER
            || type == FlowNodeType::TRIGGER_REDSTONE
            || type == FlowNodeType::TRIGGER_ITEM;
    }
};

} // namespace science_and_theology::sfm
