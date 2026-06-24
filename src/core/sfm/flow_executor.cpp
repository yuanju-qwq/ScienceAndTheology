#include "flow_executor.hpp"

#include <algorithm>

namespace science_and_theology::sfm {

FlowExecutor::FlowExecutor(FlowProgram& program, VariableStore& variables,
                           ContainerRegistry& containers)
    : program_(program), variables_(variables), containers_(containers) {}

void FlowExecutor::tick(int64_t current_tick) {
    current_tick_ = current_tick;
    triggered_last_tick_ = false;
    last_node_count_ = 0;

    // Evaluate all trigger nodes.
    auto triggers = program_.get_trigger_nodes();
    for (FlowNodeId trigger_id : triggers) {
        const FlowNode* node = program_.get_node(trigger_id);
        if (!node) continue;
        if (!should_trigger(*node, current_tick)) continue;

        triggered_last_tick_ = true;
        // Reset per-cycle state for this trigger execution.
        activated_.clear();
        port_values_.clear();
        active_flow_outputs_.clear();

        activate(trigger_id, 0);
        last_node_count_ += static_cast<int64_t>(activated_.size());
    }
}

void FlowExecutor::activate(FlowNodeId node_id, int depth) {
    if (depth > kMaxDepth) return;
    // Cycle protection: each node activates at most once per cycle.
    if (activated_.count(node_id)) return;
    activated_.insert(node_id);

    const FlowNode* node = program_.get_node(node_id);
    if (!node) return;

    // Execute node logic — sets output data values and marks active FLOW ports.
    execute_node(*node);

    // Propagate FLOW signal from all active FLOW output ports.
    for (const FlowPort& port : node->output_ports) {
        if (port.type != FlowPortType::FLOW) continue;
        if (!is_flow_active(node_id, port.id)) continue;
        propagate_flow(node_id, port.id, depth);
    }
}

void FlowExecutor::propagate_flow(FlowNodeId node_id, FlowPortId port_id,
                                   int depth) {
    auto conns = program_.get_connections_from(node_id, port_id);
    for (const FlowConnection* conn : conns) {
        activate(conn->to_node, depth + 1);
    }
}

void FlowExecutor::execute_node(const FlowNode& node) {
    switch (node.type) {
        case FlowNodeType::TRIGGER_TIMER:
        case FlowNodeType::TRIGGER_REDSTONE:
        case FlowNodeType::TRIGGER_ITEM:
            // Triggers only produce a FLOW signal.
            mark_flow_active(node.id, 0);
            break;
        case FlowNodeType::ITEM_INPUT:    exec_item_input(node); break;
        case FlowNodeType::ITEM_OUTPUT:   exec_item_output(node); break;
        case FlowNodeType::FLUID_INPUT:   exec_fluid_input(node); break;
        case FlowNodeType::FLUID_OUTPUT:  exec_fluid_output(node); break;
        case FlowNodeType::ENERGY_INPUT:  exec_energy_input(node); break;
        case FlowNodeType::ENERGY_OUTPUT: exec_energy_output(node); break;
        case FlowNodeType::REDSTONE_INPUT:  exec_redstone_input(node); break;
        case FlowNodeType::REDSTONE_OUTPUT: exec_redstone_output(node); break;
        case FlowNodeType::ITEM_FILTER:   exec_item_filter(node); break;
        case FlowNodeType::FLUID_FILTER:  exec_fluid_filter(node); break;
        case FlowNodeType::CONDITION:     exec_condition(node); break;
        case FlowNodeType::LOOP:          exec_loop(node); break;
        case FlowNodeType::VARIABLE_GET:  exec_variable_get(node); break;
        case FlowNodeType::VARIABLE_SET:  exec_variable_set(node); break;
        case FlowNodeType::MATH:          exec_math(node); break;
        case FlowNodeType::TEXT_LABEL:
        case FlowNodeType::GROUP_INPUT:
        case FlowNodeType::GROUP_OUTPUT:
            // Decoration / passthrough — propagate flow.
            for (const auto& p : node.output_ports) {
                if (p.type == FlowPortType::FLOW) mark_flow_active(node.id, p.id);
            }
            break;
        default:
            break;
    }
}

FlowValue FlowExecutor::evaluate_input(const FlowNode& node,
                                        FlowPortId port_id) {
    const FlowConnection* conn = program_.get_connection_to(node.id, port_id);
    if (!conn) return FlowValue{};
    const FlowValue* val = get_output_value(conn->from_node, conn->from_port);
    return val ? *val : FlowValue{};
}

bool FlowExecutor::should_trigger(const FlowNode& trigger, int64_t tick) {
    switch (trigger.type) {
        case FlowNodeType::TRIGGER_TIMER: {
            int64_t interval = get_param_int(trigger, "interval_ticks", 20);
            if (interval <= 0) interval = 20;
            int64_t last = last_timer_fire_[trigger.id];
            if (tick - last >= interval) {
                last_timer_fire_[trigger.id] = tick;
                return true;
            }
            return false;
        }
        case FlowNodeType::TRIGGER_REDSTONE: {
            IContainerAccess* c = get_container(trigger);
            if (!c || !c->has_redstone()) return false;
            int32_t signal = c->get_redstone_signal();
            int32_t prev = last_redstone_signal_[trigger.id];
            last_redstone_signal_[trigger.id] = signal;
            std::string mode = trigger.params.count("signal_mode")
                ? trigger.params.at("signal_mode") : std::string("any");
            if (mode == "rising")  return signal > 0 && prev == 0;
            if (mode == "falling") return signal == 0 && prev > 0;
            // "any" — fire on any change.
            return signal != prev;
        }
        case FlowNodeType::TRIGGER_ITEM: {
            IContainerAccess* c = get_container(trigger);
            if (!c || !c->has_items()) return false;
            int64_t item_id = get_param_int(trigger, "item_id", 0);
            int64_t threshold = get_param_int(trigger, "threshold", 1);
            return c->count_item(static_cast<gt::ItemId>(item_id)) >= threshold;
        }
        default:
            return false;
    }
}

// ============================================================
// Item I/O
// ============================================================

void FlowExecutor::exec_item_input(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    if (!c || !c->has_items()) {
        mark_flow_active(node.id, 0);
        return;
    }
    int64_t max_items = get_param_int(node, "max_items", 64);

    // Read current items from the container.
    auto items = c->list_items();

    // Apply optional filter from input port (if connected to a filter node,
    // the filter already ran and its output is the filtered stream).
    FlowValue input_stream = evaluate_input(node, 0);
    // If an input ITEM_STREAM is connected, it means "only extract these
    // item types" (used as a filter source). Otherwise extract all.
    bool use_input_filter = (input_stream.type == FlowPortType::ITEM_STREAM
                             && !input_stream.items.empty());

    FlowValue out;
    out.type = FlowPortType::ITEM_STREAM;
    int64_t remaining = max_items;
    for (const auto& entry : items) {
        if (remaining <= 0) break;
        if (use_input_filter) {
            bool found = false;
            for (const auto& f : input_stream.items) {
                if (f.item_id == entry.item_id) { found = true; break; }
            }
            if (!found) continue;
        }
        int64_t to_take = std::min(entry.count, remaining);
        int64_t actual = c->extract_item(entry.item_id, to_take);
        if (actual > 0) {
            out.items.push_back({entry.item_id, actual});
            remaining -= actual;
        }
    }
    set_output_value(node.id, 0, std::move(out));
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_item_output(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    FlowValue stream = evaluate_input(node, 0);
    if (c && c->has_items() && stream.type == FlowPortType::ITEM_STREAM) {
        for (const auto& entry : stream.items) {
            c->insert_item(entry.item_id, entry.count);
        }
    }
    mark_flow_active(node.id, 0);
}

// ============================================================
// Fluid I/O
// ============================================================

void FlowExecutor::exec_fluid_input(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    if (!c || !c->has_fluids()) {
        mark_flow_active(node.id, 0);
        return;
    }
    int64_t max_mb = get_param_int(node, "max_mb", 1000);

    auto fluids = c->list_fluids();
    FlowValue input_stream = evaluate_input(node, 0);
    bool use_input_filter = (input_stream.type == FlowPortType::FLUID_STREAM
                             && !input_stream.fluids.empty());

    FlowValue out;
    out.type = FlowPortType::FLUID_STREAM;
    int64_t remaining = max_mb;
    for (const auto& entry : fluids) {
        if (remaining <= 0) break;
        if (use_input_filter) {
            bool found = false;
            for (const auto& f : input_stream.fluids) {
                if (f.fluid_id == entry.fluid_id) { found = true; break; }
            }
            if (!found) continue;
        }
        int64_t to_take = std::min(entry.amount_mb, remaining);
        int64_t actual = c->extract_fluid(entry.fluid_id, to_take);
        if (actual > 0) {
            out.fluids.push_back({entry.fluid_id, actual});
            remaining -= actual;
        }
    }
    set_output_value(node.id, 0, std::move(out));
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_fluid_output(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    FlowValue stream = evaluate_input(node, 0);
    if (c && c->has_fluids() && stream.type == FlowPortType::FLUID_STREAM) {
        for (const auto& entry : stream.fluids) {
            c->insert_fluid(entry.fluid_id, entry.amount_mb);
        }
    }
    mark_flow_active(node.id, 0);
}

// ============================================================
// Energy I/O
// ============================================================

void FlowExecutor::exec_energy_input(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    if (!c || !c->has_energy()) {
        mark_flow_active(node.id, 0);
        return;
    }
    int64_t max_eu = get_param_int(node, "max_eu", 1000);
    int64_t to_take = std::min(max_eu, c->get_energy_stored());
    int64_t actual = c->extract_energy(to_take);

    FlowValue out = FlowValue::make_energy(actual);
    set_output_value(node.id, 0, std::move(out));
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_energy_output(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    FlowValue val = evaluate_input(node, 0);
    if (c && c->has_energy() && val.type == FlowPortType::ENERGY) {
        c->insert_energy(val.energy);
    }
    mark_flow_active(node.id, 0);
}

// ============================================================
// Redstone I/O
// ============================================================

void FlowExecutor::exec_redstone_input(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    int32_t signal = 0;
    if (c && c->has_redstone()) {
        signal = c->get_redstone_signal();
    }
    FlowValue out = FlowValue::make_redstone(signal);
    set_output_value(node.id, 0, std::move(out));
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_redstone_output(const FlowNode& node) {
    IContainerAccess* c = get_container(node);
    FlowValue val = evaluate_input(node, 0);
    if (c && c->has_redstone() && val.type == FlowPortType::REDSTONE) {
        c->set_redstone_signal(val.redstone);
    }
    mark_flow_active(node.id, 0);
}

// ============================================================
// Filters
// ============================================================

void FlowExecutor::exec_item_filter(const FlowNode& node) {
    FlowValue stream = evaluate_input(node, 0);
    if (stream.type == FlowPortType::ITEM_STREAM) {
        FlowValue filtered;
        filtered.type = FlowPortType::ITEM_STREAM;
        for (const auto& entry : stream.items) {
            if (node.item_filter.matches(entry.item_id)) {
                filtered.items.push_back(entry);
            }
        }
        set_output_value(node.id, 0, std::move(filtered));
    }
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_fluid_filter(const FlowNode& node) {
    FlowValue stream = evaluate_input(node, 0);
    if (stream.type == FlowPortType::FLUID_STREAM) {
        FlowValue filtered;
        filtered.type = FlowPortType::FLUID_STREAM;
        for (const auto& entry : stream.fluids) {
            if (node.fluid_filter.matches(entry.fluid_id)) {
                filtered.fluids.push_back(entry);
            }
        }
        set_output_value(node.id, 0, std::move(filtered));
    }
    mark_flow_active(node.id, 0);
}

// ============================================================
// Control flow
// ============================================================

void FlowExecutor::exec_condition(const FlowNode& node) {
    // Port layout: in[0]=NUMBER(a), in[1]=NUMBER(b)
    //              out[0]=FLOW(true), out[1]=FLOW(false)
    FlowValue a = evaluate_input(node, 0);
    FlowValue b = evaluate_input(node, 1);

    int64_t va = (a.type == FlowPortType::NUMBER) ? a.number : 0;
    int64_t vb = (b.type == FlowPortType::NUMBER) ? b.number : 0;

    CompareOp op = static_cast<CompareOp>(
        get_param_int(node, "compare_op", 0));

    bool result = false;
    switch (op) {
        case CompareOp::EQUAL:          result = (va == vb); break;
        case CompareOp::NOT_EQUAL:      result = (va != vb); break;
        case CompareOp::LESS:           result = (va <  vb); break;
        case CompareOp::LESS_EQUAL:     result = (va <= vb); break;
        case CompareOp::GREATER:        result = (va >  vb); break;
        case CompareOp::GREATER_EQUAL:  result = (va >= vb); break;
        default: break;
    }
    // Propagate only the matching branch.
    mark_flow_active(node.id, result ? 0 : 1);
}

void FlowExecutor::exec_loop(const FlowNode& node) {
    // Port layout: out[0]=FLOW(body start), out[1]=FLOW(done)
    // The loop activates the body port N times, then the done port.
    // Since each node can only activate once per cycle (cycle protection),
    // the loop uses a bounded inline execution: it directly activates
    // body-connected nodes up to N times by clearing the activated set
    // for the body subgraph each iteration.
    //
    // Simplified model: the loop propagates the body FLOW once (the body
    // subgraph executes once), and iteration count is handled by the
    // timer trigger re-firing. Full inline iteration would require
    // subgraph isolation which is complex; this keeps the MVP stable.
    int64_t iterations = get_param_int(node, "iterations", 1);
    if (iterations > 0) {
        mark_flow_active(node.id, 0);  // body
    } else {
        mark_flow_active(node.id, 1);  // done
    }
}

// ============================================================
// Data nodes
// ============================================================

void FlowExecutor::exec_variable_get(const FlowNode& node) {
    FlowVariableId vid = static_cast<FlowVariableId>(
        get_param_int(node, "variable_id", 0));
    const FlowVariable* var = variables_.get_variable(vid);
    if (var) {
        set_output_value(node.id, 0, var->value);
    }
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_variable_set(const FlowNode& node) {
    // Port layout: in[0]=(variable type value), out[0]=FLOW
    FlowVariableId vid = static_cast<FlowVariableId>(
        get_param_int(node, "variable_id", 0));
    FlowVariable* var = variables_.get_variable(vid);
    if (var) {
        FlowValue val = evaluate_input(node, 0);
        if (val.type == var->type || val.type == FlowPortType::NONE) {
            var->value = std::move(val);
        }
    }
    mark_flow_active(node.id, 0);
}

void FlowExecutor::exec_math(const FlowNode& node) {
    // Port layout: in[0]=NUMBER(a), in[1]=NUMBER(b), out[0]=NUMBER
    FlowValue a = evaluate_input(node, 0);
    FlowValue b = evaluate_input(node, 1);

    int64_t va = (a.type == FlowPortType::NUMBER) ? a.number : 0;
    int64_t vb = (b.type == FlowPortType::NUMBER) ? b.number : 0;

    MathOp op = static_cast<MathOp>(get_param_int(node, "op", 0));
    int64_t result = 0;
    switch (op) {
        case MathOp::ADD:      result = va + vb; break;
        case MathOp::SUBTRACT: result = va - vb; break;
        case MathOp::MULTIPLY: result = va * vb; break;
        case MathOp::DIVIDE:   result = (vb != 0) ? va / vb : 0; break;
        case MathOp::MODULO:   result = (vb != 0) ? va % vb : 0; break;
        case MathOp::MIN:      result = std::min(va, vb); break;
        case MathOp::MAX:      result = std::max(va, vb); break;
        default: break;
    }
    set_output_value(node.id, 0, FlowValue::make_number(result));
    mark_flow_active(node.id, 0);
}

// ============================================================
// Cache helpers
// ============================================================

void FlowExecutor::set_output_value(FlowNodeId node, FlowPortId port,
                                     FlowValue value) {
    port_values_[port_key(node, port)] = std::move(value);
}

const FlowValue* FlowExecutor::get_output_value(FlowNodeId node,
                                                 FlowPortId port) const {
    auto it = port_values_.find(port_key(node, port));
    return it == port_values_.end() ? nullptr : &it->second;
}

void FlowExecutor::mark_flow_active(FlowNodeId node, FlowPortId port) {
    active_flow_outputs_.insert(port_key(node, port));
}

bool FlowExecutor::is_flow_active(FlowNodeId node, FlowPortId port) const {
    return active_flow_outputs_.count(port_key(node, port)) > 0;
}

int64_t FlowExecutor::get_param_int(const FlowNode& node,
                                     const std::string& key,
                                     int64_t default_val) const {
    auto it = node.params.find(key);
    if (it == node.params.end()) return default_val;
    try {
        return std::stoll(it->second);
    } catch (...) {
        return default_val;
    }
}

IContainerAccess* FlowExecutor::get_container(const FlowNode& node) {
    uint32_t index = static_cast<uint32_t>(
        get_param_int(node, "container_index", 0));
    return containers_.get_by_index(index);
}

} // namespace science_and_theology::sfm
