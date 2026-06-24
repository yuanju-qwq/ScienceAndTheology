#include "voxel_network_graph.hpp"

#include <queue>
#include <unordered_set>

namespace science_and_theology::gt {

// Finds all positions in the same connected component as `start`.
// BFS over 6-face adjacency; returns empty if `start` not in graph.
std::vector<MapPosition> VoxelNetworkGraph::find_component(
        MapPosition start) const {
    std::vector<MapPosition> component;
    if (blocks_.count(start) == 0) {
        return component;
    }

    std::unordered_set<MapPosition> visited;
    std::queue<MapPosition> frontier;
    frontier.push(start);
    visited.insert(start);

    while (!frontier.empty()) {
        MapPosition current = frontier.front();
        frontier.pop();
        component.push_back(current);

        for (const MapPosition& n : neighbors(current)) {
            if (blocks_.count(n) > 0 && visited.count(n) == 0) {
                visited.insert(n);
                frontier.push(n);
            }
        }
    }
    return component;
}

// Returns all disjoint connected components in the graph.
std::vector<std::vector<MapPosition>>
VoxelNetworkGraph::find_all_components() const {
    std::vector<std::vector<MapPosition>> components;
    std::unordered_set<MapPosition> visited;

    for (const MapPosition& pos : blocks_) {
        if (visited.count(pos) > 0) {
            continue;
        }
        std::vector<MapPosition> comp = find_component(pos);
        for (const MapPosition& p : comp) {
            visited.insert(p);
        }
        components.push_back(std::move(comp));
    }
    return components;
}

// Returns positions adjacent to `component` that are not in the graph.
// Useful for discovering attached machines/containers/consumers.
std::vector<MapPosition> VoxelNetworkGraph::find_adjacent_outside(
        const std::vector<MapPosition>& component) const {
    std::vector<MapPosition> adjacent;
    std::unordered_set<MapPosition> seen;

    for (const MapPosition& pos : component) {
        for (const MapPosition& n : neighbors(pos)) {
            // Skip positions that are themselves conductors.
            if (blocks_.count(n) > 0) {
                continue;
            }
            // Deduplicate across the whole component.
            if (seen.insert(n).second) {
                adjacent.push_back(n);
            }
        }
    }
    return adjacent;
}

} // namespace science_and_theology::gt
