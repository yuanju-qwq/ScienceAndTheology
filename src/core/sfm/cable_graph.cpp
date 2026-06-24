#include "cable_graph.hpp"

#include <queue>
#include <unordered_set>

namespace science_and_theology::sfm {

std::vector<gt::MapPosition> CableGraph::discover_containers(
        const std::function<bool(gt::MapPosition)>& is_container) const {
    std::vector<gt::MapPosition> result;
    std::unordered_set<gt::MapPosition> visited;
    std::unordered_set<gt::MapPosition> found_containers;

    // BFS from the manager position over cable cells.
    std::queue<gt::MapPosition> queue;
    // Start: check neighbors of the manager (cables directly adjacent).
    visited.insert(manager_pos_);
    for (const auto& n : neighbors(manager_pos_)) {
        if (cables_.count(n)) {
            queue.push(n);
            visited.insert(n);
        } else if (is_container(n)) {
            found_containers.insert(n);
        }
    }

    while (!queue.empty()) {
        gt::MapPosition cur = queue.front();
        queue.pop();
        for (const auto& n : neighbors(cur)) {
            if (visited.count(n)) continue;
            visited.insert(n);
            if (cables_.count(n)) {
                queue.push(n);
            } else if (is_container(n)) {
                found_containers.insert(n);
            }
        }
    }

    result.assign(found_containers.begin(), found_containers.end());
    return result;
}

} // namespace science_and_theology::sfm
