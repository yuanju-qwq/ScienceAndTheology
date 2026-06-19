#include "fluid_network.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

#include "fluid/fluid_registry.hpp"

namespace science_and_theology::gt {

// --- Node lifecycle ---

FluidNodeId FluidNetwork::add_node(MapPosition position, PipeType pipe_type) {
    if (position_index_.count(position) > 0) {
        return kInvalidFluidNodeId;
    }

    FluidNodeId node_id = next_id_++;
    FluidNode node;
    node.id = node_id;
    node.position = position;
    node.pipe_type = pipe_type;
    nodes_[node_id] = node;
    position_index_[position] = node_id;
    return node_id;
}

bool FluidNetwork::remove_node(FluidNodeId node_id) {
    auto node_it = nodes_.find(node_id);
    if (node_it == nodes_.end()) {
        return false;
    }

    // Collect neighbor IDs before modifying adjacency.
    std::vector<FluidNodeId> neighbors;
    auto adj_it = adjacency_.find(node_id);
    if (adj_it != adjacency_.end()) {
        for (auto edge_it : adj_it->second) {
            FluidEdge* edge = edge_ptr(edge_it);
            FluidNodeId other =
                (edge->node_a == node_id) ? edge->node_b : edge->node_a;
            neighbors.push_back(other);
        }
    }

    for (FluidNodeId other : neighbors) {
        disconnect(node_id, other);
    }

    position_index_.erase(node_it->second.position);
    nodes_.erase(node_it);
    adjacency_.erase(node_id);
    component_index_.erase(node_id);
    return true;
}

FluidNode* FluidNetwork::get_node(FluidNodeId node_id) {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

const FluidNode* FluidNetwork::get_node(FluidNodeId node_id) const {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

// --- Edge lifecycle ---

bool FluidNetwork::connect(FluidNodeId a, FluidNodeId b,
                            int64_t max_flow_rate) {
    if (a == b) return false;

    const FluidNode* node_a = get_node(a);
    const FluidNode* node_b = get_node(b);
    if (node_a == nullptr || node_b == nullptr) return false;

    // Reject connections between different pipe types (liquid ↔ gas).
    if (node_a->pipe_type != node_b->pipe_type) return false;

    // Reject duplicate connections.
    for (const auto& edge : edges_) {
        if (edge.connects(a, b)) return false;
    }

    int64_t distance = manhattan_distance(
        node_a->position.x, node_a->position.y, node_a->position.z,
        node_b->position.x, node_b->position.y, node_b->position.z);

    edges_.emplace_back();
    FluidEdge& edge = edges_.back();
    edge.node_a = a;
    edge.node_b = b;
    edge.max_flow_rate = max_flow_rate;
    edge.distance_tiles = distance;

    auto edge_it = std::prev(edges_.end());
    add_adjacency(a, edge_it);
    add_adjacency(b, edge_it);

    return true;
}

bool FluidNetwork::disconnect(FluidNodeId a, FluidNodeId b) {
    for (auto it = edges_.begin(); it != edges_.end(); ++it) {
        if (it->connects(a, b)) {
            remove_adjacency(a, it);
            remove_adjacency(b, it);
            edges_.erase(it);
            return true;
        }
    }
    return false;
}

// --- Adjacency helpers ---

void FluidNetwork::add_adjacency(FluidNodeId node_id, EdgeIterator edge_it) {
    adjacency_[node_id].push_back(edge_it);
}

void FluidNetwork::remove_adjacency(FluidNodeId node_id,
                                     EdgeIterator edge_it) {
    auto it = adjacency_.find(node_id);
    if (it == adjacency_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), edge_it), vec.end());
    if (vec.empty()) {
        adjacency_.erase(it);
    }
}

// --- Fluid type management ---

bool FluidNetwork::set_node_fluid_type(FluidNodeId node_id,
                                        FluidId fluid_type) {
    FluidNode* node = get_node(node_id);
    if (node == nullptr) return false;

    // Validate: gas pipes cannot carry liquid, liquid pipes cannot carry gas.
    const FluidDefinition* def = FluidRegistry::get_fluid(fluid_type);
    if (def == nullptr) return false;  // unknown fluid ID

    PipeType required_type = def->is_gas ? PipeType::GAS : PipeType::LIQUID;
    if (node->pipe_type != required_type) {
        return false;  // pipe type mismatch
    }

    // Check if the connected component already has a different fluid.
    auto component = find_connected_component(node_id);
    for (FluidNodeId other_id : component) {
        const FluidNode* other = get_node(other_id);
        if (other == nullptr) continue;
        if (other->fluid_type != kInvalidFluidId &&
            other->fluid_type != fluid_type) {
            return false;  // component already carries a different fluid
        }
    }

    // Propagate type to all nodes in the component.
    for (FluidNodeId other_id : component) {
        FluidNode* other = get_node(other_id);
        if (other != nullptr) {
            other->fluid_type = fluid_type;
        }
    }

    return true;
}

FluidId FluidNetwork::get_component_fluid_type(FluidNodeId node_id) const {
    auto component = find_connected_component(node_id);
    if (component.empty()) return kInvalidFluidId;

    // All nodes in the component should have the same fluid type.
    const FluidNode* first = get_node(component[0]);
    return (first != nullptr) ? first->fluid_type : kInvalidFluidId;
}

// --- Producer / consumer management ---

void FluidNetwork::set_producer(FluidNodeId node_id,
                                 int64_t amount_per_tick) {
    FluidNode* node = get_node(node_id);
    if (node != nullptr) {
        node->production = amount_per_tick;
    }
}

void FluidNetwork::set_consumer(FluidNodeId node_id,
                                 int64_t demand_per_tick) {
    FluidNode* node = get_node(node_id);
    if (node != nullptr) {
        node->demand = demand_per_tick;
    }
}

// --- Network topology ---

std::vector<FluidNodeId> FluidNetwork::find_connected_component(
        FluidNodeId start_id) const {
    std::vector<FluidNodeId> component;
    if (nodes_.count(start_id) == 0) return component;

    std::unordered_set<FluidNodeId> visited;
    std::queue<FluidNodeId> bfs_queue;

    visited.insert(start_id);
    bfs_queue.push(start_id);

    while (!bfs_queue.empty()) {
        FluidNodeId current = bfs_queue.front();
        bfs_queue.pop();
        component.push_back(current);

        auto adj_it = adjacency_.find(current);
        if (adj_it != adjacency_.end()) {
            for (auto edge_it : adj_it->second) {
                const FluidEdge* edge = edge_cptr(edge_it);
                FluidNodeId neighbor = (edge->node_a == current)
                                           ? edge->node_b
                                           : edge->node_a;
                if (visited.count(neighbor) == 0) {
                    visited.insert(neighbor);
                    bfs_queue.push(neighbor);
                }
            }
        }
    }

    return component;
}

std::vector<std::vector<FluidNodeId>> FluidNetwork::find_all_components() const {
    std::vector<std::vector<FluidNodeId>> components;
    std::unordered_set<FluidNodeId> visited;

    for (const auto& [node_id, _] : nodes_) {
        if (visited.count(node_id) > 0) continue;
        auto component = find_connected_component(node_id);
        for (FluidNodeId id : component) {
            visited.insert(id);
        }
        components.push_back(std::move(component));
    }

    return components;
}

bool FluidNetwork::are_connected(FluidNodeId a, FluidNodeId b) const {
    if (a == b) return false;

    auto adj_it = adjacency_.find(a);
    if (adj_it == adjacency_.end()) return false;

    for (auto edge_it : adj_it->second) {
        if (edge_cptr(edge_it)->connects(a, b)) return true;
    }
    return false;
}

// --- Network update ---

void FluidNetwork::update_network() {
    auto components = find_all_components();

    component_index_.clear();
    for (size_t comp_idx = 0; comp_idx < components.size(); ++comp_idx) {
        for (FluidNodeId node_id : components[comp_idx]) {
            component_index_[node_id] = static_cast<int>(comp_idx);
        }
    }

    // For each component, distribute production to consumers.
    for (const auto& component : components) {
        push_production_to_consumers(component);
    }
}

// --- Component statistics ---

FluidNetwork::ComponentStats FluidNetwork::compute_component_stats(
        const std::vector<FluidNodeId>& component) const {
    ComponentStats stats;

    for (FluidNodeId node_id : component) {
        const FluidNode* node = get_node(node_id);
        if (node == nullptr) continue;

        stats.total_production += node->production;
        stats.total_demand += node->demand;

        if (node->fluid_type != kInvalidFluidId) {
            stats.fluid_type = node->fluid_type;
        }
    }

    return stats;
}

// ============================================================
// Push-based production distribution
// ============================================================

void FluidNetwork::push_production_to_consumers(
        const std::vector<FluidNodeId>& component) {
    int64_t total_production = 0;
    int64_t total_demand = 0;
    std::vector<FluidNode*> consumer_nodes;

    for (FluidNodeId node_id : component) {
        FluidNode* node = get_node(node_id);
        if (node == nullptr) continue;

        node->delivered = 0;
        total_production += node->production;

        if (node->demand > 0) {
            total_demand += node->demand;
            consumer_nodes.push_back(node);
        }
    }

    if (total_production <= 0 || total_demand <= 0) return;

    int64_t available = (total_production < total_demand)
                        ? total_production : total_demand;
    int64_t remaining = available;

    for (size_t i = 0; i < consumer_nodes.size() && remaining > 0; ++i) {
        FluidNode* consumer = consumer_nodes[i];

        int64_t share;
        if (i == consumer_nodes.size() - 1) {
            share = remaining;
        } else {
            share = (consumer->demand * available) / total_demand;
            if (share > remaining) share = remaining;
            if (share > consumer->demand) share = consumer->demand;
        }

        consumer->delivered = share;
        remaining -= share;
    }
}

// --- Flow queries ---

int64_t FluidNetwork::get_available_flow(FluidNodeId node_id) const {
    auto component = find_connected_component(node_id);
    auto stats = compute_component_stats(component);
    int64_t net = stats.total_production - stats.total_demand;
    return (net > 0) ? net : 0;
}

int64_t FluidNetwork::get_component_total_production(
        FluidNodeId node_id) const {
    auto component = find_connected_component(node_id);
    auto stats = compute_component_stats(component);
    return stats.total_production;
}

int64_t FluidNetwork::get_component_total_demand(
        FluidNodeId node_id) const {
    auto component = find_connected_component(node_id);
    auto stats = compute_component_stats(component);
    return stats.total_demand;
}

int64_t FluidNetwork::get_delivered(FluidNodeId node_id) const {
    const FluidNode* node = get_node(node_id);
    return (node != nullptr) ? node->delivered : 0;
}

void FluidNetwork::clear() {
    nodes_.clear();
    edges_.clear();
    position_index_.clear();
    adjacency_.clear();
    component_index_.clear();
    next_id_ = 1;
}

} // namespace science_and_theology::gt