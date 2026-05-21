#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/gt/power_network.hpp"

namespace science_and_theology {

// GDExtension wrapper for the GT power network.
// Exposes the C++ PowerNetwork to GDScript with proper type conversions.
//
// Usage in GDScript:
//   var net = GDPowerNetwork.new()
//   var node_id = net.add_node(GDPowerNetwork.TIER_LV, Vector2i(10, 5))
//   net.connect_nodes(a, b, "copper")
//   net.update_network()
//   if net.is_overloaded(node_id):
//       print(net.get_overload_info(node_id))
class GDPowerNetwork : public godot::Resource {
    GDCLASS(GDPowerNetwork, godot::Resource)

public:
    GDPowerNetwork();
    ~GDPowerNetwork() override;

    // --- Node lifecycle ---
    int64_t add_node(int tier, godot::Vector2i position);
    bool remove_node(int64_t node_id);
    godot::Dictionary get_node_info(int64_t node_id) const;
    int64_t get_node_at(godot::Vector2i position) const;
    int64_t get_node_count() const;

    // --- Edge lifecycle ---
    bool connect_nodes(int64_t node_a, int64_t node_b, const godot::String& cable_material);
    bool disconnect_nodes(int64_t node_a, int64_t node_b);
    godot::Array get_edges_for_node(int64_t node_id) const;

    // --- Network topology ---
    void update_network();
    godot::PackedInt32Array find_connected_component(int64_t start_id) const;
    godot::Array find_all_components() const;
    bool are_in_same_network(int64_t node_a, int64_t node_b) const;
    bool are_connected(int64_t node_a, int64_t node_b) const;

    // --- Power state ---
    void set_power_demand(int64_t node_id, int64_t demand);
    void set_generation_capacity(int64_t node_id, int64_t capacity);
    bool is_overloaded(int64_t node_id) const;
    godot::Dictionary get_overload_info(int64_t node_id) const;
    int64_t get_total_power_loss() const;
    int64_t get_total_generation() const;
    int64_t get_total_demand() const;

    // --- Lifecycle ---
    void clear();

    // --- Static helpers for GDScript ---
    static godot::Dictionary get_cable_material_info(const godot::String& name);
    static godot::PackedStringArray get_all_cable_materials();
    static int64_t get_voltage_for_tier(int tier);
    static godot::String get_tier_name(int tier);
    static int64_t manhattan_dist(godot::Vector2i a, godot::Vector2i b);

    // --- Voltage tier constants for GDScript ---
    enum VoltageTierConst {
        TIER_ULV = 0,
        TIER_LV,
        TIER_MV,
        TIER_HV,
        TIER_EV,
        TIER_IV,
        TIER_LuV,
        TIER_ZPM,
        TIER_UV,
        TIER_UHV,
        TIER_UEV,
        TIER_UIV,
        TIER_UMV,
        TIER_UXV,
        TIER_MAX,
    };

    // Overload state constants for GDScript signal handling.
    enum OverloadStateConst {
        OVERLOAD_OK = 0,
        OVERLOAD_OVER_VOLTAGE = 1,
        OVERLOAD_OVER_CAPACITY = 2,
    };

protected:
    static void _bind_methods();

private:
    gt::PowerNetwork network_;

    // Forward the C++ overload callback to Godot signals.
    void _on_overload(gt::PowerNodeId node_id, const gt::OverloadInfo& info);

    // Convert C++ types to Godot variants.
    static godot::Vector2i _to_godot(const gt::MapPosition& pos);
    static gt::MapPosition _from_godot(godot::Vector2i pos);
    static godot::Dictionary _to_godot(const gt::OverloadInfo& info);
    static const gt::CableProperties* _find_cable(const godot::String& name);
};

} // namespace science_and_theology
