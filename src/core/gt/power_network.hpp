#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "power_node.hpp"

namespace science_and_theology::gt {

// Callback type for overload notifications.
// Parameters: node_id, overload_info
using OverloadCallback = std::function<void(PowerNodeId, const OverloadInfo&)>;

// The power network: a graph of PowerNodes connected by PowerEdges.
//
// Design principles (per gt_factorio_power_system_design.md):
// 1. Node-based (pole network), NOT per-tile cables like Minecraft GT.
// 2. Only recalculates on topology changes, not every tick.
// 3. Capacity-based instead of amperage/packet-based.
// 4. Overload detection: over-voltage -> machine explosion,
//    over-capacity -> wire burn.
// 5. Cable material determines capacity and distance-based loss.
class PowerNetwork {
public:
    PowerNetwork() = default;
    ~PowerNetwork() = default;

    // --- Node lifecycle ---

    // Adds a new power node. Returns the assigned node ID.
    // Returns kInvalidNodeId if a node already exists at the given position.
    PowerNodeId add_node(VoltageTier tier, MapPosition position);

    // Removes a node and all its connections. Returns true if the node existed.
    bool remove_node(PowerNodeId node_id);

    // Returns a pointer to the node, or nullptr if not found.
    PowerNode* get_node(PowerNodeId node_id);
    const PowerNode* get_node(PowerNodeId node_id) const;

    // Returns the node ID at a given position, or kInvalidNodeId.
    PowerNodeId get_node_at(MapPosition position) const;

    // Returns the total number of nodes.
    size_t node_count() const { return nodes_.size(); }

    // --- Edge lifecycle ---

    // Connects two nodes with a wire of the given cable material.
    // The distance between nodes is computed automatically from their positions.
    // Returns true if the connection was created.
    bool connect(PowerNodeId node_a, PowerNodeId node_b,
                 const CableProperties& cable);

    // Disconnects two nodes. Returns true if the edge existed.
    bool disconnect(PowerNodeId node_a, PowerNodeId node_b);

    // Returns all edges connected to a given node.
    std::vector<const PowerEdge*> get_edges_for_node(PowerNodeId node_id) const;

    // Returns the total number of edges.
    size_t edge_count() const { return edges_.size(); }

    // --- Network topology ---

    // Recalculates the network state after topology changes.
    // This triggers: connected component discovery, power distribution,
    // voltage propagation, loss calculation, overload detection.
    // Should be called once after a batch of add/remove/connect/disconnect
    // operations, not every tick.
    void update_network();

    // Finds all nodes in the same connected component as the given node.
    // Uses BFS.
    std::vector<PowerNodeId> find_connected_component(
            PowerNodeId start_id) const;

    // Returns all disjoint connected components.
    std::vector<std::vector<PowerNodeId>> find_all_components() const;

    // --- Power state queries ---

    // Sets the power demand (consumption) for a node (typically a machine pole).
    void set_power_demand(PowerNodeId node_id, int64_t demand);

    // Sets the generation capacity for a node (typically a generator pole).
    void set_generation_capacity(PowerNodeId node_id, int64_t capacity);

    // Returns whether the given node is currently overloaded.
    bool is_overloaded(PowerNodeId node_id) const;

    // Returns the overload info for a node.
    OverloadInfo get_overload_info(PowerNodeId node_id) const;

    // Returns the total power loss across the entire network.
    int64_t get_total_power_loss() const { return total_power_loss_; }

    // Returns the total generation across the entire network.
    int64_t get_total_generation() const { return total_generation_; }

    // Returns the total demand across the entire network.
    int64_t get_total_demand() const { return total_demand_; }

    // --- Callbacks ---

    // Sets a callback invoked when a node enters or exits overload.
    void set_overload_callback(OverloadCallback callback);

    // --- Queries ---

    // Returns whether two nodes are directly connected.
    bool are_connected(PowerNodeId node_a, PowerNodeId node_b) const;

    // Returns whether two nodes are in the same connected component
    // (i.e., power can flow between them).
    bool are_in_same_network(PowerNodeId node_a, PowerNodeId node_b) const;

    // Clears all nodes and edges.
    void clear();

private:
    using NodeMap = std::unordered_map<PowerNodeId, PowerNode>;
    using PositionIndex = std::unordered_map<int64_t, PowerNodeId>;
    using AdjacencyList = std::unordered_map<PowerNodeId,
                                              std::vector<PowerEdge*>>;

    // Converts MapPosition to a hashable key for the position index.
    static int64_t make_position_key(MapPosition pos);

    // Adds to the adjacency list.
    void add_adjacency(PowerNodeId node_id, PowerEdge* edge);
    void remove_adjacency(PowerNodeId node_id, PowerEdge* edge);

    // Recalculates power flow for a single connected component.
    void process_component(const std::vector<PowerNodeId>& component);

    // Checks a single node for overload conditions.
    void check_node_overload(PowerNode& node, int64_t supplied_voltage);

    // Checks a single edge for overload conditions (capacity + loss).
    void check_edge_overload(PowerEdge& edge);

    // Resets overload state for all nodes and edges.
    void reset_overloads();

    // Computes total loss for a component and subtracts from available power.
    int64_t compute_component_loss(
            const std::vector<PowerNodeId>& component,
            int64_t total_generation);

    // ID counter for new nodes.
    PowerNodeId next_id_ = 1;

    // Owning storage.
    NodeMap nodes_;
    std::vector<PowerEdge> edges_;

    // Position -> node ID lookup.
    PositionIndex position_index_;

    // Node ID -> list of incident edges for fast traversal.
    AdjacencyList adjacency_;

    // Component cache: node ID -> component index.
    // Rebuilt on each update_network() call.
    std::unordered_map<PowerNodeId, int> component_index_;

    // Network-wide aggregate statistics (updated by update_network()).
    int64_t total_power_loss_ = 0;
    int64_t total_generation_ = 0;
    int64_t total_demand_ = 0;

    // Overload notification callback.
    OverloadCallback overload_callback_;
};

} // namespace science_and_theology::gt