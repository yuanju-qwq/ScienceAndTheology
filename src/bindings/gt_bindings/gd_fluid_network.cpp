#include "gd_fluid_network.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/gt/fluid/fluid_types.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDFluidNetwork::GDFluidNetwork() = default;
GDFluidNetwork::~GDFluidNetwork() = default;

void GDFluidNetwork::_bind_methods() {
    // Node lifecycle
    ClassDB::bind_method(D_METHOD("add_node", "position", "pipe_type"),
                         &GDFluidNetwork::add_node, DEFVAL(PIPE_LIQUID));
    ClassDB::bind_method(D_METHOD("remove_node", "node_id"),
                         &GDFluidNetwork::remove_node);
    ClassDB::bind_method(D_METHOD("get_node_info", "node_id"),
                         &GDFluidNetwork::get_node_info);
    ClassDB::bind_method(D_METHOD("get_node_count"),
                         &GDFluidNetwork::get_node_count);

    // Edge lifecycle
    ClassDB::bind_method(D_METHOD("connect_nodes", "node_a", "node_b",
                                   "max_flow_rate"),
                         &GDFluidNetwork::connect_nodes);
    ClassDB::bind_method(D_METHOD("disconnect_nodes", "node_a", "node_b"),
                         &GDFluidNetwork::disconnect_nodes);

    // Fluid type
    ClassDB::bind_method(D_METHOD("set_node_fluid_type", "node_id",
                                   "fluid_type_id"),
                         &GDFluidNetwork::set_node_fluid_type);
    ClassDB::bind_method(D_METHOD("get_component_fluid_type", "node_id"),
                         &GDFluidNetwork::get_component_fluid_type);

    // Producer / consumer
    ClassDB::bind_method(D_METHOD("set_producer", "node_id", "amount"),
                         &GDFluidNetwork::set_producer);
    ClassDB::bind_method(D_METHOD("set_consumer", "node_id", "demand"),
                         &GDFluidNetwork::set_consumer);

    // Network update
    ClassDB::bind_method(D_METHOD("update_network"),
                         &GDFluidNetwork::update_network);

    // Topology
    ClassDB::bind_method(D_METHOD("find_connected_component", "start_id"),
                         &GDFluidNetwork::find_connected_component);
    ClassDB::bind_method(D_METHOD("are_connected", "node_a", "node_b"),
                         &GDFluidNetwork::are_connected);

    // Flow queries
    ClassDB::bind_method(D_METHOD("get_available_flow", "node_id"),
                         &GDFluidNetwork::get_available_flow);
    ClassDB::bind_method(D_METHOD("get_component_total_production",
                                   "node_id"),
                         &GDFluidNetwork::get_component_total_production);
    ClassDB::bind_method(D_METHOD("get_component_total_demand", "node_id"),
                         &GDFluidNetwork::get_component_total_demand);
    ClassDB::bind_method(D_METHOD("get_delivered", "node_id"),
                         &GDFluidNetwork::get_delivered);

    ClassDB::bind_method(D_METHOD("clear"), &GDFluidNetwork::clear);

    // Static helpers
    ClassDB::bind_static_method("GDFluidNetwork",
        D_METHOD("get_fluid_info", "fluid_id"),
        &GDFluidNetwork::get_fluid_info);
    ClassDB::bind_static_method("GDFluidNetwork",
        D_METHOD("get_fluid_by_name", "name"),
        &GDFluidNetwork::get_fluid_by_name);
    ClassDB::bind_static_method("GDFluidNetwork",
        D_METHOD("get_fluid_id", "name"),
        &GDFluidNetwork::get_fluid_id);
    ClassDB::bind_static_method("GDFluidNetwork",
        D_METHOD("get_all_fluid_names"),
        &GDFluidNetwork::get_all_fluid_names);
    ClassDB::bind_static_method("GDFluidNetwork",
        D_METHOD("get_fluid_count"),
        &GDFluidNetwork::get_fluid_count);

    // Pipe type constants for GDScript.
    BIND_CONSTANT(PIPE_LIQUID);
    BIND_CONSTANT(PIPE_GAS);
}

// --- Node lifecycle ---

int64_t GDFluidNetwork::add_node(godot::Vector2i position, int pipe_type) {
    FluidNodeId id = network_.add_node(_from_godot(position),
                                        static_cast<PipeType>(pipe_type));
    return static_cast<int64_t>(id);
}

bool GDFluidNetwork::remove_node(int64_t node_id) {
    return network_.remove_node(static_cast<FluidNodeId>(node_id));
}

godot::Dictionary GDFluidNetwork::get_node_info(int64_t node_id) const {
    godot::Dictionary info;
    const FluidNode* node = network_.get_node(
        static_cast<FluidNodeId>(node_id));
    if (node == nullptr) return info;

    info["id"] = static_cast<int64_t>(node->id);
    info["position"] = _to_godot(node->position);
    info["pipe_type"] = static_cast<int>(node->pipe_type);
    info["fluid_type"] = static_cast<int>(node->fluid_type);
    info["production"] = node->production;
    info["demand"] = node->demand;
    info["buffer"] = node->buffer;
    info["delivered"] = node->delivered;
    return info;
}

int64_t GDFluidNetwork::get_node_count() const {
    return static_cast<int64_t>(network_.node_count());
}

// --- Edge lifecycle ---

bool GDFluidNetwork::connect_nodes(int64_t node_a, int64_t node_b,
                                    int64_t max_flow_rate) {
    return network_.connect(static_cast<FluidNodeId>(node_a),
                             static_cast<FluidNodeId>(node_b),
                             max_flow_rate);
}

bool GDFluidNetwork::disconnect_nodes(int64_t node_a, int64_t node_b) {
    return network_.disconnect(static_cast<FluidNodeId>(node_a),
                                static_cast<FluidNodeId>(node_b));
}

// --- Fluid type ---

bool GDFluidNetwork::set_node_fluid_type(int64_t node_id, int fluid_type_id) {
    return network_.set_node_fluid_type(
        static_cast<FluidNodeId>(node_id),
        static_cast<FluidId>(fluid_type_id));
}

int GDFluidNetwork::get_component_fluid_type(int64_t node_id) const {
    return static_cast<int>(network_.get_component_fluid_type(
        static_cast<FluidNodeId>(node_id)));
}

// --- Producer / consumer ---

void GDFluidNetwork::set_producer(int64_t node_id, int64_t amount) {
    network_.set_producer(static_cast<FluidNodeId>(node_id), amount);
}

void GDFluidNetwork::set_consumer(int64_t node_id, int64_t demand) {
    network_.set_consumer(static_cast<FluidNodeId>(node_id), demand);
}

// --- Network update ---

void GDFluidNetwork::update_network() {
    network_.update_network();
}

// --- Topology ---

godot::PackedInt32Array GDFluidNetwork::find_connected_component(
        int64_t start_id) const {
    auto comp = network_.find_connected_component(
        static_cast<FluidNodeId>(start_id));
    godot::PackedInt32Array arr;
    for (auto id : comp) {
        arr.append(static_cast<int32_t>(id));
    }
    return arr;
}

bool GDFluidNetwork::are_connected(int64_t node_a, int64_t node_b) const {
    return network_.are_connected(static_cast<FluidNodeId>(node_a),
                                   static_cast<FluidNodeId>(node_b));
}

// --- Flow queries ---

int64_t GDFluidNetwork::get_available_flow(int64_t node_id) const {
    return network_.get_available_flow(static_cast<FluidNodeId>(node_id));
}

int64_t GDFluidNetwork::get_component_total_production(
        int64_t node_id) const {
    return network_.get_component_total_production(
        static_cast<FluidNodeId>(node_id));
}

int64_t GDFluidNetwork::get_component_total_demand(int64_t node_id) const {
    return network_.get_component_total_demand(
        static_cast<FluidNodeId>(node_id));
}

int64_t GDFluidNetwork::get_delivered(int64_t node_id) const {
    return network_.get_delivered(static_cast<FluidNodeId>(node_id));
}

void GDFluidNetwork::clear() {
    network_.clear();
}

// --- Static helpers ---

godot::Dictionary GDFluidNetwork::get_fluid_info(int fluid_id) {
    godot::Dictionary info;
    const FluidDefinition* def = FluidRegistry::get_fluid(
        static_cast<FluidId>(fluid_id));
    if (def == nullptr) return info;

    info["name"] = def->name;
    info["display_name"] = def->display_name;
    info["chemical_formula"] = def->chemical_formula;
    info["temperature"] = def->temperature;
    info["is_gas"] = def->is_gas;
    return info;
}

godot::Dictionary GDFluidNetwork::get_fluid_by_name(
        const godot::String& name) {
    godot::Dictionary info;
    const FluidDefinition* def = FluidRegistry::get_fluid_by_name(
        name.utf8().get_data());
    if (def == nullptr) return info;

    info["name"] = def->name;
    info["display_name"] = def->display_name;
    info["chemical_formula"] = def->chemical_formula;
    info["temperature"] = def->temperature;
    info["is_gas"] = def->is_gas;
    return info;
}

int GDFluidNetwork::get_fluid_id(const godot::String& name) {
    return static_cast<int>(FluidRegistry::get_fluid_id(
        name.utf8().get_data()));
}

godot::PackedStringArray GDFluidNetwork::get_all_fluid_names() {
    godot::PackedStringArray arr;
    // Skip index 0 (invalid entry).
    for (size_t i = 1; i < FluidRegistry::get_fluid_count() + 1; ++i) {
        const FluidDefinition* def = FluidRegistry::get_fluid(
            static_cast<FluidId>(i));
        if (def != nullptr) {
            arr.append(def->name);
        }
    }
    return arr;
}

int GDFluidNetwork::get_fluid_count() {
    return static_cast<int>(FluidRegistry::get_fluid_count());
}

// --- Conversion helpers ---

gt::MapPosition GDFluidNetwork::_from_godot(godot::Vector2i pos) {
    return gt::MapPosition{pos.x, pos.y};
}

godot::Vector2i GDFluidNetwork::_to_godot(const gt::MapPosition& pos) {
    return godot::Vector2i(pos.x, pos.y);
}

} // namespace science_and_theology