#include "flow_node_factory.hpp"

namespace science_and_theology::sfm {

FlowNode FlowNodeFactory::create(FlowNodeId id, FlowNodeType type) {
    FlowNode node;
    node.id = id;
    node.type = type;

    auto add_in = [&](FlowPortId pid, FlowPortType ptype, const char* name) {
        node.input_ports.emplace_back(pid, ptype, name, true);
    };
    auto add_out = [&](FlowPortId pid, FlowPortType ptype, const char* name) {
        node.output_ports.emplace_back(pid, ptype, name, false);
    };

    switch (type) {
        // --- Triggers ---
        case FlowNodeType::TRIGGER_TIMER:
            add_out(0, FlowPortType::FLOW, "Trigger");
            node.params["interval_ticks"] = "20";
            break;
        case FlowNodeType::TRIGGER_REDSTONE:
            add_out(0, FlowPortType::FLOW, "Trigger");
            node.params["signal_mode"] = "any";
            break;
        case FlowNodeType::TRIGGER_ITEM:
            add_out(0, FlowPortType::FLOW, "Trigger");
            node.params["threshold"] = "1";
            break;
        case FlowNodeType::TRIGGER_SIGNAL:
            add_out(0, FlowPortType::FLOW, "Trigger");
            node.params["container_index"] = "0";
            node.params["signal_mode"] = "any";  // rising|falling|any
            break;

        // --- Item I/O ---
        case FlowNodeType::ITEM_INPUT:
            add_out(0, FlowPortType::ITEM_STREAM, "Items");
            node.params["max_items"] = "64";
            break;
        case FlowNodeType::ITEM_OUTPUT:
            add_in(0, FlowPortType::ITEM_STREAM, "Items");
            break;

        // --- Fluid I/O ---
        case FlowNodeType::FLUID_INPUT:
            add_out(0, FlowPortType::FLUID_STREAM, "Fluids");
            node.params["max_mb"] = "1000";
            break;
        case FlowNodeType::FLUID_OUTPUT:
            add_in(0, FlowPortType::FLUID_STREAM, "Fluids");
            break;

        // --- Energy I/O ---
        case FlowNodeType::ENERGY_INPUT:
            add_out(0, FlowPortType::ENERGY, "Energy");
            node.params["max_eu"] = "1000";
            break;
        case FlowNodeType::ENERGY_OUTPUT:
            add_in(0, FlowPortType::ENERGY, "Energy");
            break;

        // --- Redstone I/O ---
        case FlowNodeType::REDSTONE_INPUT:
            add_out(0, FlowPortType::REDSTONE, "Signal");
            break;
        case FlowNodeType::REDSTONE_OUTPUT:
            add_in(0, FlowPortType::REDSTONE, "Signal");
            break;

        // --- Signal I/O ---
        case FlowNodeType::SIGNAL_INPUT:
            add_out(0, FlowPortType::SIGNAL, "Signal");
            node.params["container_index"] = "0";
            break;
        case FlowNodeType::SIGNAL_OUTPUT:
            add_in(0, FlowPortType::SIGNAL, "Signal");
            node.params["container_index"] = "0";
            break;

        // --- Filters ---
        case FlowNodeType::ITEM_FILTER:
            add_in(0, FlowPortType::ITEM_STREAM, "In");
            add_out(0, FlowPortType::ITEM_STREAM, "Out");
            break;
        case FlowNodeType::FLUID_FILTER:
            add_in(0, FlowPortType::FLUID_STREAM, "In");
            add_out(0, FlowPortType::FLUID_STREAM, "Out");
            break;

        // --- Control flow ---
        case FlowNodeType::CONDITION:
            add_in(0, FlowPortType::NUMBER, "A");
            add_in(1, FlowPortType::NUMBER, "B");
            add_out(0, FlowPortType::FLOW, "True");
            add_out(1, FlowPortType::FLOW, "False");
            node.params["compare_op"] = "0";
            break;
        case FlowNodeType::LOOP:
            add_out(0, FlowPortType::FLOW, "Body");
            add_out(1, FlowPortType::FLOW, "Done");
            node.params["iterations"] = "1";
            break;
        case FlowNodeType::GROUP_INPUT:
            add_out(0, FlowPortType::FLOW, "In");
            break;
        case FlowNodeType::GROUP_OUTPUT:
            add_in(0, FlowPortType::FLOW, "Out");
            break;

        // --- Data ---
        case FlowNodeType::VARIABLE_GET:
            add_out(0, FlowPortType::NUMBER, "Value");
            node.params["variable_id"] = "0";
            break;
        case FlowNodeType::VARIABLE_SET:
            add_in(0, FlowPortType::NUMBER, "Value");
            add_out(0, FlowPortType::FLOW, "Done");
            node.params["variable_id"] = "0";
            break;
        case FlowNodeType::MATH:
            add_in(0, FlowPortType::NUMBER, "A");
            add_in(1, FlowPortType::NUMBER, "B");
            add_out(0, FlowPortType::NUMBER, "Result");
            node.params["op"] = "0";
            break;
        case FlowNodeType::TEXT_LABEL:
            node.params["text"] = "Label";
            break;

        default:
            break;
    }

    return node;
}

} // namespace science_and_theology::sfm
