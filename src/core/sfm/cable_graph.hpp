#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "../power/power_node.hpp"

namespace science_and_theology::sfm {

// ============================================================
// CableGraph — cable topology for one Manager's network
// ============================================================
//
// The Manager connects to nearby containers via SFM inventory cables.
// Cables form a graph where each cable block is adjacent (6-face) to
// other cables. The Manager is the root. Containers are non-cable
// blocks adjacent to any cable in the graph.
//
// Container discovery: BFS from the manager position over cable cells,
// collecting adjacent non-cable positions that pass the
// `is_container` predicate.

class CableGraph {
public:
    CableGraph() = default;

    // Set the manager block position (root of the cable network).
    void set_manager_position(gt::MapPosition pos) { manager_pos_ = pos; }
    gt::MapPosition get_manager_position() const { return manager_pos_; }

    // Add / remove a cable segment at a position.
    void add_cable(gt::MapPosition pos) { cables_.insert(pos); }
    void remove_cable(gt::MapPosition pos) { cables_.erase(pos); }
    bool has_cable(gt::MapPosition pos) const { return cables_.count(pos) > 0; }
    size_t cable_count() const { return cables_.size(); }

    // Returns all cable positions.
    const std::unordered_set<gt::MapPosition>& cables() const { return cables_; }

    // Discover all container positions reachable from the manager via cables.
    // is_container: predicate returning true if a position holds a container
    //               (furnace, chest, machine, tank, ...). Cables and the
    //               manager itself should return false.
    std::vector<gt::MapPosition> discover_containers(
        const std::function<bool(gt::MapPosition)>& is_container) const;

    void clear() { cables_.clear(); }

private:
    gt::MapPosition manager_pos_;
    std::unordered_set<gt::MapPosition> cables_;

    // 6-face neighbors.
    static std::vector<gt::MapPosition> neighbors(gt::MapPosition p) {
        return {
            {p.x + 1, p.y, p.z}, {p.x - 1, p.y, p.z},
            {p.x, p.y + 1, p.z}, {p.x, p.y - 1, p.z},
            {p.x, p.y, p.z + 1}, {p.x, p.y, p.z - 1},
        };
    }
};

} // namespace science_and_theology::sfm
