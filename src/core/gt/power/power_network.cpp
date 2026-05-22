#include "power_network.hpp"

#include <algorithm>
#include <cmath>
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

    // Collect neighbor IDs first; disconnect modifies adjacency_ and edges_.
    std::vector<PowerNodeId> neighbors;
    auto adj_it = adjacency_.find(node_id);
    if (adj_it != adjacency_.end()) {
        for (auto edge_it : adj_it->second) {
            PowerEdge* edge = edge_ptr(edge_it);
            PowerNodeId other =
                (edge->node_a == node_id) ? edge->node_b : edge->node_a;
            neighbors.push_back(other);
        }
    }

    for (PowerNodeId other : neighbors) {
        disconnect(node_id, other);
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

    edges_.emplace_back(node_a, node_b, cable, distance);
    auto edge_it = std::prev(edges_.end());
    add_adjacency(node_a, edge_it);
    add_adjacency(node_b, edge_it);

    return true;
}

bool PowerNetwork::disconnect(PowerNodeId node_a, PowerNodeId node_b) {
    for (auto it = edges_.begin(); it != edges_.end(); ++it) {
        if (it->connects(node_a, node_b)) {
            remove_adjacency(node_a, it);
            remove_adjacency(node_b, it);
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
        for (auto edge_it : it->second) {
            result.push_back(edge_cptr(edge_it));
        }
    }
    return result;
}

// --- Adjacency helpers ---

void PowerNetwork::add_adjacency(PowerNodeId node_id, EdgeIterator edge_it) {
    adjacency_[node_id].push_back(edge_it);
}

void PowerNetwork::remove_adjacency(PowerNodeId node_id, EdgeIterator edge_it) {
    auto it = adjacency_.find(node_id);
    if (it == adjacency_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), edge_it), vec.end());
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

        // Transformers isolate networks: don't traverse past them.
        auto current_node_it = nodes_.find(current);
        if (current_node_it != nodes_.end() && current_node_it->second.is_transformer) {
            continue;
        }

        auto adj_it = adjacency_.find(current);
        if (adj_it != adjacency_.end()) {
            for (auto edge_it : adj_it->second) {
                const PowerEdge* edge = edge_cptr(edge_it);
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

    // Phase 1: compute per-component stats (gen, demand, loss, tier).
    std::vector<ComponentStats> comp_stats(components.size());
    for (size_t i = 0; i < components.size(); ++i) {
        comp_stats[i] = compute_component_stats(components[i]);
        total_generation_ += comp_stats[i].total_generation;
        total_demand_ += comp_stats[i].total_demand;
        total_power_loss_ += comp_stats[i].total_loss;
    }

    // Phase 2: transfer surplus power through transformers.
    transfer_transformer_power(components, comp_stats);

    // Phase 3: compute per-edge flows (request-based tree propagation)
    // and check overloads for every component.
    for (size_t i = 0; i < components.size(); ++i) {
        compute_edge_flows(components[i]);

        int64_t supplied_voltage = get_voltage(comp_stats[i].network_tier);
        for (PowerNodeId node_id : components[i]) {
            PowerNode* node = get_node(node_id);
            if (node == nullptr) continue;
            check_node_overload(*node, supplied_voltage);
        }

        check_component_edges(components[i]);
    }
}

// --- Component statistics ---

PowerNetwork::ComponentStats PowerNetwork::compute_component_stats(
        const std::vector<PowerNodeId>& component) const {
    ComponentStats stats;
    std::unordered_set<PowerNodeId> comp_set(component.begin(), component.end());
    std::unordered_set<const PowerEdge*> counted_edges;

    for (PowerNodeId node_id : component) {
        const PowerNode* node = get_node(node_id);
        if (node == nullptr) continue;

        // Transformers don't contribute gen/demand within their own component;
        // they are pass-through devices. Their role is handled in Phase 2.
        if (!node->is_transformer) {
            stats.total_generation += node->generation_capacity;
            stats.total_demand += node->power_demand;
        }

        if (node->generation_capacity > 0 && !node->is_transformer
            && node->tier > stats.network_tier) {
            stats.network_tier = node->tier;
        }

        // Count internal edge loss (both endpoints in this component).
        auto adj_it = adjacency_.find(node_id);
        if (adj_it == adjacency_.end()) continue;

        for (auto edge_it : adj_it->second) {
            const PowerEdge* edge = edge_cptr(edge_it);
            PowerNodeId other = (edge->node_a == node_id) ? edge->node_b
                                                           : edge->node_a;
            if (comp_set.count(other) == 0) continue;  // cross-component edge
            if (counted_edges.count(edge) > 0) continue;
            counted_edges.insert(edge);
            stats.total_loss += edge->power_loss;
        }
    }

    return stats;
}

// --- Transformer power transfer ---

void PowerNetwork::transfer_transformer_power(
        const std::vector<std::vector<PowerNodeId>>& components,
        std::vector<ComponentStats>& comp_stats) {
    // Collect all transformer nodes, sorted by tier descending.
    // Higher-tier transformers are processed first so cascading chains
    // (e.g. HV→MV→LV) propagate surplus correctly.
    struct XfmrEntry {
        PowerNodeId node_id;
        VoltageTier tier;
        VoltageTier output_tier;
        int max_step;
    };
    std::vector<XfmrEntry> transformers;

    for (const auto& [node_id, node] : nodes_) {
        if (node.is_transformer) {
            transformers.push_back(
                {node_id, node.tier, node.transformer_output_tier, node.max_step});
        }
    }

    std::sort(transformers.begin(), transformers.end(),
              [](const XfmrEntry& a, const XfmrEntry& b) {
                  return static_cast<int>(a.tier) > static_cast<int>(b.tier);
              });

    for (const auto& t : transformers) {
        auto comp_it = component_index_.find(t.node_id);
        if (comp_it == component_index_.end()) continue;
        size_t input_comp = static_cast<size_t>(comp_it->second);

        // Validate max_step constraint.
        int tier_diff = std::abs(static_cast<int>(t.tier) -
                                  static_cast<int>(t.output_tier));
        if (tier_diff > t.max_step) continue;

        // Compute transformer operational loss.
        int64_t transformer_loss = tier_diff * PowerNode::kTransformerLossPerStep;

        // Compute input component surplus.
        int64_t surplus = comp_stats[input_comp].total_generation
                        + comp_stats[input_comp].transferred_in
                        - comp_stats[input_comp].total_demand
                        - comp_stats[input_comp].total_loss
                        - comp_stats[input_comp].transferred_out
                        - transformer_loss;

        if (surplus <= 0) continue;

        // Find output components connected through this transformer.
        std::unordered_set<size_t> output_comps;
        auto adj_it = adjacency_.find(t.node_id);
        if (adj_it != adjacency_.end()) {
            for (auto edge_it : adj_it->second) {
                const PowerEdge* edge = edge_cptr(edge_it);
                PowerNodeId neighbor = (edge->node_a == t.node_id)
                                           ? edge->node_b
                                           : edge->node_a;
                auto ncomp_it = component_index_.find(neighbor);
                if (ncomp_it == component_index_.end()) continue;
                size_t ncomp = static_cast<size_t>(ncomp_it->second);
                if (ncomp != input_comp) {
                    output_comps.insert(ncomp);
                }
            }
        }

        if (output_comps.empty()) continue;

        // Distribute surplus evenly across output components.
        int64_t per_comp = surplus / static_cast<int64_t>(output_comps.size());
        if (per_comp <= 0) continue;

        for (size_t out_comp : output_comps) {
            comp_stats[out_comp].total_generation += per_comp;
            comp_stats[out_comp].transferred_in += per_comp;

            if (t.output_tier > comp_stats[out_comp].network_tier) {
                comp_stats[out_comp].network_tier = t.output_tier;
            }
        }

        comp_stats[input_comp].transferred_out +=
            per_comp * static_cast<int64_t>(output_comps.size());

        // Transformer loss counts as additional network-wide loss.
        total_power_loss_ += transformer_loss;
    }
}

// --- Request-based edge flow computation ---

void PowerNetwork::compute_edge_flows(
        const std::vector<PowerNodeId>& component) {
    std::unordered_set<PowerNodeId> comp_set(component.begin(), component.end());
    std::unordered_map<PowerNodeId, PowerNodeId> parent;
    std::unordered_map<PowerNodeId, EdgeIterator> parent_edge;
    std::unordered_map<PowerNodeId, int64_t> subtree_demand;

    // Initialize: every node starts with its own power demand.
    for (PowerNodeId node_id : component) {
        const PowerNode* node = get_node(node_id);
        subtree_demand[node_id] = (node != nullptr) ? node->power_demand : 0;
    }

    // BFS from all generator nodes (multiple roots).
    std::queue<PowerNodeId> bfs_queue;
    std::vector<PowerNodeId> bfs_order;

    for (PowerNodeId node_id : component) {
        const PowerNode* node = get_node(node_id);
        if (node != nullptr && node->generation_capacity > 0
            && !node->is_transformer) {
            bfs_queue.push(node_id);
            parent[node_id] = kInvalidNodeId;  // root marker
        }
    }

    if (bfs_queue.empty()) return;  // no generators, nothing flows

    while (!bfs_queue.empty()) {
        PowerNodeId current = bfs_queue.front();
        bfs_queue.pop();
        bfs_order.push_back(current);

        auto adj_it = adjacency_.find(current);
        if (adj_it == adjacency_.end()) continue;

        for (auto edge_it : adj_it->second) {
            PowerEdge* edge = edge_ptr(edge_it);
            PowerNodeId neighbor = (edge->node_a == current) ? edge->node_b
                                                              : edge->node_a;

            if (parent.count(neighbor) > 0) continue;        // already visited
            if (comp_set.count(neighbor) == 0) continue;      // cross-component

            const PowerNode* neighbor_node = get_node(neighbor);
            if (neighbor_node != nullptr && neighbor_node->is_transformer) {
                continue;  // don't traverse into transformers
            }

            parent[neighbor] = current;
            parent_edge[neighbor] = edge_it;
            bfs_queue.push(neighbor);
        }
    }

    // Reverse propagation: leaves → roots.
    // Each node's demand is added to its parent's subtree total.
    for (auto it = bfs_order.rbegin(); it != bfs_order.rend(); ++it) {
        PowerNodeId node_id = *it;
        auto parent_it = parent.find(node_id);
        if (parent_it == parent.end() || parent_it->second == kInvalidNodeId) {
            continue;
        }

        PowerNodeId parent_id = parent_it->second;
        subtree_demand[parent_id] += subtree_demand[node_id];

        auto edge_wrap = parent_edge.find(node_id);
        if (edge_wrap != parent_edge.end()) {
            PowerEdge* edge = edge_ptr(edge_wrap->second);
            edge->current_load = subtree_demand[node_id];
        }
    }
}

// --- Internal edge overload checking ---

void PowerNetwork::check_component_edges(
        const std::vector<PowerNodeId>& component) {
    std::unordered_set<PowerNodeId> comp_set(component.begin(), component.end());
    std::unordered_set<const PowerEdge*> checked;

    for (PowerNodeId node_id : component) {
        auto adj_it = adjacency_.find(node_id);
        if (adj_it == adjacency_.end()) continue;

        for (auto edge_it : adj_it->second) {
            PowerEdge* edge = edge_ptr(edge_it);
            PowerNodeId other = (edge->node_a == node_id) ? edge->node_b
                                                           : edge->node_a;
            if (comp_set.count(other) == 0) continue;  // cross-component edge
            if (checked.count(edge) > 0) continue;
            checked.insert(edge);

            check_edge_overload(*edge);
        }
    }
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

        // Also notify via general overload callback for the nodes on this edge.
        if (overload_callback_) {
            overload_callback_(edge.node_a, edge.overload_info);
            overload_callback_(edge.node_b, edge.overload_info);
        }
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

    for (auto edge_it : adj_it->second) {
        if (edge_cptr(edge_it)->connects(node_a, node_b)) return true;
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
