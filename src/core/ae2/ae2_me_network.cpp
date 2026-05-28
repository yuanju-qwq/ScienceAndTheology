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

    edges_.erase(
        std::remove_if(edges_.begin(), edges_.end(),
            [id](const auto& e) { return e.first == id || e.second == id; }),
        edges_.end());

    node_data_.erase(id);
    channel_providers_.erase(id);
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

    // If two Controllers now share a component, reject the connection.
    int comp = component_of(a);
    if (comp >= 0) {
        int controller_count = 0;
        for (MENodeId id : nodes_in_component(comp)) {
            if (nodes_[id].type == MENodeType::Controller)
                controller_count++;
        }
        if (controller_count > 1) {
            // Revert: remove the edge and rebuild.
            edges_.pop_back();
            rebuild_components();
            return false;
        }
    }

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
// Helpers
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

bool MENetwork::is_device_type(MENodeType type) const {
    return type == MENodeType::Drive ||
           type == MENodeType::StorageBus ||
           type == MENodeType::Interface ||
           type == MENodeType::Terminal;
}

// ============================================================
// Query
// ============================================================

int64_t MENetwork::check(const ResourceKey& key, MENodeId context_node) const {
    return check_id(ResourceId::from_key(key), context_node);
}

int64_t MENetwork::check_id(const ResourceId& key, MENodeId context_node) const {
    int comp = component_of(context_node);
    if (comp < 0) return 0;

    int64_t total = 0;
    for (const auto& [id, data] : node_data_) {
        if (data.component_id != comp) continue;
        if (!nodes_.at(id).online) continue;
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
        if (!nodes_.at(id).online) continue;
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
        if (!nodes_[id].online) continue;
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
    int64_t taken = extract_id(key, amount, context_node);
    if (taken >= amount) return taken;

    int64_t remaining = amount - taken;
    for (auto& [id, data] : node_data_) {
        if (data.component_id == component_of(context_node)) continue;
        if (!nodes_[id].online) continue;
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
        if (!nodes_[id].online) continue;
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
    int64_t overflow = insert_id(key, amount, context_node);
    if (overflow <= 0) return 0;

    for (auto& [id, data] : node_data_) {
        if (data.component_id == component_of(context_node)) continue;
        if (!nodes_[id].online) continue;
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
    for (auto& [id, data] : node_data_) {
        data.component_id = -1;
    }

    adjacency_.clear();
    for (const auto& e : edges_) {
        adjacency_[e.first].push_back(e.second);
        adjacency_[e.second].push_back(e.first);
    }

    int next_comp = 0;
    std::unordered_set<MENodeId> visited;

    for (auto& [id, node] : nodes_) {
        if (visited.count(id)) continue;

        std::queue<MENodeId> q;
        q.push(id);
        visited.insert(id);
        node_data_[id].component_id = next_comp;

        while (!q.empty()) {
            MENodeId cur = q.front(); q.pop();
            for (MENodeId nb : adjacency_[cur]) {
                if (!visited.count(nb)) {
                    visited.insert(nb);
                    node_data_[nb].component_id = next_comp;
                    q.push(nb);
                }
            }
        }
        next_comp++;
    }

    allocate_channels();
}

std::vector<MENodeId> MENetwork::find_connected_nodes(MENodeId start) const {
    int comp = component_of(start);
    if (comp < 0) return {};
    return nodes_in_component(comp);
}

// ============================================================
// Channel system
// ============================================================

void MENetwork::set_channel_count(MENodeId id, int channels) {
    if (!nodes_.count(id)) return;
    auto it = channel_providers_.find(id);
    if (it != channel_providers_.end()) {
        it->second.channels = channels;
    } else {
        channel_providers_[id] = ChannelProviderState{channels};
    }
    // Re-allocate channels on next rebuild / next query soon.
}

int MENetwork::network_total_channels(MENodeId id) const {
    int comp = component_of(id);
    if (comp < 0) return 0;
    auto it = component_info_.find(comp);
    return it != component_info_.end() ? it->second.total_channels : 0;
}

int MENetwork::network_online_devices(MENodeId id) const {
    int comp = component_of(id);
    if (comp < 0) return 0;
    auto it = component_info_.find(comp);
    return it != component_info_.end() ? it->second.online_count : 0;
}

bool MENetwork::is_node_online(MENodeId id) const {
    auto nit = nodes_.find(id);
    return nit != nodes_.end() && nit->second.online;
}

void MENetwork::allocate_channels() {
    component_info_.clear();

    // Reset all nodes to online.
    for (auto& [id, node] : nodes_) {
        node.online = true;
    }

    // Collect component data.
    for (auto& [id, data] : node_data_) {
        int comp = data.component_id;
        if (comp < 0) continue;
        auto& info = component_info_[comp];

        const auto& node = nodes_[id];
        if (node.type == MENodeType::Controller) {
            info.has_controller = true;
            auto it = channel_providers_.find(id);
            int ch = (it != channel_providers_.end()) ? it->second.channels : 32;
            info.total_channels += ch;
        } else if (node.type == MENodeType::Switch) {
            auto it = channel_providers_.find(id);
            int ch = (it != channel_providers_.end()) ? it->second.channels : 32;
            info.total_channels += ch;
        }
    }

    // For each component: mark excess devices as offline.
    for (auto& [comp, info] : component_info_) {
        if (!info.has_controller) {
            // No controller → all devices offline.
            for (MENodeId id : nodes_in_component(comp)) {
                nodes_[id].online = false;
            }
            info.total_channels = 0;
            info.online_count = 0;
            continue;
        }

        // Collect device nodes (not Controller, not Switch, not Cable).
        std::vector<MENodeId> devices;
        for (MENodeId id : nodes_in_component(comp)) {
            if (is_device_type(nodes_[id].type)) {
                devices.push_back(id);
            }
        }

        // Sort by BFS distance from controllers (devices closer to controller get priority).
        std::queue<MENodeId> q;
        std::unordered_map<MENodeId, int> dist;
        for (MENodeId id : nodes_in_component(comp)) {
            if (nodes_[id].type == MENodeType::Controller) {
                dist[id] = 0;
                q.push(id);
            }
        }

        while (!q.empty()) {
            MENodeId cur = q.front(); q.pop();
            int d = dist[cur];
            for (MENodeId nb : adjacency_[cur]) {
                if (component_of(nb) != comp) continue;
                if (!dist.count(nb)) {
                    dist[nb] = d + 1;
                    q.push(nb);
                }
            }
        }

        std::sort(devices.begin(), devices.end(),
            [&dist](MENodeId a, MENodeId b) {
                int da = dist.count(a) ? dist[a] : 9999;
                int db = dist.count(b) ? dist[b] : 9999;
                return da < db;
            });

        int avail = info.total_channels;
        info.online_count = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (static_cast<int>(i) < avail) {
                nodes_[devices[i]].online = true;
                info.online_count++;
            } else {
                nodes_[devices[i]].online = false;
            }
        }
    }
}

} // namespace science_and_theology::gt
