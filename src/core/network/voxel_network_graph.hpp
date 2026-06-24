#pragma once

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../power/power_node.hpp"  // for gt::MapPosition

namespace science_and_theology::gt {

// ============================================================
// VoxelNetworkGraph — per-block conduction topology foundation
// ============================================================
//
// Shared foundation for signal and power networks that use the
// "every block is a conductor" model (like Minecraft redstone dust
// or per-tile cables), as opposed to point-to-point pole networks.
//
// A graph stores the set of conductor block positions (signal wires
// or cables). Connectivity is implicit 6-face adjacency: two blocks
// are connected if they are both in the graph and differ by exactly
// one axis by one tile.
//
// Connected components are discovered via BFS. Each network
// (SignalNetwork, PowerNetwork) owns its own VoxelNetworkGraph
// instance and attaches domain-specific semantics (signal strength,
// voltage, loss) on top of the topology this class provides.
//
// Design notes:
// - MVP uses full-graph BFS rebuild on topology change. A chunk-
//   partitioned incremental cache can be layered on later without
//   changing the public API.
// - MapPosition hashing is provided in power_node.hpp.

class VoxelNetworkGraph {
public:
    VoxelNetworkGraph() = default;
    ~VoxelNetworkGraph() = default;

    // --- Block lifecycle ---

    // Adds a conductor block at the given position.
    void add_block(MapPosition pos) { blocks_.insert(pos); }

    // Removes a conductor block at the given position.
    void remove_block(MapPosition pos) { blocks_.erase(pos); }

    // Returns whether a conductor block exists at the given position.
    bool has_block(MapPosition pos) const { return blocks_.count(pos) > 0; }

    // Returns the number of conductor blocks in the graph.
    size_t block_count() const { return blocks_.size(); }

    // Returns all conductor positions (read-only).
    const std::unordered_set<MapPosition>& blocks() const { return blocks_; }

    // --- Topology queries ---

    // Finds all positions in the same connected component as `start`.
    // Uses BFS over 6-face adjacency. Returns an empty vector if
    // `start` is not in the graph.
    std::vector<MapPosition> find_component(MapPosition start) const;

    // Returns all disjoint connected components in the graph.
    // Each component is a vector of positions that are mutually
    // reachable via 6-face adjacency.
    std::vector<std::vector<MapPosition>> find_all_components() const;

    // Returns the set of positions adjacent (6-face) to any block in
    // `component` that are NOT themselves in the graph. Useful for
    // discovering consumers/producers (machines, containers) attached
    // to a network without being conductors themselves.
    std::vector<MapPosition> find_adjacent_outside(
        const std::vector<MapPosition>& component) const;

    // Clears all blocks.
    void clear() { blocks_.clear(); }

private:
    // The 6 face-neighbors of a position, in +X,-X,+Y,-Y,+Z,-Z order.
    static std::array<MapPosition, 6> neighbors(MapPosition p) {
        return {{
            {p.x + 1, p.y, p.z},
            {p.x - 1, p.y, p.z},
            {p.x, p.y + 1, p.z},
            {p.x, p.y - 1, p.z},
            {p.x, p.y, p.z + 1},
            {p.x, p.y, p.z - 1},
        }};
    }

    std::unordered_set<MapPosition> blocks_;
};

} // namespace science_and_theology::gt
