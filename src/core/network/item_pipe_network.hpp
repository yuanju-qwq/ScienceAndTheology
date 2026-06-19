#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "power/power_node.hpp"
#include "pipe_types.hpp"

namespace science_and_theology::gt {

using ItemPipeNodeId = uint32_t;
inline constexpr ItemPipeNodeId kInvalidItemPipeNodeId = 0;

// Max number of distinct item types a pipe network can buffer at once.
inline constexpr int kMaxItemTypesInPipe = 64;

// ============================================================
// Item pipe node — a pipe tile or machine I/O port
// ============================================================

struct ItemPipeNode {
    ItemPipeNodeId id = kInvalidItemPipeNodeId;
    MapPosition position;

    // Whether this node is a source (machine output) or sink (machine input).
    // A node can be both (e.g. a pipe junction connected to both sides).
    bool is_source = false;
    bool is_sink = false;
};

// ============================================================
// Item pipe edge — a pipe segment between two nodes
// ============================================================

struct ItemPipeEdge {
    ItemPipeNodeId node_a = kInvalidItemPipeNodeId;
    ItemPipeNodeId node_b = kInvalidItemPipeNodeId;

    // Max items that can pass through this pipe per tick.
    int64_t max_items_per_tick = 1;

    int64_t distance_tiles = 0;

    bool connects(ItemPipeNodeId a, ItemPipeNodeId b) const {
        return (node_a == a && node_b == b) || (node_a == b && node_b == a);
    }
};

// ============================================================
// Item pipe network — shared-item-buffer model
// ============================================================
//
// Design: each connected component acts as a shared item buffer.
// Sources (machine output slots) push items into the buffer.
// Sinks (machine input slots) pull items from the buffer.
// No per-item routing or tick-by-tick movement — items are available
// instantly anywhere within the same component.
//
// This mirrors GT's item pipe behavior: pipes = shared connected inventory.
//
// Phase 2 may add throughput limits, per-tick movement, and routing.

class ItemPipeNetwork {
public:
    ItemPipeNetwork() = default;
    ~ItemPipeNetwork() = default;

    // --- Node lifecycle ---

    ItemPipeNodeId add_node(MapPosition position);
    bool remove_node(ItemPipeNodeId node_id);
    ItemPipeNode* get_node(ItemPipeNodeId node_id);
    const ItemPipeNode* get_node(ItemPipeNodeId node_id) const;
    size_t node_count() const { return nodes_.size(); }

    // --- Edge lifecycle ---

    bool connect(ItemPipeNodeId a, ItemPipeNodeId b, int64_t max_items_per_tick);
    bool disconnect(ItemPipeNodeId a, ItemPipeNodeId b);
    size_t edge_count() const { return edges_.size(); }

    // --- Item insertion / extraction ---

    // Inserts items into the pipe network at a node. Items become available
    // immediately to all nodes in the same connected component.
    // Returns the number of items actually inserted (may be limited by buffer).
    int64_t insert(ItemPipeNodeId node_id, uint16_t item_id, int64_t count);

    // Extracts items from the pipe network at a node.
    // Returns the number of items actually extracted.
    int64_t extract(ItemPipeNodeId node_id, uint16_t item_id,
                    int64_t requested);

    // Returns how many of a given item are available in the connected component.
    int64_t count_item(ItemPipeNodeId node_id, uint16_t item_id) const;

    // Returns total items stored in the connected component.
    int64_t count_total_items(ItemPipeNodeId node_id) const;

    // --- Network topology ---

    std::vector<ItemPipeNodeId> find_connected_component(
            ItemPipeNodeId start) const;
    std::vector<std::vector<ItemPipeNodeId>> find_all_components() const;
    bool are_connected(ItemPipeNodeId a, ItemPipeNodeId b) const;

    // --- Network update ---

    // Rebuilds component index after topology changes.
    void update_network();

    // --- Source / sink ---

    void set_source(ItemPipeNodeId node_id, bool is_source);
    void set_sink(ItemPipeNodeId node_id, bool is_sink);

    void clear();

private:
    using NodeMap = std::unordered_map<ItemPipeNodeId, ItemPipeNode>;
    using EdgeList = std::list<ItemPipeEdge>;
    using EdgeIterator = EdgeList::iterator;
    using AdjacencyList = std::unordered_map<ItemPipeNodeId,
                                              std::vector<EdgeIterator>>;

    static ItemPipeEdge* edge_ptr(EdgeIterator it) { return &(*it); }
    static const ItemPipeEdge* edge_cptr(EdgeIterator it) { return &(*it); }

    void add_adjacency(ItemPipeNodeId node_id, EdgeIterator edge_it);
    void remove_adjacency(ItemPipeNodeId node_id, EdgeIterator edge_it);

    ItemPipeNodeId next_id_ = 1;
    NodeMap nodes_;
    EdgeList edges_;
    std::unordered_map<MapPosition, ItemPipeNodeId> position_index_;
    AdjacencyList adjacency_;

    // Component index: node_id → component_id.
    std::unordered_map<ItemPipeNodeId, int> component_index_;

    // Per-component item buffer: component_id → (item_id → count).
    std::unordered_map<int, std::unordered_map<uint16_t, int64_t>>
            component_buffers_;
};

} // namespace science_and_theology::gt