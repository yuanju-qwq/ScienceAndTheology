#include "gd_power_network.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/config/gt_values.hpp"

// Required by BIND_ENUM_CONSTANT for nested enum types.
VARIANT_ENUM_CAST(science_and_theology::GDPowerNetwork::VoltageTierConst)
VARIANT_ENUM_CAST(science_and_theology::GDPowerNetwork::OverloadStateConst)

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDPowerNetwork::GDPowerNetwork() {
    network_.set_overload_callback([this](PowerNodeId node_id,
                                           const OverloadInfo& info) {
        _on_overload(node_id, info);
    });
}

GDPowerNetwork::~GDPowerNetwork() = default;

void GDPowerNetwork::_bind_methods() {
    // Node lifecycle
    ClassDB::bind_method(D_METHOD("add_node", "tier", "position"),
                         &GDPowerNetwork::add_node);
    ClassDB::bind_method(D_METHOD("remove_node", "node_id"),
                         &GDPowerNetwork::remove_node);
    ClassDB::bind_method(D_METHOD("get_node_info", "node_id"),
                         &GDPowerNetwork::get_node_info);
    ClassDB::bind_method(D_METHOD("get_node_at", "position"),
                         &GDPowerNetwork::get_node_at);
    ClassDB::bind_method(D_METHOD("get_node_count"),
                         &GDPowerNetwork::get_node_count);

    // Edge lifecycle
    ClassDB::bind_method(D_METHOD("connect_nodes", "node_a", "node_b", "cable_material"),
                         &GDPowerNetwork::connect_nodes);
    ClassDB::bind_method(D_METHOD("disconnect_nodes", "node_a", "node_b"),
                         &GDPowerNetwork::disconnect_nodes);
    ClassDB::bind_method(D_METHOD("get_edges_for_node", "node_id"),
                         &GDPowerNetwork::get_edges_for_node);

    // Topology
    ClassDB::bind_method(D_METHOD("update_network"),
                         &GDPowerNetwork::update_network);
    ClassDB::bind_method(D_METHOD("find_connected_component", "start_id"),
                         &GDPowerNetwork::find_connected_component);
    ClassDB::bind_method(D_METHOD("find_all_components"),
                         &GDPowerNetwork::find_all_components);
    ClassDB::bind_method(D_METHOD("are_in_same_network", "node_a", "node_b"),
                         &GDPowerNetwork::are_in_same_network);
    ClassDB::bind_method(D_METHOD("are_connected", "node_a", "node_b"),
                         &GDPowerNetwork::are_connected);

    // Power state
    ClassDB::bind_method(D_METHOD("set_power_demand", "node_id", "demand"),
                         &GDPowerNetwork::set_power_demand);
    ClassDB::bind_method(D_METHOD("set_generation_capacity", "node_id", "capacity"),
                         &GDPowerNetwork::set_generation_capacity);
    ClassDB::bind_method(D_METHOD("is_overloaded", "node_id"),
                         &GDPowerNetwork::is_overloaded);
    ClassDB::bind_method(D_METHOD("get_overload_info", "node_id"),
                         &GDPowerNetwork::get_overload_info);
    ClassDB::bind_method(D_METHOD("get_total_power_loss"),
                         &GDPowerNetwork::get_total_power_loss);
    ClassDB::bind_method(D_METHOD("get_total_generation"),
                         &GDPowerNetwork::get_total_generation);
    ClassDB::bind_method(D_METHOD("get_total_demand"),
                         &GDPowerNetwork::get_total_demand);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDPowerNetwork::clear);

    // Static helpers
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_cable_material_info", "name"),
        &GDPowerNetwork::get_cable_material_info);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_all_cable_materials"),
        &GDPowerNetwork::get_all_cable_materials);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_voltage_for_tier", "tier"),
        &GDPowerNetwork::get_voltage_for_tier);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_tier_name", "tier"),
        &GDPowerNetwork::get_tier_name);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("manhattan_dist", "a", "b"),
        &GDPowerNetwork::manhattan_dist);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_transformer_loss_per_step"),
        &GDPowerNetwork::get_transformer_loss_per_step);

    // Signal
    ADD_SIGNAL(MethodInfo("overload_detected",
        PropertyInfo(Variant::INT, "node_id"),
        PropertyInfo(Variant::INT, "overload_state"),
        PropertyInfo(Variant::INT, "actual_load"),
        PropertyInfo(Variant::INT, "max_capacity"),
        PropertyInfo(Variant::INT, "actual_voltage"),
        PropertyInfo(Variant::INT, "max_voltage")));

    // Voltage tier constants
    BIND_ENUM_CONSTANT(TIER_ULV);
    BIND_ENUM_CONSTANT(TIER_LV);
    BIND_ENUM_CONSTANT(TIER_MV);
    BIND_ENUM_CONSTANT(TIER_HV);
    BIND_ENUM_CONSTANT(TIER_EV);
    BIND_ENUM_CONSTANT(TIER_IV);
    BIND_ENUM_CONSTANT(TIER_LuV);
    BIND_ENUM_CONSTANT(TIER_ZPM);
    BIND_ENUM_CONSTANT(TIER_UV);
    BIND_ENUM_CONSTANT(TIER_UHV);
    BIND_ENUM_CONSTANT(TIER_UEV);
    BIND_ENUM_CONSTANT(TIER_UIV);
    BIND_ENUM_CONSTANT(TIER_UMV);
    BIND_ENUM_CONSTANT(TIER_UXV);
    BIND_ENUM_CONSTANT(TIER_MAX);

    // Overload state constants
    BIND_ENUM_CONSTANT(OVERLOAD_OK);
    BIND_ENUM_CONSTANT(OVERLOAD_OVER_VOLTAGE);
    BIND_ENUM_CONSTANT(OVERLOAD_OVER_CAPACITY);
}

// --- Node lifecycle ---

int64_t GDPowerNetwork::add_node(int tier, godot::Vector2i position) {
    auto t = static_cast<VoltageTier>(tier);
    PowerNodeId id = network_.add_node(t, _from_godot(position));
    return static_cast<int64_t>(id);
}

bool GDPowerNetwork::remove_node(int64_t node_id) {
    return network_.remove_node(static_cast<PowerNodeId>(node_id));
}

godot::Dictionary GDPowerNetwork::get_node_info(int64_t node_id) const {
    godot::Dictionary info;
    const PowerNode* node = network_.get_node(static_cast<PowerNodeId>(node_id));
    if (node == nullptr) return info;

    info["id"] = static_cast<int64_t>(node->id);
    info["tier"] = static_cast<int>(node->tier);
    info["position"] = _to_godot(node->position);
    info["generation_capacity"] = node->generation_capacity;
    info["max_input_voltage"] = node->max_input_voltage;
    info["power_demand"] = node->power_demand;
    info["is_transformer"] = node->is_transformer;
    info["transformer_output_tier"] = static_cast<int>(node->transformer_output_tier);
    info["max_step"] = node->max_step;
    info["is_overloaded"] = node->overload_info.state != OverloadState::OK;
    return info;
}

int64_t GDPowerNetwork::get_node_at(godot::Vector2i position) const {
    return static_cast<int64_t>(network_.get_node_at(_from_godot(position)));
}

int64_t GDPowerNetwork::get_node_count() const {
    return static_cast<int64_t>(network_.node_count());
}

// --- Edge lifecycle ---

bool GDPowerNetwork::connect_nodes(int64_t node_a, int64_t node_b,
                                    const godot::String& cable_material) {
    const CableProperties* cable = _find_cable(cable_material);
    if (cable == nullptr) {
        UtilityFunctions::push_warning(
            "GDPowerNetwork: Unknown cable material: ", cable_material);
        return false;
    }
    return network_.connect(static_cast<PowerNodeId>(node_a),
                             static_cast<PowerNodeId>(node_b), *cable);
}

bool GDPowerNetwork::disconnect_nodes(int64_t node_a, int64_t node_b) {
    return network_.disconnect(static_cast<PowerNodeId>(node_a),
                                static_cast<PowerNodeId>(node_b));
}

godot::Array GDPowerNetwork::get_edges_for_node(int64_t node_id) const {
    godot::Array result;
    auto edges = network_.get_edges_for_node(static_cast<PowerNodeId>(node_id));
    for (const PowerEdge* edge : edges) {
        godot::Dictionary edge_info;
        edge_info["node_a"] = static_cast<int64_t>(edge->node_a);
        edge_info["node_b"] = static_cast<int64_t>(edge->node_b);
        edge_info["cable_material"] = edge->cable.material_name;
        edge_info["max_capacity"] = edge->max_capacity;
        edge_info["distance_tiles"] = edge->distance_tiles;
        edge_info["power_loss"] = edge->power_loss;
        edge_info["current_load"] = edge->current_load;
        edge_info["is_overloaded"] = edge->overload_info.state != OverloadState::OK;
        edge_info["overload_info"] = _to_godot(edge->overload_info);
        result.append(edge_info);
    }
    return result;
}

// --- Topology ---

void GDPowerNetwork::update_network() {
    network_.update_network();
}

godot::PackedInt32Array GDPowerNetwork::find_connected_component(int64_t start_id) const {
    auto comp = network_.find_connected_component(static_cast<PowerNodeId>(start_id));
    godot::PackedInt32Array arr;
    for (auto id : comp) {
        arr.append(static_cast<int32_t>(id));
    }
    return arr;
}

godot::Array GDPowerNetwork::find_all_components() const {
    godot::Array result;
    auto components = network_.find_all_components();
    for (const auto& comp : components) {
        godot::PackedInt32Array arr;
        for (auto id : comp) {
            arr.append(static_cast<int32_t>(id));
        }
        result.append(arr);
    }
    return result;
}

bool GDPowerNetwork::are_in_same_network(int64_t node_a, int64_t node_b) const {
    return network_.are_in_same_network(static_cast<PowerNodeId>(node_a),
                                         static_cast<PowerNodeId>(node_b));
}

bool GDPowerNetwork::are_connected(int64_t node_a, int64_t node_b) const {
    return network_.are_connected(static_cast<PowerNodeId>(node_a),
                                   static_cast<PowerNodeId>(node_b));
}

// --- Power state ---

void GDPowerNetwork::set_power_demand(int64_t node_id, int64_t demand) {
    network_.set_power_demand(static_cast<PowerNodeId>(node_id), demand);
}

void GDPowerNetwork::set_generation_capacity(int64_t node_id, int64_t capacity) {
    network_.set_generation_capacity(static_cast<PowerNodeId>(node_id), capacity);
}

bool GDPowerNetwork::is_overloaded(int64_t node_id) const {
    return network_.is_overloaded(static_cast<PowerNodeId>(node_id));
}

godot::Dictionary GDPowerNetwork::get_overload_info(int64_t node_id) const {
    return _to_godot(network_.get_overload_info(static_cast<PowerNodeId>(node_id)));
}

int64_t GDPowerNetwork::get_total_power_loss() const {
    return network_.get_total_power_loss();
}

int64_t GDPowerNetwork::get_total_generation() const {
    return network_.get_total_generation();
}

int64_t GDPowerNetwork::get_total_demand() const {
    return network_.get_total_demand();
}

void GDPowerNetwork::clear() {
    network_.clear();
}

// --- Static helpers ---

godot::Dictionary GDPowerNetwork::get_cable_material_info(const godot::String& name) {
    godot::Dictionary info;
    const CableProperties* cable = _find_cable(name);
    if (cable == nullptr) return info;

    info["material_name"] = cable->material_name;
    info["max_voltage_tier"] = static_cast<int>(cable->max_voltage_tier);
    info["tier_name"] = get_voltage_name(cable->max_voltage_tier);
    info["max_voltage"] = get_voltage(cable->max_voltage_tier);
    info["amperage"] = cable->amperage;
    info["capacity"] = get_cable_capacity(*cable);
    info["loss_per_tile"] = cable->loss_per_tile;
    return info;
}

godot::PackedStringArray GDPowerNetwork::get_all_cable_materials() {
    godot::PackedStringArray arr;
    for (size_t i = 0; i < kCableMaterialCount; ++i) {
        arr.append(kCableMaterials[i].material_name);
    }
    return arr;
}

int64_t GDPowerNetwork::get_voltage_for_tier(int tier) {
    return get_voltage(static_cast<VoltageTier>(tier));
}

godot::String GDPowerNetwork::get_tier_name(int tier) {
    return get_voltage_name(static_cast<VoltageTier>(tier));
}

int64_t GDPowerNetwork::manhattan_dist(godot::Vector2i a, godot::Vector2i b) {
    // Bindings still use 2D; z=0 until v28-5 migrates to Vector3i.
    return manhattan_distance(a.x, a.y, 0, b.x, b.y, 0);
}

int64_t GDPowerNetwork::get_transformer_loss_per_step() {
    return PowerNode::kTransformerLossPerStep;
}

// --- Callback ---

void GDPowerNetwork::_on_overload(PowerNodeId node_id, const OverloadInfo& info) {
    emit_signal("overload_detected",
        static_cast<int64_t>(node_id),
        static_cast<int>(info.state),
        info.actual_load,
        info.max_capacity,
        info.actual_voltage,
        info.max_voltage);
}

// --- Type conversion helpers ---

godot::Vector2i GDPowerNetwork::_to_godot(const MapPosition& pos) {
    return godot::Vector2i(pos.x, pos.y);
}

MapPosition GDPowerNetwork::_from_godot(godot::Vector2i pos) {
    return MapPosition{pos.x, pos.y};
}

godot::Dictionary GDPowerNetwork::_to_godot(const OverloadInfo& info) {
    godot::Dictionary d;
    d["state"] = static_cast<int>(info.state);
    d["actual_load"] = info.actual_load;
    d["max_capacity"] = info.max_capacity;
    d["actual_voltage"] = info.actual_voltage;
    d["max_voltage"] = info.max_voltage;
    return d;
}

const CableProperties* GDPowerNetwork::_find_cable(const godot::String& name) {
    godot::CharString utf8 = name.utf8();
    for (size_t i = 0; i < kCableMaterialCount; ++i) {
        // Case-insensitive comparison for GDScript convenience.
        godot::String mat_name(kCableMaterials[i].material_name);
        if (name.to_lower() == mat_name.to_lower()) {
            return &kCableMaterials[i];
        }
    }
    return nullptr;
}

} // namespace science_and_theology
