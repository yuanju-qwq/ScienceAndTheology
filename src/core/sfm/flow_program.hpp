#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "flow_node.hpp"
#include "flow_types.hpp"

namespace science_and_theology::sfm {

// ============================================================
// FlowConnection — a directed link from one port to another
// ============================================================
//
// SFM semantics:
//   - FLOW ports: one output may fan-out to multiple inputs (a trigger
//     can activate several downstream nodes). Each input accepts one
//     incoming flow connection.
//   - Data ports: each input accepts exactly one incoming connection
//     (a port has a single value source). Outputs may fan-out.

struct FlowConnection {
    FlowConnectionId id = kInvalidFlowConnectionId;
    FlowNodeId from_node = kInvalidFlowNodeId;
    FlowPortId from_port = 0;
    FlowNodeId to_node = kInvalidFlowNodeId;
    FlowPortId to_port = 0;
    FlowPortType port_type = FlowPortType::NONE;
};

// ============================================================
// FlowProgram — the complete node graph for one Manager
// ============================================================
//
// Owns all nodes and connections. Provides topology queries used by
// the executor: fan-out from an output port, the single source feeding
// an input port, and the set of trigger (entry-point) nodes.

class FlowProgram {
public:
    FlowProgram() = default;
    ~FlowProgram() = default;

    FlowProgram(const FlowProgram&) = delete;
    FlowProgram& operator=(const FlowProgram&) = delete;

    // --- Node lifecycle ---

    // Adds a node with a generated id. Returns the new id.
    FlowNodeId add_node(FlowNodeType type);

    // Removes a node and all connections touching it.
    bool remove_node(FlowNodeId id);

    FlowNode* get_node(FlowNodeId id) {
        auto it = nodes_.find(id);
        return it == nodes_.end() ? nullptr : &it->second;
    }
    const FlowNode* get_node(FlowNodeId id) const {
        auto it = nodes_.find(id);
        return it == nodes_.end() ? nullptr : &it->second;
    }

    size_t node_count() const { return nodes_.size(); }

    const std::unordered_map<FlowNodeId, FlowNode>& nodes() const {
        return nodes_;
    }

    // --- Connection lifecycle ---

    // Connects from (from_node:from_port) to (to_node:to_port).
    // Validates that both ports exist and types match.
    // For data input ports, replaces any existing connection.
    // Returns the connection id, or kInvalidFlowConnectionId on failure.
    FlowConnectionId connect(FlowNodeId from_node, FlowPortId from_port,
                              FlowNodeId to_node, FlowPortId to_port);

    bool disconnect(FlowConnectionId id);

    // Removes all connections from a specific output port.
    void disconnect_output(FlowNodeId node, FlowPortId port);

    size_t connection_count() const { return connections_.size(); }

    const std::unordered_map<FlowConnectionId, FlowConnection>& connections() const {
        return connections_;
    }

    // --- Topology queries ---

    // Returns all connections originating from (node:port) output.
    std::vector<const FlowConnection*> get_connections_from(
        FlowNodeId node, FlowPortId port) const;

    // Returns the single connection feeding (node:port) input, or nullptr.
    const FlowConnection* get_connection_to(
        FlowNodeId node, FlowPortId port) const;

    // Returns all connections leaving a node (any output port).
    std::vector<const FlowConnection*> get_connections_from_node(
        FlowNodeId node) const;

    // Returns ids of all trigger nodes (entry points for execution).
    std::vector<FlowNodeId> get_trigger_nodes() const;

    // --- Serialization helpers ---

    // Serialize to a compact JSON string for save/load.
    std::string to_json() const;

    // Load from a JSON string. Replaces all existing content.
    // Returns true on success.
    bool from_json(const std::string& json);

    void clear();

private:
    bool validate_connection(FlowNodeId from_node, FlowPortId from_port,
                             FlowNodeId to_node, FlowPortId to_port,
                             FlowPortType& out_type) const;

    std::unordered_map<FlowNodeId, FlowNode> nodes_;
    std::unordered_map<FlowConnectionId, FlowConnection> connections_;

    // Index: (node, port) → list of outgoing connection ids.
    std::unordered_map<uint64_t, std::vector<FlowConnectionId>> out_index_;
    // Index: (node, port) → single incoming connection id.
    std::unordered_map<uint64_t, FlowConnectionId> in_index_;

    FlowNodeId next_node_id_ = 1;
    FlowConnectionId next_conn_id_ = 1;

    static uint64_t port_key(FlowNodeId node, FlowPortId port) {
        return (static_cast<uint64_t>(node) << 8) | static_cast<uint64_t>(port);
    }
};

} // namespace science_and_theology::sfm
