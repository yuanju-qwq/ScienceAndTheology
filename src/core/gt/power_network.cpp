#include "power_network.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace science_and_theology::gt {

// --- Position key ---

int64_t PowerNetwork::make_position_key(MapPosition pos) {
    return (static_cast<int64_t>(pos.x) << 32) |
           (static_cast<int64_t>(pos.y) & 0xFFFFFFFFLL);
}

// --- Node lifecycle ---

PowerNodeId PowerNetwork::add_node(VoltageTier tier, MapPosition position) {
    int64_t pos_key = make_position_key(position);
    if (position_index_.count(pos_key) > 0) {
        return kInvalidNodeId;
    }

    PowerNodeId node_id = next_id_++;
    PowerNode node(node_id, tier, position);
    nodes_[node_id] = node;
    position_index_[pos_key] = node_id;
    return node_id;
}

bool PowerNetwork::remove_node(PowerNodeId node_id) {
    auto node_it = nodes_.find(node_id);
    if (node_it == nodes_.end()) {
        return false;
    }

    auto adj_it = adjacency_.find(node_id);
    if (adj_it != adjacency_.end()) {
        auto edge_ptrs = adj_it->second;
        for (PowerEdge* edge : edge_ptrs) {
            PowerNodeId other =
                (edge->node_a == node_id) ? edge->node_b : edge->node_a;
            disconnect(node_id, other);
        }
    }

    int64_t pos_key = make_position_key(node_it->second.position);
    position_index_.erase(pos_key);

    nodes_.erase(node_it);
    adjacency_.erase(node_id);
    component_index_.erase(node_id);
    return true;
}

PowerNode* PowerNetwork::get_node(PowerNodeId node_id) {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

const PowerNode* PowerNetwork::get_node(PowerNodeId node_id) const {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

PowerNodeId PowerNetwork::get_node_at(MapPosition position) const {
    int64_t pos_key = make_position_key(position);
    auto it = position_index_.find(pos_key);
    return (it != position_index_.end()) ? it->second : kInvalidNodeId;
}

// --- Edge lifecycle ---

bool PowerNetwork::connect(PowerNodeId node_a, PowerNodeId node_b,
                           const CableProperties& cable) {
    if (node_a == node_b) {
        return false;
    }

    const PowerNode* node_a_ptr = get_node(node_a);
    const PowerNode* node_b_ptr = get_node(node_b);
    if (node_a_ptr == nullptr || node_b_ptr == nullptr) {
        return false;
    }

    for (const auto& edge : edges_) {
        if (edge.connects(node_a, node_b)) {
            return false;
        }
    }

    int64_t distance = manhattan_distance(
        node_a_ptr->position.x, node_a_ptr->position.y,
        node_b_ptr->position.x, node_b_ptr->position.y);

    PowerEdge edge(node_a, node_b, cable, distance);
    edges_.push_back(edge);

    PowerEdge* edge_ptr = &edges_.back();
    add_adjacency(node_a, edge_ptr);
    add_adjacency(node_b, edge_ptr);

    return true;
}

bool PowerNetwork::disconnect(PowerNodeId node_a, PowerNodeId node_b) {
    for (auto it = edges_.begin(); it != edges_.end(); ++it) {
        if (it->connects(node_a, node_b)) {
            remove_adjacency(node_a, &(*it));
            remove_adjacency(node_b, &(*it));
            edges_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<const PowerEdge*> PowerNetwork::get_edges_for_node(
        PowerNodeId node_id) const {
    std::vector<const PowerEdge*> result;
    auto it = adjacency_.find(node_id);
    if (it != adjacency_.end()) {
        for (const PowerEdge* edge : it->second) {
            result.push_back(edge);
        }
    }
    return result;
}

// --- Adjacency helpers ---

void PowerNetwork::add_adjacency(PowerNodeId node_id, PowerEdge* edge) {
    adjacency_[node_id].push_back(edge);
}

void PowerNetwork::remove_adjacency(PowerNodeId node_id, PowerEdge* edge) {
    auto it = adjacency_.find(node_id);
    if (it == adjacency_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), edge), vec.end());
    if (vec.empty()) {
        adjacency_.erase(it);
    }
}

// --- Network topology ---

std::vector<PowerNodeId> PowerNetwork::find_connected_component(
        PowerNodeId start_id) const {
    std::vector<PowerNodeId> component;
    if (nodes_.count(start_id) == 0) {
        return component;
    }

    std::unordered_set<PowerNodeId> visited;
    std::queue<PowerNodeId> bfs_queue;

    visited.insert(start_id);
    bfs_queue.push(start_id);

    while (!bfs_queue.empty()) {
        PowerNodeId current = bfs_queue.front();
        bfs_queue.pop();
        component.push_back(current);

        auto adj_it = adjacency_.find(current);
        if (adj_it != adjacency_.end()) {
            for (const PowerEdge* edge : adj_it->second) {
                PowerNodeId neighbor =
                    (edge->node_a == current) ? edge->node_b : edge->node_a;
                if (visited.count(neighbor) == 0) {
                    visited.insert(neighbor);
                    bfs_queue.push(neighbor);
                }
            }
        }
    }

    return component;
}

std::vector<std::vector<PowerNodeId>> PowerNetwork::find_all_components() const {
    std::vector<std::vector<PowerNodeId>> components;
    std::unordered_set<PowerNodeId> visited;

    for (const auto& [node_id, _] : nodes_) {
        if (visited.count(node_id) > 0) {
            continue;
        }

        auto component = find_connected_component(node_id);
        for (PowerNodeId id : component) {
            visited.insert(id);
        }
        components.push_back(std::move(component));
    }

    return components;
}

void PowerNetwork::update_network() {
    auto components = find_all_components();

    component_index_.clear();
    for (size_t comp_idx = 0; comp_idx < components.size(); ++comp_idx) {
        for (PowerNodeId node_id : components[comp_idx]) {
            component_index_[node_id] = static_cast<int>(comp_idx);
        }
    }

    reset_overloads();
    total_power_loss_ = 0;
    total_generation_ = 0;
    total_demand_ = 0;

    for (const auto& component : components) {
        process_component(component);
    }
}

// --- Power flow processing ---

void PowerNetwork::process_component(
        const std::vector<PowerNodeId>& component) {
    // Determine the dominant voltage tier (highest generation tier).
    VoltageTier network_tier = VoltageTier::ULV;
    int64_t component_generation = 0;
    int64_t component_demand = 0;

    for (PowerNodeId node_id : component) {
        PowerNode* node = get_node(node_id);
        if (node == nullptr) continue;

        component_generation += node->generation_capacity;
        component_demand += node->power_demand;
        total_generation_ += node->generation_capacity;
        total_demand_ += node->power_demand;

        if (node->generation_capacity > 0 && node->tier > network_tier) {
            network_tier = node->tier;
        }
    }

    // Compute total loss across all edges in this component.
    int64_t component_loss = compute_component_loss(component,
                                                     component_generation);
    total_power_loss_ += component_loss;

    int64_t effective_power = component_generation - component_loss;
    if (effective_power < 0) {
        effective_power = 0;
    }

    int64_t supplied_voltage = get_voltage(network_tier);

    // Check node overloads.
    for (PowerNodeId node_id : component) {
        PowerNode* node = get_node(node_id);
        if (node == nullptr) continue;

        check_node_overload(*node, supplied_voltage);
    }

    // Check edge overloads.
    // Approximate: each edge carries the demand of the far-side sub-tree.
    // For minimal viable, sum the demand from neighboring edges.
    for (PowerNodeId node_id : component) {
        auto adj_it = adjacency_.find(node_id);
        if (adj_it == adjacency_.end()) continue;

        for (PowerEdge* edge : adj_it->second) {
            edge->current_load += effective_power /
                std::max<size_t>(adj_it->second.size(), 1);
            check_edge_overload(*edge);
        }
    }
}

int64_t PowerNetwork::compute_component_loss(
        const std::vector<PowerNodeId>& component,
        int64_t total_generation) {
    (void)total_generation;

    int64_t total_loss = 0;
    std::unordered_set<const PowerEdge*> visited_edges;

    for (PowerNodeId node_id : component) {
        auto adj_it = adjacency_.find(node_id);
        if (adj_it == adjacency_.end()) continue;

        for (PowerEdge* edge : adj_it->second) {
            if (visited_edges.count(edge) > 0) continue;
            visited_edges.insert(edge);

            total_loss += edge->power_loss;
        }
    }

    return total_loss;
}

// --- Overload detection ---

void PowerNetwork::check_node_overload(PowerNode& node,
                                       int64_t supplied_voltage) {
    node.overload_info = OverloadInfo{};
    node.overload_info.actual_voltage = supplied_voltage;
    node.overload_info.max_voltage = node.max_input_voltage;

    // Over-voltage: machine receives voltage above its max rating.
    if (node.power_demand > 0 &&
        supplied_voltage > node.max_input_voltage) {
        node.overload_info.state = OverloadState::OVER_VOLTAGE;
        node.overload_info.actual_load = supplied_voltage;
        node.overload_info.max_capacity = node.max_input_voltage;

        if (overload_callback_) {
            overload_callback_(node.id, node.overload_info);
        }
    }
}

void PowerNetwork::check_edge_overload(PowerEdge& edge) {
    edge.overload_info = OverloadInfo{};
    edge.overload_info.actual_load = edge.current_load;
    edge.overload_info.max_capacity = edge.max_capacity;

    // Over-capacity: wire carries more power than its material rating.
    if (edge.current_load > edge.max_capacity) {
        edge.overload_info.state = OverloadState::OVER_CAPACITY;
    }
}

// --- Overload reset ---

void PowerNetwork::reset_overloads() {
    for (auto& [_, node] : nodes_) {
        node.overload_info = OverloadInfo{};
    }
    for (auto& edge : edges_) {
        edge.current_load = 0;
        edge.overload_info = OverloadInfo{};
    }
}

// --- State queries ---

void PowerNetwork::set_power_demand(PowerNodeId node_id, int64_t demand) {
    PowerNode* node = get_node(node_id);
    if (node != nullptr) {
        node->power_demand = demand;
    }
}

void PowerNetwork::set_generation_capacity(PowerNodeId node_id,
                                            int64_t capacity) {
    PowerNode* node = get_node(node_id);
    if (node != nullptr) {
        node->generation_capacity = capacity;
    }
}

bool PowerNetwork::is_overloaded(PowerNodeId node_id) const {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;
    return it->second.overload_info.state != OverloadState::OK;
}

OverloadInfo PowerNetwork::get_overload_info(PowerNodeId node_id) const {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return OverloadInfo{};
    return it->second.overload_info;
}

void PowerNetwork::set_overload_callback(OverloadCallback callback) {
    overload_callback_ = std::move(callback);
}

bool PowerNetwork::are_connected(PowerNodeId node_a,
                                  PowerNodeId node_b) const {
    if (node_a == node_b) return false;

    auto adj_it = adjacency_.find(node_a);
    if (adj_it == adjacency_.end()) return false;

    for (const PowerEdge* edge : adj_it->second) {
        if (edge->connects(node_a, node_b)) return true;
    }
    return false;
}

bool PowerNetwork::are_in_same_network(PowerNodeId node_a,
                                       PowerNodeId node_b) const {
    if (node_a == node_b) return true;

    auto it_a = component_index_.find(node_a);
    auto it_b = component_index_.find(node_b);

    if (it_a == component_index_.end() || it_b == component_index_.end()) {
        return false;
    }

    return it_a->second == it_b->second;
}

void PowerNetwork::clear() {
    nodes_.clear();
    edges_.clear();
    position_index_.clear();
    adjacency_.clear();
    component_index_.clear();
    next_id_ = 1;
    total_power_loss_ = 0;
    total_generation_ = 0;
    total_demand_ = 0;
}

} // namespace science_and_theology::gt