#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "core/sfm/sfm_manager.hpp"
#include "core/sfm/i_container_access.hpp"

#include <memory>
#include <string>

namespace science_and_theology {

// ============================================================
// CallableContainerAccess — IContainerAccess backed by GDScript
// ============================================================
//
// Bridges C++ IContainerAccess calls to a GDScript Callable.
// The callable receives (method_name: String, ...) and returns
// the corresponding value. This lets GDScript provide adapters
// for any container type (furnace, chest, machine, tank, ...)
// without C++ knowing each container's internals.

class CallableContainerAccess : public sfm::IContainerAccess {
public:
    CallableContainerAccess(std::string display_name, godot::Callable callable)
        : display_name_(std::move(display_name)), callable_(std::move(callable)) {}

    sfm::ContainerId get_id() const override { return id_; }
    void set_id(sfm::ContainerId id) { id_ = id; }

    std::string get_display_name() const override { return display_name_; }

    bool has_items() const override {
        return call_bool("has_items");
    }
    bool has_fluids() const override {
        return call_bool("has_fluids");
    }
    bool has_energy() const override {
        return call_bool("has_energy");
    }
    bool has_redstone() const override {
        return call_bool("has_redstone");
    }

    int64_t count_item(gt::ItemId item_id) const override {
        return call_int("count_item", static_cast<int64_t>(item_id));
    }
    int64_t count_total_items() const override {
        return call_int("count_total_items");
    }
    int64_t extract_item(gt::ItemId item_id, int64_t count) override {
        return call_int("extract_item", static_cast<int64_t>(item_id), count);
    }
    int64_t insert_item(gt::ItemId item_id, int64_t count) override {
        return call_int("insert_item", static_cast<int64_t>(item_id), count);
    }
    std::vector<sfm::FlowItemEntry> list_items() const override;

    int64_t count_fluid(gt::FluidId fluid_id) const override {
        return call_int("count_fluid", static_cast<int64_t>(fluid_id));
    }
    int64_t extract_fluid(gt::FluidId fluid_id, int64_t amount_mb) override {
        return call_int("extract_fluid", static_cast<int64_t>(fluid_id), amount_mb);
    }
    int64_t insert_fluid(gt::FluidId fluid_id, int64_t amount_mb) override {
        return call_int("insert_fluid", static_cast<int64_t>(fluid_id), amount_mb);
    }
    std::vector<sfm::FlowFluidEntry> list_fluids() const override;

    int64_t get_energy_stored() const override {
        return call_int("get_energy_stored");
    }
    int64_t get_energy_capacity() const override {
        return call_int("get_energy_capacity");
    }
    int64_t extract_energy(int64_t amount) override {
        return call_int("extract_energy", amount);
    }
    int64_t insert_energy(int64_t amount) override {
        return call_int("insert_energy", amount);
    }

    int32_t get_redstone_signal() const override {
        return static_cast<int32_t>(call_int("get_redstone_signal"));
    }
    void set_redstone_signal(int32_t signal) override {
        callable_.call("set_redstone_signal", signal);
    }

private:
    bool call_bool(const godot::String& method) const {
        godot::Variant ret = callable_.call(method);
        return ret.operator bool();
    }
    int64_t call_int(const godot::String& method) const {
        godot::Variant ret = callable_.call(method);
        return static_cast<int64_t>(ret);
    }
    int64_t call_int(const godot::String& method, int64_t a) const {
        godot::Variant ret = callable_.call(method, a);
        return static_cast<int64_t>(ret);
    }
    int64_t call_int(const godot::String& method, int64_t a, int64_t b) const {
        godot::Variant ret = callable_.call(method, a, b);
        return static_cast<int64_t>(ret);
    }

    sfm::ContainerId id_ = sfm::kInvalidContainerId;
    std::string display_name_;
    godot::Callable callable_;
};

// ============================================================
// GDFlowManager — Godot binding for SFMManager
// ============================================================

class GDFlowManager : public godot::Resource {
    GDCLASS(GDFlowManager, godot::Resource)

public:
    GDFlowManager();
    ~GDFlowManager() override;

    // --- Node operations ---
    int64_t add_node(int64_t type);
    bool remove_node(int64_t node_id);
    godot::Dictionary get_node_info(int64_t node_id) const;
    godot::Array get_all_nodes() const;
    void set_node_position(int64_t node_id, double x, double y);
    godot::Vector2 get_node_position(int64_t node_id) const;
    void set_node_param(int64_t node_id, const godot::String& key,
                        const godot::String& value);
    godot::String get_node_param(int64_t node_id, const godot::String& key) const;

    // --- Connection operations ---
    int64_t connect_nodes(int64_t from_node, int64_t from_port,
                          int64_t to_node, int64_t to_port);
    bool disconnect(int64_t conn_id);
    godot::Array get_all_connections() const;

    // --- Filter operations ---
    void set_item_filter(int64_t node_id, int64_t mode,
                         const godot::PackedInt32Array& item_ids);
    godot::Dictionary get_item_filter(int64_t node_id) const;
    void set_fluid_filter(int64_t node_id, int64_t mode,
                          const godot::PackedInt32Array& fluid_ids);
    godot::Dictionary get_fluid_filter(int64_t node_id) const;

    // --- Variable operations ---
    int64_t add_variable(const godot::String& name, int64_t type);
    bool remove_variable(int64_t var_id);
    godot::Array get_variables() const;

    // --- Container operations ---
    int64_t register_scripted_container(const godot::String& display_name,
                                        const godot::Callable& callable);
    bool unregister_container(int64_t index);
    godot::Array get_containers() const;

    // --- Cable operations ---
    void add_cable(godot::Vector3i pos);
    void remove_cable(godot::Vector3i pos);
    godot::Array get_cables() const;
    void set_manager_position(godot::Vector3i pos);
    godot::Vector3i get_manager_position() const;

    // --- Simulation ---
    void tick(int64_t current_tick);
    bool was_triggered_last_tick() const;
    int64_t get_last_execution_node_count() const;

    // --- Serialization ---
    godot::String serialize() const;
    bool deserialize(const godot::String& data);

    // --- Static helpers ---
    static godot::String get_node_type_name(int64_t type);
    static godot::Dictionary get_node_type_ports(int64_t type);

protected:
    static void _bind_methods();

private:
    std::unique_ptr<sfm::SFMManager> manager_;

    static gt::MapPosition _from_godot(godot::Vector3i pos) {
        return gt::MapPosition{pos.x, pos.y, pos.z};
    }
    static godot::Vector3i _to_godot(const gt::MapPosition& pos) {
        return godot::Vector3i(pos.x, pos.y, pos.z);
    }
};

} // namespace science_and_theology
