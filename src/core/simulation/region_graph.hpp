#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

#include "../world/chunk_data.hpp"

namespace science_and_theology {

// Type of region managed by the RegionGraph.
enum class RegionType : uint8_t {
    POWER_GRID  = 0,
    FLUID       = 1,
    CONNECTED   = 2,
    POLLUTION   = 3,
    TEMPERATURE = 4,
    COUNT       = 5,
};

// Human-readable names for RegionType.
constexpr const char* kRegionTypeNames[] = {
    "power_grid", "fluid", "connected", "pollution", "temperature",
};

// Identifies a single node (block position) in the region graph.
// Each node belongs to exactly one region for a given RegionType.
struct RegionNode {
    std::string dimension_id;
    int block_x = 0;
    int block_y = 0;
    int block_z = 0;

    bool operator==(const RegionNode& o) const {
        return block_x == o.block_x
            && block_y == o.block_y
            && block_z == o.block_z
            && dimension_id == o.dimension_id;
    }

    bool operator!=(const RegionNode& o) const { return !(*this == o); }
};

// Per-region data: pollution level, temperature, and member count.
struct RegionData {
    uint64_t region_id = 0;
    RegionType type = RegionType::CONNECTED;
    std::string dimension_id;

    // Pollution level [0, 1]. 0 = clean, 1 = fully polluted.
    double pollution = 0.0;

    // Temperature in degrees (arbitrary game scale).
    double temperature = 20.0;

    // Number of member nodes in this region.
    size_t node_count = 0;
};

} // namespace science_and_theology

// std::hash specializations for RegionNode.
template <>
struct std::hash<science_and_theology::RegionNode> {
    size_t operator()(const science_and_theology::RegionNode& n) const {
        size_t h = std::hash<std::string>()(n.dimension_id);
        h ^= std::hash<int>()(n.block_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(n.block_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(n.block_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace science_and_theology {

// Union-Find based region graph.
// Manages connected components (regions) for a specific RegionType.
//
// Each RegionNode maps to a region ID. When two adjacent nodes are linked,
// they belong to the same region. When a node is removed, the component
// may split — this is detected lazily on the next rebuild.
//
// Design:
//   - Uses Union-Find (disjoint set) for O(alpha(n)) amortized merge.
//   - Split detection is deferred: when a node is removed, we mark
//     the region as "dirty" and rebuild the affected region via BFS
//     on the next tick or explicit call.
//   - RegionData (pollution, temperature) is stored per region ID.
//   - When regions merge, the larger region absorbs the smaller one's data.
//
// Thread safety: main thread only. Not thread-safe.
class RegionGraph {
public:
    explicit RegionGraph(RegionType type);
    ~RegionGraph() = default;

    // Disallow copy, allow move.
    RegionGraph(const RegionGraph&) = delete;
    RegionGraph& operator=(const RegionGraph&) = delete;
    RegionGraph(RegionGraph&&) = default;
    RegionGraph& operator=(RegionGraph&&) = default;

    // --- Node management ---

    // Add a node to the graph. Initially in its own region.
    // Returns the region ID assigned to the node.
    uint64_t add_node(const RegionNode& node);

    // Remove a node from the graph. Marks the region as dirty.
    void remove_node(const RegionNode& node);

    // Returns true if the node exists in the graph.
    bool has_node(const RegionNode& node) const;

    // Returns the region ID for a node. Returns 0 if not found.
    uint64_t find_region(const RegionNode& node) const;

    // --- Edge / link management ---

    // Link two adjacent nodes into the same region (Union operation).
    // Both nodes must already exist. Returns the resulting region ID.
    uint64_t link(const RegionNode& a, const RegionNode& b);

    // --- Region queries ---

    // Returns the RegionData for a given region ID, or nullptr.
    const RegionData* get_region_data(uint64_t region_id) const;
    RegionData* get_region_data_mut(uint64_t region_id);

    // Returns all region IDs in this graph.
    std::vector<uint64_t> all_region_ids() const;

    // Returns the number of regions.
    size_t region_count() const { return regions_.size(); }

    // Returns the number of nodes.
    size_t node_count() const { return node_to_region_.size(); }

    // Returns the RegionType of this graph.
    RegionType type() const { return type_; }

    // --- Dirty region tracking ---

    // Returns region IDs that need rebuild (split detection).
    const std::unordered_set<uint64_t>& dirty_regions() const {
        return dirty_regions_;
    }

    // Mark a region as needing rebuild.
    void mark_dirty(uint64_t region_id);

    // Clear the dirty set after rebuilds are complete.
    void clear_dirty() { dirty_regions_.clear(); }

    // --- Rebuild ---

    // Rebuild all dirty regions using BFS from remaining nodes.
    // The adjacency_fn callback returns neighbors of a given node.
    // Returns the list of region IDs that were rebuilt.
    std::vector<uint64_t> rebuild_dirty(
        const std::function<std::vector<RegionNode>(const RegionNode&)>&
            adjacency_fn);

    // Full rebuild: recompute all regions from scratch using BFS.
    std::vector<uint64_t> rebuild_all(
        const std::function<std::vector<RegionNode>(const RegionNode&)>&
            adjacency_fn);

    // --- Pollution & temperature simulation ---

    // Advance pollution diffusion by one step.
    // Each region's pollution moves toward the average of its neighbors.
    // neighbor_fn returns the region IDs adjacent to a given region.
    void tick_pollution(
        const std::function<std::vector<uint64_t>(uint64_t)>& neighbor_fn,
        float diffusion_rate);

    // Advance temperature simulation by one step.
    // Each region's temperature moves toward the average of its neighbors.
    void tick_temperature(
        const std::function<std::vector<uint64_t>(uint64_t)>& neighbor_fn,
        float conduction_rate);

    // --- Bulk access ---

    // Returns a read-only view of all node-to-region mappings.
    const std::unordered_map<RegionNode, uint64_t>& node_map() const {
        return node_to_region_;
    }

private:
    // Union-Find: find root with path compression.
    uint64_t find(uint64_t id) const;

    // Union-Find: union two sets. Returns the new root.
    uint64_t unite(uint64_t a, uint64_t b);

    // BFS from a seed node to discover the connected component.
    // Returns the set of nodes in the component.
    std::unordered_set<RegionNode> bfs_component(
        const RegionNode& seed,
        const std::function<std::vector<RegionNode>(const RegionNode&)>&
            adjacency_fn,
        const std::unordered_set<RegionNode>& valid_nodes) const;

    RegionType type_;

    // Union-Find structures.
    mutable std::unordered_map<uint64_t, uint64_t> parent_;
    mutable std::unordered_map<uint64_t, uint64_t> rank_;

    // Node -> region ID mapping.
    std::unordered_map<RegionNode, uint64_t> node_to_region_;

    // Region ID -> RegionData.
    std::unordered_map<uint64_t, RegionData> regions_;

    // Region IDs that need split detection rebuild.
    std::unordered_set<uint64_t> dirty_regions_;

    // Next region ID counter.
    uint64_t next_region_id_ = 1;
};

} // namespace science_and_theology
