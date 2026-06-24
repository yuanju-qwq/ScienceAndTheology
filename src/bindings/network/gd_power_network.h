#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "core/network/power_network.hpp"

namespace science_and_theology {

// ============================================================
// GDPowerNetwork — GDExtension wrapper for the per-block power network
// ============================================================
//
// Exposes the C++ PowerNetwork (per-tile cable model) to GDScript.
//
// Usage in GDScript:
//   var net = GDPowerNetwork.new()
//   net.add_cable(Vector3i(10, 5, 0), GDPowerNetwork.TIER_LV)
//   net.set_generator(Vector3i(10, 6, 0), 128, GDPowerNetwork.TIER_LV)
//   net.set_consumer(Vector3i(12, 5, 0), 32, 32)
//   net.update_network()
//   var power = net.get_power_at(Vector3i(12, 5, 0))
//   if net.is_overloaded(Vector3i(10, 5, 0)):
//       print(net.get_overload_info(Vector3i(10, 5, 0)))
class GDPowerNetwork : public godot::Resource {
    GDCLASS(GDPowerNetwork, godot::Resource)

public:
    GDPowerNetwork();
    ~GDPowerNetwork() override;

    // --- Cable block lifecycle ---
    void add_cable(godot::Vector3i position, int tier);
    void remove_cable(godot::Vector3i position);
    bool has_cable(godot::Vector3i position) const;
    int64_t get_cable_count() const;

    // --- Generator / consumer lifecycle ---
    void set_generator(godot::Vector3i position, int64_t capacity, int tier);
    void remove_generator(godot::Vector3i position);
    void set_consumer(godot::Vector3i position, int64_t demand,
                      int64_t max_input_voltage);
    void remove_consumer(godot::Vector3i position);

    // --- Network recomputation ---
    void update_network();

    // --- Power state queries ---
    int64_t get_power_at(godot::Vector3i position) const;
    bool is_overloaded(godot::Vector3i position) const;
    godot::Dictionary get_overload_info(godot::Vector3i position) const;
    int64_t get_total_power_loss() const;
    int64_t get_total_generation() const;
    int64_t get_total_demand() const;
    bool are_in_same_network(godot::Vector3i a, godot::Vector3i b) const;

    // --- Lifecycle ---
    void clear();

    // --- Static helpers for GDScript ---
    static int64_t get_voltage_for_tier(int tier);
    static godot::String get_tier_name(int tier);
    static godot::String get_cable_material_for_tier(int tier);

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

    // Forward the C++ overload callback to a Godot signal.
    void _on_overload(gt::MapPosition pos, const gt::OverloadInfo& info);

    // Convert C++ types to Godot variants.
    static godot::Vector3i _to_godot(const gt::MapPosition& pos);
    static gt::MapPosition _from_godot(godot::Vector3i pos);
    static godot::Dictionary _to_godot(const gt::OverloadInfo& info);
};

} // namespace science_and_theology
