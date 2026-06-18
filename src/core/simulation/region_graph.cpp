#include "region_graph.hpp"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace science_and_theology {

// --- RegionGraph ---

RegionGraph::RegionGraph(RegionType type) : type_(type) {}

uint64_t RegionGraph::add_node(const RegionNode& node) {
    auto it = node_to_region_.find(node);
    if (it != node_to_region_.end()) {
        return find(it->second);
    }

    uint64_t rid = next_region_id_++;

    // Initialize Union-Find entry.
    parent_[rid] = rid;
    rank_[rid] = 0;

    // Initialize RegionData.
    RegionData data;
    data.region_id = rid;
    data.type = type_;
    data.dimension_id = node.dimension_id;
    data.pollution = 0.0;
    data.temperature = 20.0;
    data.node_count = 1;
    regions_[rid] = data;

    node_to_region_[node] = rid;
    return rid;
}

void RegionGraph::remove_node(const RegionNode& node) {
    auto it = node_to_region_.find(node);
    if (it == node_to_region_.end()) return;

    uint64_t rid = find(it->second);

    // Decrease node count.
    auto rit = regions_.find(rid);
    if (rit != regions_.end()) {
        if (rit->second.node_count > 0) {
            --rit->second.node_count;
        }
    }

    // Mark the region as dirty for split detection.
    dirty_regions_.insert(rid);

    // Remove the node from the mapping.
    node_to_region_.erase(it);
}

bool RegionGraph::has_node(const RegionNode& node) const {
    return node_to_region_.find(node) != node_to_region_.end();
}

uint64_t RegionGraph::find_region(const RegionNode& node) const {
    auto it = node_to_region_.find(node);
    if (it == node_to_region_.end()) return 0;
    return find(it->second);
}

uint64_t RegionGraph::link(const RegionNode& a, const RegionNode& b) {
    auto it_a = node_to_region_.find(a);
    auto it_b = node_to_region_.find(b);
    if (it_a == node_to_region_.end() || it_b == node_to_region_.end()) {
        return 0;
    }

    uint64_t ra = find(it_a->second);
    uint64_t rb = find(it_b->second);
    if (ra == rb) return ra;

    uint64_t merged = unite(ra, rb);

    // Merge node counts.
    auto data_a = regions_.find(ra);
    auto data_b = regions_.find(rb);
    if (data_a != regions_.end() && data_b != regions_.end()) {
        if (merged == ra) {
            data_a->second.node_count += data_b->second.node_count;
            // Weighted average for pollution and temperature.
            size_t total = data_a->second.node_count;
            if (total > 0) {
                double wa = static_cast<double>(
                    data_a->second.node_count - data_b->second.node_count) / total;
                double wb = static_cast<double>(data_b->second.node_count) / total;
                data_a->second.pollution =
                    data_a->second.pollution * wa + data_b->second.pollution * wb;
                data_a->second.temperature =
                    data_a->second.temperature * wa + data_b->second.temperature * wb;
            }
            regions_.erase(rb);
        } else {
            data_b->second.node_count += data_a->second.node_count;
            size_t total = data_b->second.node_count;
            if (total > 0) {
                double wb = static_cast<double>(
                    data_b->second.node_count - data_a->second.node_count) / total;
                double wa = static_cast<double>(data_a->second.node_count) / total;
                data_b->second.pollution =
                    data_b->second.pollution * wb + data_a->second.pollution * wa;
                data_b->second.temperature =
                    data_b->second.temperature * wb + data_a->second.temperature * wa;
            }
            regions_.erase(ra);
        }
    }

    return merged;
}

const RegionData* RegionGraph::get_region_data(uint64_t region_id) const {
    auto it = regions_.find(region_id);
    return it != regions_.end() ? &it->second : nullptr;
}

RegionData* RegionGraph::get_region_data_mut(uint64_t region_id) {
    auto it = regions_.find(region_id);
    return it != regions_.end() ? &it->second : nullptr;
}

std::vector<uint64_t> RegionGraph::all_region_ids() const {
    std::vector<uint64_t> ids;
    ids.reserve(regions_.size());
    for (const auto& [rid, _] : regions_) {
        ids.push_back(rid);
    }
    return ids;
}

void RegionGraph::mark_dirty(uint64_t region_id) {
    dirty_regions_.insert(region_id);
}

std::vector<uint64_t> RegionGraph::rebuild_dirty(
    const std::function<std::vector<RegionNode>(const RegionNode&)>&
        adjacency_fn) {
    std::vector<uint64_t> rebuilt;

    for (uint64_t dirty_rid : dirty_regions_) {
        // Collect all nodes that currently belong to this dirty region.
        std::unordered_set<RegionNode> members;
        for (const auto& [node, rid] : node_to_region_) {
            if (find(rid) == dirty_rid) {
                members.insert(node);
            }
        }

        if (members.empty()) {
            // Region has no remaining nodes; remove it.
            regions_.erase(dirty_rid);
            rebuilt.push_back(dirty_rid);
            continue;
        }

        // BFS from an arbitrary seed to find the first component.
        RegionNode seed = *members.begin();
        auto component = bfs_component(seed, adjacency_fn, members);

        // If all members are in one component, no split occurred.
        if (component.size() == members.size()) {
            // Update node count.
            auto data = regions_.find(dirty_rid);
            if (data != regions_.end()) {
                data->second.node_count = component.size();
            }
            continue;
        }

        // Split detected. The first component keeps the original region ID.
        auto orig_data = regions_.find(dirty_rid);
        RegionData orig_snapshot;
        if (orig_data != regions_.end()) {
            orig_snapshot = orig_data->second;
            orig_data->second.node_count = component.size();
        }

        // Remove first component members from the remaining set.
        std::unordered_set<RegionNode> remaining;
        for (const auto& n : members) {
            if (component.find(n) == component.end()) {
                remaining.insert(n);
            }
        }

        // Reassign first component nodes to the original region.
        for (const auto& n : component) {
            node_to_region_[n] = dirty_rid;
        }

        // BFS remaining members to create new regions.
        while (!remaining.empty()) {
            RegionNode rseed = *remaining.begin();
            auto rcomp = bfs_component(rseed, adjacency_fn, remaining);

            uint64_t new_rid = next_region_id_++;
            parent_[new_rid] = new_rid;
            rank_[new_rid] = 0;

            RegionData new_data;
            new_data.region_id = new_rid;
            new_data.type = type_;
            new_data.dimension_id = orig_snapshot.dimension_id;
            // Inherit pollution/temperature from the original region.
            new_data.pollution = orig_snapshot.pollution;
            new_data.temperature = orig_snapshot.temperature;
            new_data.node_count = rcomp.size();
            regions_[new_rid] = new_data;

            for (const auto& n : rcomp) {
                node_to_region_[n] = new_rid;
            }

            rebuilt.push_back(new_rid);

            for (const auto& n : rcomp) {
                remaining.erase(n);
            }
        }

        rebuilt.push_back(dirty_rid);
    }

    dirty_regions_.clear();
    return rebuilt;
}

std::vector<uint64_t> RegionGraph::rebuild_all(
    const std::function<std::vector<RegionNode>(const RegionNode&)>&
        adjacency_fn) {
    // Collect all current nodes.
    std::unordered_set<RegionNode> all_nodes;
    for (const auto& [node, _] : node_to_region_) {
        all_nodes.insert(node);
    }

    // Reset all region state.
    regions_.clear();
    parent_.clear();
    rank_.clear();
    dirty_regions_.clear();
    next_region_id_ = 1;

    std::vector<uint64_t> new_ids;
    std::unordered_set<RegionNode> visited;

    for (const auto& node : all_nodes) {
        if (visited.find(node) != visited.end()) continue;

        auto component = bfs_component(node, adjacency_fn, all_nodes);

        uint64_t rid = next_region_id_++;
        parent_[rid] = rid;
        rank_[rid] = 0;

        RegionData data;
        data.region_id = rid;
        data.type = type_;
        data.dimension_id = node.dimension_id;
        data.pollution = 0.0;
        data.temperature = 20.0;
        data.node_count = component.size();
        regions_[rid] = data;

        for (const auto& n : component) {
            node_to_region_[n] = rid;
            visited.insert(n);
        }

        new_ids.push_back(rid);
    }

    return new_ids;
}

void RegionGraph::tick_pollution(
    const std::function<std::vector<uint64_t>(uint64_t)>& neighbor_fn,
    float diffusion_rate) {
    // Compute new pollution levels for all regions.
    std::unordered_map<uint64_t, double> new_pollution;

    for (const auto& [rid, data] : regions_) {
        auto neighbors = neighbor_fn(rid);
        if (neighbors.empty()) {
            new_pollution[rid] = data.pollution;
            continue;
        }

        double sum = data.pollution;
        for (uint64_t nid : neighbors) {
            auto it = regions_.find(nid);
            if (it != regions_.end()) {
                sum += it->second.pollution;
            }
        }
        double avg = sum / (1.0 + static_cast<double>(neighbors.size()));
        new_pollution[rid] = data.pollution +
            (avg - data.pollution) * diffusion_rate;
    }

    // Apply.
    for (auto& [rid, data] : regions_) {
        auto it = new_pollution.find(rid);
        if (it != new_pollution.end()) {
            data.pollution = std::clamp(it->second, 0.0, 1.0);
        }
    }
}

void RegionGraph::tick_temperature(
    const std::function<std::vector<uint64_t>(uint64_t)>& neighbor_fn,
    float conduction_rate) {
    std::unordered_map<uint64_t, double> new_temp;

    for (const auto& [rid, data] : regions_) {
        auto neighbors = neighbor_fn(rid);
        if (neighbors.empty()) {
            new_temp[rid] = data.temperature;
            continue;
        }

        double sum = data.temperature;
        for (uint64_t nid : neighbors) {
            auto it = regions_.find(nid);
            if (it != regions_.end()) {
                sum += it->second.temperature;
            }
        }
        double avg = sum / (1.0 + static_cast<double>(neighbors.size()));
        new_temp[rid] = data.temperature +
            (avg - data.temperature) * conduction_rate;
    }

    for (auto& [rid, data] : regions_) {
        auto it = new_temp.find(rid);
        if (it != new_temp.end()) {
            data.temperature = it->second;
        }
    }
}

// --- Union-Find ---

uint64_t RegionGraph::find(uint64_t id) const {
    auto it = parent_.find(id);
    if (it == parent_.end()) return id;
    if (it->second != id) {
        it->second = find(it->second);
    }
    return it->second;
}

uint64_t RegionGraph::unite(uint64_t a, uint64_t b) {
    uint64_t ra = find(a);
    uint64_t rb = find(b);
    if (ra == rb) return ra;

    auto ra_it = rank_.find(ra);
    auto rb_it = rank_.find(rb);
    uint64_t ra_rank = ra_it != rank_.end() ? ra_it->second : 0;
    uint64_t rb_rank = rb_it != rank_.end() ? rb_it->second : 0;

    if (ra_rank < rb_rank) {
        std::swap(ra, rb);
    }

    parent_[rb] = ra;
    if (ra_rank == rb_rank) {
        rank_[ra] = ra_rank + 1;
    }

    return ra;
}

// --- BFS ---

std::unordered_set<RegionNode> RegionGraph::bfs_component(
    const RegionNode& seed,
    const std::function<std::vector<RegionNode>(const RegionNode&)>&
        adjacency_fn,
    const std::unordered_set<RegionNode>& valid_nodes) const {
    std::unordered_set<RegionNode> visited;
    std::queue<RegionNode> q;
    q.push(seed);
    visited.insert(seed);

    while (!q.empty()) {
        RegionNode current = q.front();
        q.pop();

        auto neighbors = adjacency_fn(current);
        for (const auto& nb : neighbors) {
            if (visited.find(nb) != visited.end()) continue;
            if (valid_nodes.find(nb) == valid_nodes.end()) continue;
            visited.insert(nb);
            q.push(nb);
        }
    }

    return visited;
}

} // namespace science_and_theology
