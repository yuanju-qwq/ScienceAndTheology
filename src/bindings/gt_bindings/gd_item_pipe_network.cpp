#include "gd_item_pipe_network.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDItemPipeNetwork::GDItemPipeNetwork() = default;
GDItemPipeNetwork::~GDItemPipeNetwork() = default;

void GDItemPipeNetwork::_bind_methods() {
    // Node lifecycle
    ClassDB::bind_method(D_METHOD("add_node", "position"),
                         &GDItemPipeNetwork::add_node);
    ClassDB::bind_method(D_METHOD("remove_node", "node_id"),
                         &GDItemPipeNetwork::remove_node);
    ClassDB::bind_method(D_METHOD("get_node_info", "node_id"),
                         &GDItemPipeNetwork::get_node_info);
    ClassDB::bind_method(D_METHOD("get_node_count"),
                         &GDItemPipeNetwork::get_node_count);

    // Edge lifecycle
    ClassDB::bind_method(D_METHOD("connect_nodes", "node_a", "node_b",
                                   "max_items_per_tick"),
                         &GDItemPipeNetwork::connect_nodes);
    ClassDB::bind_method(D_METHOD("disconnect_nodes", "node_a", "node_b"),
                         &GDItemPipeNetwork::disconnect_nodes);

    // Item insertion / extraction
    ClassDB::bind_method(D_METHOD("insert", "node_id", "item_id", "count"),
                         &GDItemPipeNetwork::insert);
    ClassDB::bind_method(D_METHOD("extract", "node_id", "item_id",
                                   "requested"),
                         &GDItemPipeNetwork::extract);
    ClassDB::bind_method(D_METHOD("count_item", "node_id", "item_id"),
                         &GDItemPipeNetwork::count_item);
    ClassDB::bind_method(D_METHOD("count_total_items", "node_id"),
                         &GDItemPipeNetwork::count_total_items);

    // Topology
    ClassDB::bind_method(D_METHOD("find_connected_component", "start_id"),
                         &GDItemPipeNetwork::find_connected_component);
    ClassDB::bind_method(D_METHOD("are_connected", "node_a", "node_b"),
                         &GDItemPipeNetwork::are_connected);

    // Network update
    ClassDB::bind_method(D_METHOD("update_network"),
                         &GDItemPipeNetwork::update_network);

    // Source / sink
    ClassDB::bind_method(D_METHOD("set_source", "node_id", "is_source"),
                         &GDItemPipeNetwork::set_source);
    ClassDB::bind_method(D_METHOD("set_sink", "node_id", "is_sink"),
                         &GDItemPipeNetwork::set_sink);

    ClassDB::bind_method(D_METHOD("clear"),
                         &GDItemPipeNetwork::clear);

    // Pipe type constants
    ClassDB::bind_integer_constant(
        "GDItemPipeNetwork", "PIPE_TYPE_LIQUID", 0);
    ClassDB::bind_integer_constant(
        "GDItemPipeNetwork", "PIPE_TYPE_GAS", 1);
    ClassDB::bind_integer_constant(
        "GDItemPipeNetwork", "PIPE_TYPE_ITEM", 2);
}

// --- Node lifecycle ---

int64_t GDItemPipeNetwork::add_node(godot::Vector2i position) {
    ItemPipeNodeId id = network_.add_node(_from_godot(position));
    return static_cast<int64_t>(id);
}

bool GDItemPipeNetwork::remove_node(int64_t node_id) {
    return network_.remove_node(static_cast<ItemPipeNodeId>(node_id));
}

godot::Dictionary GDItemPipeNetwork::get_node_info(int64_t node_id) const {
    godot::Dictionary info;
    const ItemPipeNode* node = network_.get_node(
        static_cast<ItemPipeNodeId>(node_id));
    if (node == nullptr) return info;

    info["id"] = static_cast<int64_t>(node->id);
    info["position"] = _to_godot(node->position);
    info["is_source"] = node->is_source;
    info["is_sink"] = node->is_sink;
    return info;
}

int64_t GDItemPipeNetwork::get_node_count() const {
    return static_cast<int64_t>(network_.node_count());
}

// --- Edge lifecycle ---

bool GDItemPipeNetwork::connect_nodes(int64_t node_a, int64_t node_b,
                                       int64_t max_items_per_tick) {
    return network_.connect(static_cast<ItemPipeNodeId>(node_a),
                             static_cast<ItemPipeNodeId>(node_b),
                             max_items_per_tick);
}

bool GDItemPipeNetwork::disconnect_nodes(int64_t node_a, int64_t node_b) {
    return network_.disconnect(static_cast<ItemPipeNodeId>(node_a),
                                static_cast<ItemPipeNodeId>(node_b));
}

// --- Item insertion / extraction ---

int64_t GDItemPipeNetwork::insert(int64_t node_id, int item_id,
                                   int64_t count) {
    return network_.insert(static_cast<ItemPipeNodeId>(node_id),
                            static_cast<uint16_t>(item_id), count);
}

int64_t GDItemPipeNetwork::extract(int64_t node_id, int item_id,
                                    int64_t requested) {
    return network_.extract(static_cast<ItemPipeNodeId>(node_id),
                             static_cast<uint16_t>(item_id), requested);
}

int64_t GDItemPipeNetwork::count_item(int64_t node_id, int item_id) const {
    return network_.count_item(static_cast<ItemPipeNodeId>(node_id),
                                static_cast<uint16_t>(item_id));
}

int64_t GDItemPipeNetwork::count_total_items(int64_t node_id) const {
    return network_.count_total_items(static_cast<ItemPipeNodeId>(node_id));
}

// --- Topology ---

godot::PackedInt32Array GDItemPipeNetwork::find_connected_component(
        int64_t start_id) const {
    auto comp = network_.find_connected_component(
        static_cast<ItemPipeNodeId>(start_id));
    godot::PackedInt32Array arr;
    for (auto id : comp) {
        arr.append(static_cast<int32_t>(id));
    }
    return arr;
}

bool GDItemPipeNetwork::are_connected(int64_t node_a, int64_t node_b) const {
    return network_.are_connected(static_cast<ItemPipeNodeId>(node_a),
                                   static_cast<ItemPipeNodeId>(node_b));
}

// --- Network update ---

void GDItemPipeNetwork::update_network() {
    network_.update_network();
}

// --- Source / sink ---

void GDItemPipeNetwork::set_source(int64_t node_id, bool is_source) {
    network_.set_source(static_cast<ItemPipeNodeId>(node_id), is_source);
}

void GDItemPipeNetwork::set_sink(int64_t node_id, bool is_sink) {
    network_.set_sink(static_cast<ItemPipeNodeId>(node_id), is_sink);
}

void GDItemPipeNetwork::clear() {
    network_.clear();
}

// --- Conversion helpers ---

gt::MapPosition GDItemPipeNetwork::_from_godot(godot::Vector2i pos) {
    return gt::MapPosition{pos.x, pos.y};
}

godot::Vector2i GDItemPipeNetwork::_to_godot(const gt::MapPosition& pos) {
    return godot::Vector2i(pos.x, pos.y);
}

} // namespace science_and_theology