#include "item_pipe_network.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace science_and_theology::gt {

// --- Position key ---

int64_t ItemPipeNetwork::make_position_key(MapPosition pos) {
    return (static_cast<int64_t>(pos.x) << 32) |
           (static_cast<int64_t>(pos.y) & 0xFFFFFFFFLL);
}

// --- Node lifecycle ---

ItemPipeNodeId ItemPipeNetwork::add_node(MapPosition position) {
    int64_t pos_key = make_position_key(position);
    if (position_index_.count(pos_key) > 0) {
        return kInvalidItemPipeNodeId;
    }

    ItemPipeNodeId node_id = next_id_++;
    ItemPipeNode node;
    node.id = node_id;
    node.position = position;
    nodes_[node_id] = node;
    position_index_[pos_key] = node_id;
    return node_id;
}

bool ItemPipeNetwork::remove_node(ItemPipeNodeId node_id) {
    auto node_it = nodes_.find(node_id);
    if (node_it == nodes_.end()) return false;

    std::vector<ItemPipeNodeId> neighbors;
    auto adj_it = adjacency_.find(node_id);
    if (adj_it != adjacency_.end()) {
        for (auto edge_it : adj_it->second) {
            ItemPipeEdge* edge = edge_ptr(edge_it);
            ItemPipeNodeId other =
                (edge->node_a == node_id) ? edge->node_b : edge->node_a;
            neighbors.push_back(other);
        }
    }

    for (ItemPipeNodeId other : neighbors) {
        disconnect(node_id, other);
    }

    int64_t pos_key = make_position_key(node_it->second.position);
    position_index_.erase(pos_key);
    nodes_.erase(node_it);
    adjacency_.erase(node_id);

    // Remove this node from its component index and clear its buffer share.
    auto comp_it = component_index_.find(node_id);
    if (comp_it != component_index_.end()) {
        component_index_.erase(comp_it);
    }

    return true;
}

ItemPipeNode* ItemPipeNetwork::get_node(ItemPipeNodeId node_id) {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

const ItemPipeNode* ItemPipeNetwork::get_node(
        ItemPipeNodeId node_id) const {
    auto it = nodes_.find(node_id);
    return (it != nodes_.end()) ? &it->second : nullptr;
}

// --- Edge lifecycle ---

bool ItemPipeNetwork::connect(ItemPipeNodeId a, ItemPipeNodeId b,
                               int64_t max_items_per_tick) {
    if (a == b) return false;

    const ItemPipeNode* node_a = get_node(a);
    const ItemPipeNode* node_b = get_node(b);
    if (node_a == nullptr || node_b == nullptr) return false;

    for (const auto& edge : edges_) {
        if (edge.connects(a, b)) return false;
    }

    int64_t distance = manhattan_distance(
        node_a->position.x, node_a->position.y,
        node_b->position.x, node_b->position.y);

    edges_.emplace_back();
    ItemPipeEdge& edge = edges_.back();
    edge.node_a = a;
    edge.node_b = b;
    edge.max_items_per_tick = max_items_per_tick;
    edge.distance_tiles = distance;

    auto edge_it = std::prev(edges_.end());
    add_adjacency(a, edge_it);
    add_adjacency(b, edge_it);

    return true;
}

bool ItemPipeNetwork::disconnect(ItemPipeNodeId a, ItemPipeNodeId b) {
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

void ItemPipeNetwork::add_adjacency(ItemPipeNodeId node_id,
                                     EdgeIterator edge_it) {
    adjacency_[node_id].push_back(edge_it);
}

void ItemPipeNetwork::remove_adjacency(ItemPipeNodeId node_id,
                                        EdgeIterator edge_it) {
    auto it = adjacency_.find(node_id);
    if (it == adjacency_.end()) return;

    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), edge_it), vec.end());
    if (vec.empty()) {
        adjacency_.erase(it);
    }
}

// --- Item insertion / extraction ---

int64_t ItemPipeNetwork::insert(ItemPipeNodeId node_id, uint16_t item_id,
                                 int64_t count) {
    if (count <= 0) return 0;

    const ItemPipeNode* node = get_node(node_id);
    if (node == nullptr) return 0;

    auto comp_it = component_index_.find(node_id);
    if (comp_it == component_index_.end()) return 0;

    int comp_id = comp_it->second;
    auto& buffer = component_buffers_[comp_id];

    // Cap the total distinct types in the buffer.
    if (buffer.count(item_id) == 0 &&
        static_cast<int>(buffer.size()) >= kMaxItemTypesInPipe) {
        return 0;  // buffer full with too many item types
    }

    int64_t current = buffer[item_id];
    // No individual cap — items accumulate. Type cap above prevents abuse.
    int64_t new_total = current + count;
    buffer[item_id] = new_total;
    return count;
}

int64_t ItemPipeNetwork::extract(ItemPipeNodeId node_id, uint16_t item_id,
                                  int64_t requested) {
    if (requested <= 0) return 0;

    const ItemPipeNode* node = get_node(node_id);
    if (node == nullptr) return 0;

    auto comp_it = component_index_.find(node_id);
    if (comp_it == component_index_.end()) return 0;

    int comp_id = comp_it->second;
    auto& buffer = component_buffers_[comp_id];

    auto item_it = buffer.find(item_id);
    if (item_it == buffer.end()) return 0;

    int64_t available = item_it->second;
    int64_t extracted = (requested < available) ? requested : available;
    item_it->second -= extracted;
    if (item_it->second <= 0) {
        buffer.erase(item_it);
    }
    return extracted;
}

int64_t ItemPipeNetwork::count_item(ItemPipeNodeId node_id,
                                     uint16_t item_id) const {
    const ItemPipeNode* node = get_node(node_id);
    if (node == nullptr) return 0;

    auto comp_it = component_index_.find(node_id);
    if (comp_it == component_index_.end()) return 0;

    const auto& buffer = component_buffers_.find(comp_it->second);
    if (buffer == component_buffers_.end()) return 0;

    auto item_it = buffer->second.find(item_id);
    return (item_it != buffer->second.end()) ? item_it->second : 0;
}

int64_t ItemPipeNetwork::count_total_items(ItemPipeNodeId node_id) const {
    const ItemPipeNode* node = get_node(node_id);
    if (node == nullptr) return 0;

    auto comp_it = component_index_.find(node_id);
    if (comp_it == component_index_.end()) return 0;

    const auto& buffer = component_buffers_.find(comp_it->second);
    if (buffer == component_buffers_.end()) return 0;

    int64_t total = 0;
    for (const auto& [_, count] : buffer->second) {
        total += count;
    }
    return total;
}

// --- Network topology ---

std::vector<ItemPipeNodeId> ItemPipeNetwork::find_connected_component(
        ItemPipeNodeId start_id) const {
    std::vector<ItemPipeNodeId> component;
    if (nodes_.count(start_id) == 0) return component;

    std::unordered_set<ItemPipeNodeId> visited;
    std::queue<ItemPipeNodeId> bfs_queue;

    visited.insert(start_id);
    bfs_queue.push(start_id);

    while (!bfs_queue.empty()) {
        ItemPipeNodeId current = bfs_queue.front();
        bfs_queue.pop();
        component.push_back(current);

        auto adj_it = adjacency_.find(current);
        if (adj_it != adjacency_.end()) {
            for (auto edge_it : adj_it->second) {
                const ItemPipeEdge* edge = edge_cptr(edge_it);
                ItemPipeNodeId neighbor = (edge->node_a == current)
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

std::vector<std::vector<ItemPipeNodeId>>
ItemPipeNetwork::find_all_components() const {
    std::vector<std::vector<ItemPipeNodeId>> components;
    std::unordered_set<ItemPipeNodeId> visited;

    for (const auto& [node_id, _] : nodes_) {
        if (visited.count(node_id) > 0) continue;
        auto component = find_connected_component(node_id);
        for (ItemPipeNodeId id : component) {
            visited.insert(id);
        }
        components.push_back(std::move(component));
    }

    return components;
}

bool ItemPipeNetwork::are_connected(ItemPipeNodeId a,
                                     ItemPipeNodeId b) const {
    if (a == b) return false;

    auto adj_it = adjacency_.find(a);
    if (adj_it == adjacency_.end()) return false;

    for (auto edge_it : adj_it->second) {
        if (edge_cptr(edge_it)->connects(a, b)) return true;
    }
    return false;
}

// --- Network update ---

void ItemPipeNetwork::update_network() {
    auto components = find_all_components();

    // Build new component_index.
    std::unordered_map<ItemPipeNodeId, int> new_component_index;
    for (size_t comp_idx = 0; comp_idx < components.size(); ++comp_idx) {
        for (ItemPipeNodeId node_id : components[comp_idx]) {
            new_component_index[node_id] = static_cast<int>(comp_idx);
        }
    }

    // Migrate item buffers from old component IDs to new ones.
    // When components merge, their buffers merge.
    // When a component splits, items stay with the first new component.
    std::unordered_map<int, std::unordered_map<uint16_t, int64_t>>
            new_buffers;

    // Map old comp_id → new comp_id for each node.
    std::unordered_map<int, int> old_to_new;
    for (const auto& [node_id, old_comp] : component_index_) {
        auto new_it = new_component_index.find(node_id);
        if (new_it == new_component_index.end()) continue;
        old_to_new[old_comp] = new_it->second;
    }

    // Move buffers to new component IDs.
    for (const auto& [old_comp, items] : component_buffers_) {
        int new_comp = old_comp;  // default: keep old ID
        auto map_it = old_to_new.find(old_comp);
        if (map_it != old_to_new.end()) {
            new_comp = map_it->second;
        }
        auto& target = new_buffers[new_comp];
        for (const auto& [item_id, count] : items) {
            target[item_id] += count;
        }
    }

    component_index_ = std::move(new_component_index);
    component_buffers_ = std::move(new_buffers);
}

// --- Source / sink ---

void ItemPipeNetwork::set_source(ItemPipeNodeId node_id, bool is_source) {
    ItemPipeNode* node = get_node(node_id);
    if (node != nullptr) {
        node->is_source = is_source;
    }
}

void ItemPipeNetwork::set_sink(ItemPipeNodeId node_id, bool is_sink) {
    ItemPipeNode* node = get_node(node_id);
    if (node != nullptr) {
        node->is_sink = is_sink;
    }
}

void ItemPipeNetwork::clear() {
    nodes_.clear();
    edges_.clear();
    position_index_.clear();
    adjacency_.clear();
    component_index_.clear();
    component_buffers_.clear();
    next_id_ = 1;
}

} // namespace science_and_theology::gt