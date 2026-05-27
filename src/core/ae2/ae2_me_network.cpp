#include "ae2_me_network.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace science_and_theology::gt {

// ============================================================
// Node lifecycle
// ============================================================

MENodeId MENetwork::add_node(MENodeType type) {
    MENodeId id = next_id_++;
    nodes_[id] = MENode{id, type};
    node_data_[id] = Impl{};
    return id;
}

bool MENetwork::remove_node(MENodeId id) {
    if (nodes_.erase(id) == 0) return false;

    // Remove all edges involving this node.
    edges_.erase(
        std::remove_if(edges_.begin(), edges_.end(),
            [id](const auto& e) { return e.first == id || e.second == id; }),
        edges_.end());

    node_data_.erase(id);
    rebuild_components();
    return true;
}

MENode* MENetwork::get_node(MENodeId id) {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

const MENode* MENetwork::get_node(MENodeId id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

// ============================================================
// Edge lifecycle
// ============================================================

bool MENetwork::connect(MENodeId a, MENodeId b) {
    if (a == b) return false;
    if (!nodes_.count(a) || !nodes_.count(b)) return false;

    // Check for duplicate.
    for (const auto& e : edges_) {
        if ((e.first == a && e.second == b) ||
            (e.first == b && e.second == a))
            return false;
    }

    edges_.emplace_back(a, b);
    rebuild_components();
    return true;
}

bool MENetwork::disconnect(MENodeId a, MENodeId b) {
    auto it = std::find_if(edges_.begin(), edges_.end(),
        [a, b](const auto& e) {
            return (e.first == a && e.second == b) ||
                   (e.first == b && e.second == a);
        });
    if (it == edges_.end()) return false;
    edges_.erase(it);
    rebuild_components();
    return true;
}

bool MENetwork::are_connected(MENodeId a, MENodeId b) const {
    if (!nodes_.count(a) || !nodes_.count(b)) return false;
    return component_of(a) == component_of(b);
}

// ============================================================
// Storage
// ============================================================

void MENetwork::attach_storage(MENodeId id, std::unique_ptr<IStorage> storage) {
    if (!nodes_.count(id)) return;
    node_data_[id].storages.push_back(std::move(storage));
}

IStorage* MENetwork::detach_storage(MENodeId id, size_t index) {
    auto& storages = node_data_[id].storages;
    if (index >= storages.size()) return nullptr;
    IStorage* ptr = storages[index].get();
    storages.erase(storages.begin() + index);
    return ptr;
}

const std::vector<std::unique_ptr<IStorage>>* MENetwork::node_storages(MENodeId id) const {
    auto it = node_data_.find(id);
    return it != node_data_.end() ? &it->second.storages : nullptr;
}

// ============================================================
// Global query
// ============================================================

int MENetwork::component_of(MENodeId id) const {
    auto it = node_data_.find(id);
    return it != node_data_.end() ? it->second.component_id : -1;
}

const MENetwork::Impl* MENetwork::node_impl(MENodeId id) const {
    auto it = node_data_.find(id);
    return it != node_data_.end() ? &it->second : nullptr;
}

MENetwork::Impl* MENetwork::node_impl(MENodeId id) {
    auto it = node_data_.find(id);
    return it != node_data_.end() ? &it->second : nullptr;
}

std::vector<MENodeId> MENetwork::nodes_in_component(int comp_id) const {
    std::vector<MENodeId> result;
    for (const auto& [id, data] : node_data_) {
        if (data.component_id == comp_id) {
            result.push_back(id);
        }
    }
    return result;
}

int64_t MENetwork::check(const ResourceKey& key, MENodeId context_node) const {
    return check_id(ResourceId::from_key(key), context_node);
}

int64_t MENetwork::check_id(const ResourceId& key, MENodeId context_node) const {
    int comp = component_of(context_node);
    if (comp < 0) return 0;

    int64_t total = 0;
    for (const auto& [id, data] : node_data_) {
        if (data.component_id != comp) continue;
        for (const auto& storage : data.storages) {
            total += storage->available(key);
        }
    }
    return total;
}

int64_t MENetwork::check_global(const ResourceKey& key) const {
    return check_global_id(ResourceId::from_key(key));
}

int64_t MENetwork::check_global_id(const ResourceId& key) const {
    int64_t total = 0;
    for (const auto& [id, data] : node_data_) {
        for (const auto& storage : data.storages) {
            total += storage->available(key);
        }
    }
    return total;
}

int64_t MENetwork::extract(const ResourceKey& key, int64_t amount,
                           MENodeId context_node) {
    return extract_id(ResourceId::from_key(key), amount, context_node);
}

int64_t MENetwork::extract_id(const ResourceId& key, int64_t amount,
                              MENodeId context_node) {
    int comp = component_of(context_node);
    if (comp < 0) return 0;

    int64_t remaining = amount;
    for (auto& [id, data] : node_data_) {
        if (data.component_id != comp) continue;
        for (auto& storage : data.storages) {
            int64_t taken = storage->extract(key, remaining);
            remaining -= taken;
            if (remaining <= 0) break;
        }
        if (remaining <= 0) break;
    }
    return amount - remaining;
}

int64_t MENetwork::extract_global(const ResourceKey& key, int64_t amount,
                                  MENodeId context_node) {
    return extract_global_id(ResourceId::from_key(key), amount, context_node);
}

int64_t MENetwork::extract_global_id(const ResourceId& key, int64_t amount,
                                     MENodeId context_node) {
    // Try context component first, then others.
    int64_t taken = extract_id(key, amount, context_node);
    if (taken >= amount) return taken;

    int64_t remaining = amount - taken;
    for (auto& [id, data] : node_data_) {
        if (data.component_id == component_of(context_node)) continue;
        for (auto& storage : data.storages) {
            int64_t t = storage->extract(key, remaining);
            remaining -= t;
            if (remaining <= 0) break;
        }
        if (remaining <= 0) break;
    }
    return amount - remaining;
}

int64_t MENetwork::insert(const ResourceKey& key, int64_t amount,
                          MENodeId context_node) {
    return insert_id(ResourceId::from_key(key), amount, context_node);
}

int64_t MENetwork::insert_id(const ResourceId& key, int64_t amount,
                             MENodeId context_node) {
    int comp = component_of(context_node);
    if (comp < 0) return amount;

    int64_t overflow = amount;
    for (auto& [id, data] : node_data_) {
        if (data.component_id != comp) continue;
        for (auto& storage : data.storages) {
            overflow = storage->insert(key, overflow);
            if (overflow <= 0) break;
        }
        if (overflow <= 0) break;
    }
    return overflow;
}

int64_t MENetwork::insert_global(const ResourceKey& key, int64_t amount,
                                 MENodeId context_node) {
    return insert_global_id(ResourceId::from_key(key), amount, context_node);
}

int64_t MENetwork::insert_global_id(const ResourceId& key, int64_t amount,
                                    MENodeId context_node) {
    // Try context component first, then others.
    int64_t overflow = insert_id(key, amount, context_node);
    if (overflow <= 0) return 0;

    for (auto& [id, data] : node_data_) {
        if (data.component_id == component_of(context_node)) continue;
        for (auto& storage : data.storages) {
            overflow = storage->insert(key, overflow);
            if (overflow <= 0) break;
        }
        if (overflow <= 0) break;
    }
    return overflow;
}

// ============================================================
// Component rebuilding (BFS)
// ============================================================

void MENetwork::rebuild_components() {
    // Reset all components.
    for (auto& [id, data] : node_data_) {
        data.component_id = -1;
    }

    // Build adjacency list.
    std::unordered_map<MENodeId, std::vector<MENodeId>> adj;
    for (const auto& e : edges_) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }

    int next_comp = 0;
    std::unordered_set<MENodeId> visited;

    for (auto& [id, node] : nodes_) {
        if (visited.count(id)) continue;

        // BFS from this node.
        std::queue<MENodeId> q;
        q.push(id);
        visited.insert(id);
        node_data_[id].component_id = next_comp;

        while (!q.empty()) {
            MENodeId cur = q.front(); q.pop();
            for (MENodeId nb : adj[cur]) {
                if (!visited.count(nb)) {
                    visited.insert(nb);
                    node_data_[nb].component_id = next_comp;
                    q.push(nb);
                }
            }
        }
        next_comp++;
    }
}

std::vector<MENodeId> MENetwork::find_connected_nodes(MENodeId start) const {
    int comp = component_of(start);
    if (comp < 0) return {};
    return nodes_in_component(comp);
}

} // namespace science_and_theology::gt
