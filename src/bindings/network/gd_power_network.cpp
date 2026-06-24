#include "gd_power_network.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/network/power_network.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDPowerNetwork::GDPowerNetwork() {
    network_.set_overload_callback([this](gt::MapPosition pos,
                                          const gt::OverloadInfo& info) {
        _on_overload(pos, info);
    });
}

GDPowerNetwork::~GDPowerNetwork() = default;

// --- Cable block lifecycle ---

void GDPowerNetwork::add_cable(godot::Vector3i position, int tier) {
    network_.add_cable(_from_godot(position),
                       static_cast<gt::VoltageTier>(tier));
}

void GDPowerNetwork::remove_cable(godot::Vector3i position) {
    network_.remove_cable(_from_godot(position));
}

bool GDPowerNetwork::has_cable(godot::Vector3i position) const {
    return network_.has_cable(_from_godot(position));
}

int64_t GDPowerNetwork::get_cable_count() const {
    return static_cast<int64_t>(network_.cable_count());
}

// --- Generator / consumer lifecycle ---

void GDPowerNetwork::set_generator(godot::Vector3i position, int64_t capacity,
                                    int tier) {
    network_.set_generator(_from_godot(position), capacity,
                           static_cast<gt::VoltageTier>(tier));
}

void GDPowerNetwork::remove_generator(godot::Vector3i position) {
    network_.remove_generator(_from_godot(position));
}

void GDPowerNetwork::set_consumer(godot::Vector3i position, int64_t demand,
                                   int64_t max_input_voltage) {
    network_.set_consumer(_from_godot(position), demand, max_input_voltage);
}

void GDPowerNetwork::remove_consumer(godot::Vector3i position) {
    network_.remove_consumer(_from_godot(position));
}

// --- Network recomputation ---

void GDPowerNetwork::update_network() {
    network_.update_network();
}

// --- Power state queries ---

int64_t GDPowerNetwork::get_power_at(godot::Vector3i position) const {
    return network_.get_power_at(_from_godot(position));
}

bool GDPowerNetwork::is_overloaded(godot::Vector3i position) const {
    return network_.is_overloaded(_from_godot(position));
}

godot::Dictionary GDPowerNetwork::get_overload_info(
        godot::Vector3i position) const {
    return _to_godot(network_.get_overload_info(_from_godot(position)));
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

bool GDPowerNetwork::are_in_same_network(godot::Vector3i a,
                                          godot::Vector3i b) const {
    return network_.are_in_same_network(_from_godot(a), _from_godot(b));
}

// --- Lifecycle ---

void GDPowerNetwork::clear() {
    network_.clear();
}

// --- Static helpers ---

int64_t GDPowerNetwork::get_voltage_for_tier(int tier) {
    return gt::get_voltage(static_cast<gt::VoltageTier>(tier));
}

godot::String GDPowerNetwork::get_tier_name(int tier) {
    return gt::get_voltage_name(static_cast<gt::VoltageTier>(tier));
}

godot::String GDPowerNetwork::get_cable_material_for_tier(int tier) {
    gt::VoltageTier t = static_cast<gt::VoltageTier>(tier);
    for (size_t i = 0; i < gt::kCableMaterialCount; ++i) {
        if (gt::kCableMaterials[i].max_voltage_tier == t) {
            return gt::kCableMaterials[i].material_name;
        }
    }
    return "Unknown";
}

// --- Binding ---

void GDPowerNetwork::_bind_methods() {
    // Cable lifecycle
    ClassDB::bind_method(D_METHOD("add_cable", "position", "tier"),
                         &GDPowerNetwork::add_cable);
    ClassDB::bind_method(D_METHOD("remove_cable", "position"),
                         &GDPowerNetwork::remove_cable);
    ClassDB::bind_method(D_METHOD("has_cable", "position"),
                         &GDPowerNetwork::has_cable);
    ClassDB::bind_method(D_METHOD("get_cable_count"),
                         &GDPowerNetwork::get_cable_count);

    // Generator / consumer
    ClassDB::bind_method(D_METHOD("set_generator", "position", "capacity", "tier"),
                         &GDPowerNetwork::set_generator);
    ClassDB::bind_method(D_METHOD("remove_generator", "position"),
                         &GDPowerNetwork::remove_generator);
    ClassDB::bind_method(D_METHOD("set_consumer", "position", "demand",
                                  "max_input_voltage"),
                         &GDPowerNetwork::set_consumer);
    ClassDB::bind_method(D_METHOD("remove_consumer", "position"),
                         &GDPowerNetwork::remove_consumer);

    // Recompute
    ClassDB::bind_method(D_METHOD("update_network"),
                         &GDPowerNetwork::update_network);

    // Queries
    ClassDB::bind_method(D_METHOD("get_power_at", "position"),
                         &GDPowerNetwork::get_power_at);
    ClassDB::bind_method(D_METHOD("is_overloaded", "position"),
                         &GDPowerNetwork::is_overloaded);
    ClassDB::bind_method(D_METHOD("get_overload_info", "position"),
                         &GDPowerNetwork::get_overload_info);
    ClassDB::bind_method(D_METHOD("get_total_power_loss"),
                         &GDPowerNetwork::get_total_power_loss);
    ClassDB::bind_method(D_METHOD("get_total_generation"),
                         &GDPowerNetwork::get_total_generation);
    ClassDB::bind_method(D_METHOD("get_total_demand"),
                         &GDPowerNetwork::get_total_demand);
    ClassDB::bind_method(D_METHOD("are_in_same_network", "a", "b"),
                         &GDPowerNetwork::are_in_same_network);

    ClassDB::bind_method(D_METHOD("clear"), &GDPowerNetwork::clear);

    // Static helpers
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_voltage_for_tier", "tier"),
        &GDPowerNetwork::get_voltage_for_tier);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_tier_name", "tier"),
        &GDPowerNetwork::get_tier_name);
    ClassDB::bind_static_method("GDPowerNetwork",
        D_METHOD("get_cable_material_for_tier", "tier"),
        &GDPowerNetwork::get_cable_material_for_tier);

    // Voltage tier constants
    BIND_CONSTANT(TIER_ULV);
    BIND_CONSTANT(TIER_LV);
    BIND_CONSTANT(TIER_MV);
    BIND_CONSTANT(TIER_HV);
    BIND_CONSTANT(TIER_EV);
    BIND_CONSTANT(TIER_IV);
    BIND_CONSTANT(TIER_LuV);
    BIND_CONSTANT(TIER_ZPM);
    BIND_CONSTANT(TIER_UV);
    BIND_CONSTANT(TIER_UHV);
    BIND_CONSTANT(TIER_UEV);
    BIND_CONSTANT(TIER_UIV);
    BIND_CONSTANT(TIER_UMV);
    BIND_CONSTANT(TIER_UXV);
    BIND_CONSTANT(TIER_MAX);

    // Overload state constants
    BIND_CONSTANT(OVERLOAD_OK);
    BIND_CONSTANT(OVERLOAD_OVER_VOLTAGE);
    BIND_CONSTANT(OVERLOAD_OVER_CAPACITY);

    // Signal emitted when a cable or consumer enters overload.
    ADD_SIGNAL(MethodInfo("overload_detected",
        PropertyInfo(Variant::VECTOR3I, "position"),
        PropertyInfo(Variant::DICTIONARY, "overload_info")));
}

// --- Private helpers ---

void GDPowerNetwork::_on_overload(gt::MapPosition pos,
                                    const gt::OverloadInfo& info) {
    emit_signal("overload_detected", _to_godot(pos), _to_godot(info));
}

godot::Vector3i GDPowerNetwork::_to_godot(const gt::MapPosition& pos) {
    return godot::Vector3i(pos.x, pos.y, pos.z);
}

gt::MapPosition GDPowerNetwork::_from_godot(godot::Vector3i pos) {
    return gt::MapPosition{pos.x, pos.y, pos.z};
}

godot::Dictionary GDPowerNetwork::_to_godot(const gt::OverloadInfo& info) {
    godot::Dictionary d;
    d["state"] = static_cast<int64_t>(info.state);
    d["actual_load"] = info.actual_load;
    d["max_capacity"] = info.max_capacity;
    d["actual_voltage"] = info.actual_voltage;
    d["max_voltage"] = info.max_voltage;
    return d;
}

} // namespace science_and_theology
