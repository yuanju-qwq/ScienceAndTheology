#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gt/power/power_node.hpp"
#include "pipe_types.hpp"
#include "fluid/fluid_def.hpp"

namespace science_and_theology::gt {

using FluidNodeId = uint32_t;
inline constexpr FluidNodeId kInvalidFluidNodeId = 0;

// ============================================================
// Fluid node — a pipe junction or machine fluid port
// ============================================================

struct FluidNode {
    FluidNodeId id = kInvalidFluidNodeId;
    MapPosition position;

    // Whether this pipe carries liquid or gas. Set at creation, immutable.
    PipeType pipe_type = PipeType::LIQUID;

    // The fluid type carried by this pipe segment.
    // Must be consistent across an entire connected component.
    FluidId fluid_type = kInvalidFluidId;

    // Production from attached machines/pumps (mB per tick).
    int64_t production = 0;

    // Consumption demand from attached machines (mB per tick).
    int64_t demand = 0;

    // Small internal buffer stored in this pipe segment.
    int64_t buffer = 0;
    static constexpr int64_t kPipeBufferSize = 100;

    // Per-tick delivery: how much fluid was pushed to this node by the
    // network during the last update_network() call.
    // Consumers read this to know what they received.
    int64_t delivered = 0;
};

// ============================================================
// Fluid edge — a pipe connecting two nodes
// ============================================================

struct FluidEdge {
    FluidNodeId node_a = kInvalidFluidNodeId;
    FluidNodeId node_b = kInvalidFluidNodeId;

    // Max flow rate through this pipe (mB per tick).
    int64_t max_flow_rate = 0;

    // Current flow computed during update (mB per tick).
    int64_t current_flow = 0;

    // Manhattan distance between nodes (for future loss/pressure).
    int64_t distance_tiles = 0;

    bool connects(FluidNodeId a, FluidNodeId b) const {
        return (node_a == a && node_b == b) || (node_a == b && node_b == a);
    }
};

// ============================================================
// Fluid network — graph of pipes with demand-based flow
// ============================================================
//
// Design principles:
// 1. Node-based pipe junctions, not per-tile pipes.
// 2. One fluid type per connected component (no mixing).
// 3. Demand-driven flow: consumers pull from producers.
// 4. Flow limited by pipe max_flow_rate.
// 5. Updated on topology change, not every tick.

class FluidNetwork {
public:
    FluidNetwork() = default;
    ~FluidNetwork() = default;

    // --- Node lifecycle ---

    // Adds a pipe node at the given position. pipe_type determines whether
    // this node carries liquid (PipeType::LIQUID) or gas (PipeType::GAS).
    // Gas pipes cannot carry liquid fluids, and vice versa.
    FluidNodeId add_node(MapPosition position,
                         PipeType pipe_type = PipeType::LIQUID);
    bool remove_node(FluidNodeId node_id);
    FluidNode* get_node(FluidNodeId node_id);
    const FluidNode* get_node(FluidNodeId node_id) const;
    size_t node_count() const { return nodes_.size(); }

    // --- Edge lifecycle ---

    // Connects two nodes with a pipe of given flow capacity.
    bool connect(FluidNodeId a, FluidNodeId b, int64_t max_flow_rate);
    bool disconnect(FluidNodeId a, FluidNodeId b);
    size_t edge_count() const { return edges_.size(); }

    // --- Fluid type management ---

    // Sets the fluid type for a node. Validates that the connected component
    // does not already have a different fluid type.
    // Returns false if the component already carries a different fluid.
    bool set_node_fluid_type(FluidNodeId node_id, FluidId fluid_type);

    // Returns the fluid type of the connected component containing this node.
    FluidId get_component_fluid_type(FluidNodeId node_id) const;

    // --- Producer / consumer management ---

    void set_producer(FluidNodeId node_id, int64_t amount_per_tick);
    void set_consumer(FluidNodeId node_id, int64_t demand_per_tick);

    // --- Network update ---

    // Recalculates flow across the entire network.
    void update_network();

    // --- Topology queries ---

    std::vector<FluidNodeId> find_connected_component(FluidNodeId start) const;
    std::vector<std::vector<FluidNodeId>> find_all_components() const;
    bool are_connected(FluidNodeId a, FluidNodeId b) const;

    // --- Flow queries ---

    // Returns the net available flow at a node
    // (production - demand within its component).
    int64_t get_available_flow(FluidNodeId node_id) const;

    // Returns total production in the component containing this node.
    int64_t get_component_total_production(FluidNodeId node_id) const;

    // Returns total demand in the component containing this node.
    int64_t get_component_total_demand(FluidNodeId node_id) const;

    // Returns how much fluid was delivered to this node in the last tick.
    int64_t get_delivered(FluidNodeId node_id) const;

    void clear();

private:
    using NodeMap = std::unordered_map<FluidNodeId, FluidNode>;
    using EdgeList = std::list<FluidEdge>;
    using EdgeIterator = EdgeList::iterator;
    using AdjacencyList = std::unordered_map<FluidNodeId,
                                              std::vector<EdgeIterator>>;

    struct ComponentStats {
        FluidId fluid_type = kInvalidFluidId;
        int64_t total_production = 0;
        int64_t total_demand = 0;
    };

    static FluidEdge* edge_ptr(EdgeIterator it) { return &(*it); }
    static const FluidEdge* edge_cptr(EdgeIterator it) { return &(*it); }

    static int64_t make_position_key(MapPosition pos);

    void add_adjacency(FluidNodeId node_id, EdgeIterator edge_it);
    void remove_adjacency(FluidNodeId node_id, EdgeIterator edge_it);

    ComponentStats compute_component_stats(
            const std::vector<FluidNodeId>& component) const;

    // Push-based distribution: production is distributed proportionally
    // to all consumers in the component. Each consumer's `delivered` field
    // is set to the amount of fluid it received this tick.
    void push_production_to_consumers(
            const std::vector<FluidNodeId>& component);

    FluidNodeId next_id_ = 1;
    NodeMap nodes_;
    EdgeList edges_;
    std::unordered_map<int64_t, FluidNodeId> position_index_;
    AdjacencyList adjacency_;
    std::unordered_map<FluidNodeId, int> component_index_;
};

} // namespace science_and_theology::gt