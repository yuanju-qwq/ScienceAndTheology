#include "gd_sfm_manager.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/sfm/flow_node_factory.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;
using namespace science_and_theology::sfm;

// ============================================================
// CallableContainerAccess — list methods
// ============================================================

std::vector<FlowItemEntry> CallableContainerAccess::list_items() const {
    std::vector<FlowItemEntry> result;
    Variant ret = callable_.call("list_items");
    if (ret.get_type() == Variant::ARRAY) {
        Array arr = ret;
        for (int i = 0; i < arr.size(); ++i) {
            Dictionary d = arr[i];
            FlowItemEntry entry;
            entry.item_id = static_cast<ItemId>(static_cast<int64_t>(d.get("item_id", 0)));
            entry.count = static_cast<int64_t>(d.get("count", 0));
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<FlowFluidEntry> CallableContainerAccess::list_fluids() const {
    std::vector<FlowFluidEntry> result;
    Variant ret = callable_.call("list_fluids");
    if (ret.get_type() == Variant::ARRAY) {
        Array arr = ret;
        for (int i = 0; i < arr.size(); ++i) {
            Dictionary d = arr[i];
            FlowFluidEntry entry;
            entry.fluid_id = static_cast<FluidId>(static_cast<int64_t>(d.get("fluid_id", 0)));
            entry.amount_mb = static_cast<int64_t>(d.get("amount_mb", 0));
            result.push_back(entry);
        }
    }
    return result;
}

// ============================================================
// GDFlowManager — lifecycle
// ============================================================

GDFlowManager::GDFlowManager()
    : manager_(std::make_unique<SFMManager>()) {}

GDFlowManager::~GDFlowManager() = default;

void GDFlowManager::_bind_methods() {
    // --- Node operations ---
    ClassDB::bind_method(D_METHOD("add_node", "type"),
                         &GDFlowManager::add_node);
    ClassDB::bind_method(D_METHOD("remove_node", "node_id"),
                         &GDFlowManager::remove_node);
    ClassDB::bind_method(D_METHOD("get_node_info", "node_id"),
                         &GDFlowManager::get_node_info);
    ClassDB::bind_method(D_METHOD("get_all_nodes"),
                         &GDFlowManager::get_all_nodes);
    ClassDB::bind_method(D_METHOD("set_node_position", "node_id", "x", "y"),
                         &GDFlowManager::set_node_position);
    ClassDB::bind_method(D_METHOD("get_node_position", "node_id"),
                         &GDFlowManager::get_node_position);
    ClassDB::bind_method(D_METHOD("set_node_param", "node_id", "key", "value"),
                         &GDFlowManager::set_node_param);
    ClassDB::bind_method(D_METHOD("get_node_param", "node_id", "key"),
                         &GDFlowManager::get_node_param);

    // --- Connection operations ---
    ClassDB::bind_method(D_METHOD("connect_nodes", "from_node", "from_port",
                                   "to_node", "to_port"),
                         &GDFlowManager::connect_nodes);
    ClassDB::bind_method(D_METHOD("disconnect", "conn_id"),
                         &GDFlowManager::disconnect);
    ClassDB::bind_method(D_METHOD("get_all_connections"),
                         &GDFlowManager::get_all_connections);

    // --- Filter operations ---
    ClassDB::bind_method(D_METHOD("set_item_filter", "node_id", "mode", "item_ids"),
                         &GDFlowManager::set_item_filter);
    ClassDB::bind_method(D_METHOD("get_item_filter", "node_id"),
                         &GDFlowManager::get_item_filter);
    ClassDB::bind_method(D_METHOD("set_fluid_filter", "node_id", "mode", "fluid_ids"),
                         &GDFlowManager::set_fluid_filter);
    ClassDB::bind_method(D_METHOD("get_fluid_filter", "node_id"),
                         &GDFlowManager::get_fluid_filter);

    // --- Variable operations ---
    ClassDB::bind_method(D_METHOD("add_variable", "name", "type"),
                         &GDFlowManager::add_variable);
    ClassDB::bind_method(D_METHOD("remove_variable", "var_id"),
                         &GDFlowManager::remove_variable);
    ClassDB::bind_method(D_METHOD("get_variables"),
                         &GDFlowManager::get_variables);

    // --- Container operations ---
    ClassDB::bind_method(D_METHOD("register_scripted_container", "display_name", "callable"),
                         &GDFlowManager::register_scripted_container);
    ClassDB::bind_method(D_METHOD("unregister_container", "index"),
                         &GDFlowManager::unregister_container);
    ClassDB::bind_method(D_METHOD("get_containers"),
                         &GDFlowManager::get_containers);

    // --- Cable operations ---
    ClassDB::bind_method(D_METHOD("add_cable", "pos"),
                         &GDFlowManager::add_cable);
    ClassDB::bind_method(D_METHOD("remove_cable", "pos"),
                         &GDFlowManager::remove_cable);
    ClassDB::bind_method(D_METHOD("get_cables"),
                         &GDFlowManager::get_cables);
    ClassDB::bind_method(D_METHOD("set_manager_position", "pos"),
                         &GDFlowManager::set_manager_position);
    ClassDB::bind_method(D_METHOD("get_manager_position"),
                         &GDFlowManager::get_manager_position);

    // --- Simulation ---
    ClassDB::bind_method(D_METHOD("tick", "current_tick"),
                         &GDFlowManager::tick);
    ClassDB::bind_method(D_METHOD("was_triggered_last_tick"),
                         &GDFlowManager::was_triggered_last_tick);
    ClassDB::bind_method(D_METHOD("get_last_execution_node_count"),
                         &GDFlowManager::get_last_execution_node_count);

    // --- Serialization ---
    ClassDB::bind_method(D_METHOD("serialize"),
                         &GDFlowManager::serialize);
    ClassDB::bind_method(D_METHOD("deserialize", "data"),
                         &GDFlowManager::deserialize);

    // --- Static helpers ---
    ClassDB::bind_static_method("GDFlowManager",
        D_METHOD("get_node_type_name", "type"),
        &GDFlowManager::get_node_type_name);
    ClassDB::bind_static_method("GDFlowManager",
        D_METHOD("get_node_type_ports", "type"),
        &GDFlowManager::get_node_type_ports);

    // --- FlowNodeType constants ---
    #define BIND_NODE_TYPE(name, val) \
        ClassDB::bind_integer_constant("GDFlowManager", "", #name, \
            static_cast<int64_t>(sfm::FlowNodeType::val))
    BIND_NODE_TYPE(NODE_TRIGGER_TIMER,     TRIGGER_TIMER);
    BIND_NODE_TYPE(NODE_TRIGGER_REDSTONE,  TRIGGER_REDSTONE);
    BIND_NODE_TYPE(NODE_TRIGGER_ITEM,      TRIGGER_ITEM);
    BIND_NODE_TYPE(NODE_ITEM_INPUT,        ITEM_INPUT);
    BIND_NODE_TYPE(NODE_ITEM_OUTPUT,       ITEM_OUTPUT);
    BIND_NODE_TYPE(NODE_FLUID_INPUT,       FLUID_INPUT);
    BIND_NODE_TYPE(NODE_FLUID_OUTPUT,      FLUID_OUTPUT);
    BIND_NODE_TYPE(NODE_ENERGY_INPUT,      ENERGY_INPUT);
    BIND_NODE_TYPE(NODE_ENERGY_OUTPUT,     ENERGY_OUTPUT);
    BIND_NODE_TYPE(NODE_REDSTONE_INPUT,    REDSTONE_INPUT);
    BIND_NODE_TYPE(NODE_REDSTONE_OUTPUT,   REDSTONE_OUTPUT);
    BIND_NODE_TYPE(NODE_ITEM_FILTER,       ITEM_FILTER);
    BIND_NODE_TYPE(NODE_FLUID_FILTER,      FLUID_FILTER);
    BIND_NODE_TYPE(NODE_CONDITION,         CONDITION);
    BIND_NODE_TYPE(NODE_LOOP,              LOOP);
    BIND_NODE_TYPE(NODE_GROUP_INPUT,       GROUP_INPUT);
    BIND_NODE_TYPE(NODE_GROUP_OUTPUT,      GROUP_OUTPUT);
    BIND_NODE_TYPE(NODE_VARIABLE_GET,      VARIABLE_GET);
    BIND_NODE_TYPE(NODE_VARIABLE_SET,      VARIABLE_SET);
    BIND_NODE_TYPE(NODE_MATH,              MATH);
    BIND_NODE_TYPE(NODE_TEXT_LABEL,        TEXT_LABEL);
    BIND_NODE_TYPE(NODE_COUNT,             COUNT);
    #undef BIND_NODE_TYPE

    // --- FlowPortType constants ---
    #define BIND_PORT_TYPE(name, val) \
        ClassDB::bind_integer_constant("GDFlowManager", "", #name, \
            static_cast<int64_t>(sfm::FlowPortType::val))
    BIND_PORT_TYPE(PORT_NONE,         NONE);
    BIND_PORT_TYPE(PORT_FLOW,         FLOW);
    BIND_PORT_TYPE(PORT_ITEM_STREAM,  ITEM_STREAM);
    BIND_PORT_TYPE(PORT_FLUID_STREAM, FLUID_STREAM);
    BIND_PORT_TYPE(PORT_ENERGY,       ENERGY);
    BIND_PORT_TYPE(PORT_REDSTONE,     REDSTONE);
    BIND_PORT_TYPE(PORT_NUMBER,       NUMBER);
    BIND_PORT_TYPE(PORT_STRING,       STRING);
    BIND_PORT_TYPE(PORT_BOOLEAN,      BOOLEAN);
    #undef BIND_PORT_TYPE

    // --- FilterMode constants ---
    ClassDB::bind_integer_constant("GDFlowManager", "", "FILTER_WHITELIST",
        static_cast<int64_t>(sfm::FilterMode::WHITELIST));
    ClassDB::bind_integer_constant("GDFlowManager", "", "FILTER_BLACKLIST",
        static_cast<int64_t>(sfm::FilterMode::BLACKLIST));
}

// ============================================================
// Node operations
// ============================================================

int64_t GDFlowManager::add_node(int64_t type) {
    FlowNodeId id = manager_->program().add_node(
        static_cast<FlowNodeType>(type));
    return static_cast<int64_t>(id);
}

bool GDFlowManager::remove_node(int64_t node_id) {
    return manager_->program().remove_node(static_cast<FlowNodeId>(node_id));
}

static Dictionary port_to_dict(const FlowPort& port) {
    Dictionary d;
    d["id"] = static_cast<int64_t>(port.id);
    d["type"] = static_cast<int64_t>(port.type);
    d["name"] = String(port.name.c_str());
    d["is_input"] = port.is_input;
    return d;
}

godot::Dictionary GDFlowManager::get_node_info(int64_t node_id) const {
    Dictionary result;
    const FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return result;

    result["id"] = static_cast<int64_t>(node->id);
    result["type"] = static_cast<int64_t>(node->type);
    result["x"] = node->editor_x;
    result["y"] = node->editor_y;

    Array input_ports;
    for (const auto& p : node->input_ports) {
        input_ports.push_back(port_to_dict(p));
    }
    result["input_ports"] = input_ports;

    Array output_ports;
    for (const auto& p : node->output_ports) {
        output_ports.push_back(port_to_dict(p));
    }
    result["output_ports"] = output_ports;

    Dictionary params;
    for (const auto& [k, v] : node->params) {
        params[String(k.c_str())] = String(v.c_str());
    }
    result["params"] = params;

    return result;
}

godot::Array GDFlowManager::get_all_nodes() const {
    Array result;
    for (const auto& [id, node] : manager_->program().nodes()) {
        result.push_back(get_node_info(static_cast<int64_t>(id)));
    }
    return result;
}

void GDFlowManager::set_node_position(int64_t node_id, double x, double y) {
    FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (node) {
        node->editor_x = static_cast<float>(x);
        node->editor_y = static_cast<float>(y);
    }
}

godot::Vector2 GDFlowManager::get_node_position(int64_t node_id) const {
    const FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return Vector2();
    return Vector2(node->editor_x, node->editor_y);
}

void GDFlowManager::set_node_param(int64_t node_id, const godot::String& key,
                                   const godot::String& value) {
    FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (node) {
        node->params[key.utf8().get_data()] = value.utf8().get_data();
    }
}

godot::String GDFlowManager::get_node_param(int64_t node_id,
                                            const godot::String& key) const {
    const FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return String();
    auto it = node->params.find(key.utf8().get_data());
    if (it == node->params.end()) return String();
    return String(it->second.c_str());
}

// ============================================================
// Connection operations
// ============================================================

int64_t GDFlowManager::connect_nodes(int64_t from_node, int64_t from_port,
                                     int64_t to_node, int64_t to_port) {
    FlowConnectionId id = manager_->program().connect(
        static_cast<FlowNodeId>(from_node),
        static_cast<FlowPortId>(from_port),
        static_cast<FlowNodeId>(to_node),
        static_cast<FlowPortId>(to_port));
    return static_cast<int64_t>(id);
}

bool GDFlowManager::disconnect(int64_t conn_id) {
    return manager_->program().disconnect(static_cast<FlowConnectionId>(conn_id));
}

godot::Array GDFlowManager::get_all_connections() const {
    Array result;
    for (const auto& [id, conn] : manager_->program().connections()) {
        Dictionary d;
        d["id"] = static_cast<int64_t>(conn.id);
        d["from_node"] = static_cast<int64_t>(conn.from_node);
        d["from_port"] = static_cast<int64_t>(conn.from_port);
        d["to_node"] = static_cast<int64_t>(conn.to_node);
        d["to_port"] = static_cast<int64_t>(conn.to_port);
        d["port_type"] = static_cast<int64_t>(conn.port_type);
        result.push_back(d);
    }
    return result;
}

// ============================================================
// Filter operations
// ============================================================

void GDFlowManager::set_item_filter(int64_t node_id, int64_t mode,
                                    const godot::PackedInt32Array& item_ids) {
    FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return;
    node->item_filter.mode = static_cast<FilterMode>(mode);
    node->item_filter.item_ids.clear();
    for (int i = 0; i < item_ids.size(); ++i) {
        node->item_filter.item_ids.push_back(static_cast<ItemId>(item_ids[i]));
    }
}

godot::Dictionary GDFlowManager::get_item_filter(int64_t node_id) const {
    Dictionary result;
    const FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return result;
    result["mode"] = static_cast<int64_t>(node->item_filter.mode);
    PackedInt32Array ids;
    for (auto id : node->item_filter.item_ids) {
        ids.push_back(static_cast<int32_t>(id));
    }
    result["item_ids"] = ids;
    return result;
}

void GDFlowManager::set_fluid_filter(int64_t node_id, int64_t mode,
                                     const godot::PackedInt32Array& fluid_ids) {
    FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return;
    node->fluid_filter.mode = static_cast<FilterMode>(mode);
    node->fluid_filter.fluid_ids.clear();
    for (int i = 0; i < fluid_ids.size(); ++i) {
        node->fluid_filter.fluid_ids.push_back(static_cast<FluidId>(fluid_ids[i]));
    }
}

godot::Dictionary GDFlowManager::get_fluid_filter(int64_t node_id) const {
    Dictionary result;
    const FlowNode* node = manager_->program().get_node(static_cast<FlowNodeId>(node_id));
    if (!node) return result;
    result["mode"] = static_cast<int64_t>(node->fluid_filter.mode);
    PackedInt32Array ids;
    for (auto id : node->fluid_filter.fluid_ids) {
        ids.push_back(static_cast<int32_t>(id));
    }
    result["fluid_ids"] = ids;
    return result;
}

// ============================================================
// Variable operations
// ============================================================

int64_t GDFlowManager::add_variable(const godot::String& name, int64_t type) {
    FlowVariableId id = manager_->variables().add_variable(
        name.utf8().get_data(),
        static_cast<FlowPortType>(type));
    return static_cast<int64_t>(id);
}

bool GDFlowManager::remove_variable(int64_t var_id) {
    return manager_->variables().remove_variable(static_cast<FlowVariableId>(var_id));
}

godot::Array GDFlowManager::get_variables() const {
    Array result;
    for (const auto& [id, var] : manager_->variables().variables()) {
        Dictionary d;
        d["id"] = static_cast<int64_t>(id);
        d["name"] = String(var.name.c_str());
        d["type"] = static_cast<int64_t>(var.type);
        result.push_back(d);
    }
    return result;
}

// ============================================================
// Container operations
// ============================================================

int64_t GDFlowManager::register_scripted_container(
        const godot::String& display_name, const godot::Callable& callable) {
    auto access = std::make_unique<CallableContainerAccess>(
        display_name.utf8().get_data(), callable);
    ContainerId id = manager_->containers().register_container(std::move(access));
    return static_cast<int64_t>(id);
}

bool GDFlowManager::unregister_container(int64_t index) {
    return manager_->containers().unregister_container(static_cast<ContainerId>(index));
}

godot::Array GDFlowManager::get_containers() const {
    Array result;
    auto list = manager_->containers().list_containers();
    for (const auto& [index, name] : list) {
        Dictionary d;
        d["index"] = static_cast<int64_t>(index);
        d["name"] = String(name.c_str());
        // Query capabilities via the IContainerAccess interface.
        IContainerAccess* access = manager_->containers().get_by_index(index);
        if (access) {
            d["has_items"] = access->has_items();
            d["has_fluids"] = access->has_fluids();
            d["has_energy"] = access->has_energy();
            d["has_redstone"] = access->has_redstone();
        } else {
            d["has_items"] = false;
            d["has_fluids"] = false;
            d["has_energy"] = false;
            d["has_redstone"] = false;
        }
        result.push_back(d);
    }
    return result;
}

// ============================================================
// Cable operations
// ============================================================

void GDFlowManager::add_cable(godot::Vector3i pos) {
    manager_->add_cable(_from_godot(pos));
}

void GDFlowManager::remove_cable(godot::Vector3i pos) {
    manager_->remove_cable(_from_godot(pos));
}

godot::Array GDFlowManager::get_cables() const {
    Array result;
    for (const auto& pos : manager_->cable_graph().cables()) {
        result.push_back(_to_godot(pos));
    }
    return result;
}

void GDFlowManager::set_manager_position(godot::Vector3i pos) {
    manager_->set_position(_from_godot(pos));
}

godot::Vector3i GDFlowManager::get_manager_position() const {
    return _to_godot(manager_->get_position());
}

// ============================================================
// Simulation
// ============================================================

void GDFlowManager::tick(int64_t current_tick) {
    manager_->tick(current_tick);
}

bool GDFlowManager::was_triggered_last_tick() const {
    return manager_->was_triggered_last_tick();
}

int64_t GDFlowManager::get_last_execution_node_count() const {
    return manager_->get_last_execution_node_count();
}

// ============================================================
// Serialization
// ============================================================

godot::String GDFlowManager::serialize() const {
    return String(manager_->serialize().c_str());
}

bool GDFlowManager::deserialize(const godot::String& data) {
    return manager_->deserialize(data.utf8().get_data());
}

// ============================================================
// Static helpers
// ============================================================

godot::String GDFlowManager::get_node_type_name(int64_t type) {
    return String(FlowNodeFactory::type_name(
        static_cast<FlowNodeType>(type)));
}

godot::Dictionary GDFlowManager::get_node_type_ports(int64_t type) {
    Dictionary result;
    FlowNode proto = FlowNodeFactory::create(0, static_cast<FlowNodeType>(type));

    Array input_ports;
    for (const auto& p : proto.input_ports) {
        input_ports.push_back(port_to_dict(p));
    }
    result["input_ports"] = input_ports;

    Array output_ports;
    for (const auto& p : proto.output_ports) {
        output_ports.push_back(port_to_dict(p));
    }
    result["output_ports"] = output_ports;

    return result;
}

} // namespace science_and_theology
