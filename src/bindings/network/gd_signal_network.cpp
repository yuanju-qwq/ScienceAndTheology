#include "gd_signal_network.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDSignalNetwork::GDSignalNetwork() = default;
GDSignalNetwork::~GDSignalNetwork() = default;

// --- Wire block lifecycle ---

void GDSignalNetwork::add_wire(godot::Vector3i position) {
    network_.add_wire(_from_godot(position));
}

void GDSignalNetwork::remove_wire(godot::Vector3i position) {
    network_.remove_wire(_from_godot(position));
}

bool GDSignalNetwork::has_wire(godot::Vector3i position) const {
    return network_.has_wire(_from_godot(position));
}

int64_t GDSignalNetwork::get_wire_count() const {
    return static_cast<int64_t>(network_.wire_count());
}

// --- Signal sources ---

void GDSignalNetwork::set_source(godot::Vector3i position, int32_t strength) {
    network_.set_source(_from_godot(position), strength);
}

void GDSignalNetwork::remove_source(godot::Vector3i position) {
    network_.remove_source(_from_godot(position));
}

int32_t GDSignalNetwork::get_source_strength(godot::Vector3i position) const {
    return network_.get_source_strength(_from_godot(position));
}

// --- Network recomputation ---

void GDSignalNetwork::update_network() {
    network_.update_network();
}

// --- Signal queries ---

int32_t GDSignalNetwork::get_signal_at(godot::Vector3i position) const {
    return network_.get_signal_at(_from_godot(position));
}

bool GDSignalNetwork::is_powered(godot::Vector3i position) const {
    return network_.is_powered(_from_godot(position));
}

godot::Array GDSignalNetwork::get_powered_positions() const {
    godot::Array out;
    auto powered = network_.powered_positions();
    for (size_t i = 0; i < powered.size(); ++i) {
        out.append(_to_godot(powered[i].first));
    }
    return out;
}

godot::PackedInt32Array GDSignalNetwork::get_powered_strengths() const {
    godot::PackedInt32Array out;
    auto powered = network_.powered_positions();
    out.resize(powered.size());
    for (size_t i = 0; i < powered.size(); ++i) {
        out[i] = powered[i].second;
    }
    return out;
}

// --- Lifecycle ---

void GDSignalNetwork::clear() {
    network_.clear();
}

// --- Binding ---

void GDSignalNetwork::_bind_methods() {
    // Wire lifecycle
    ClassDB::bind_method(D_METHOD("add_wire", "position"),
                         &GDSignalNetwork::add_wire);
    ClassDB::bind_method(D_METHOD("remove_wire", "position"),
                         &GDSignalNetwork::remove_wire);
    ClassDB::bind_method(D_METHOD("has_wire", "position"),
                         &GDSignalNetwork::has_wire);
    ClassDB::bind_method(D_METHOD("get_wire_count"),
                         &GDSignalNetwork::get_wire_count);

    // Sources
    ClassDB::bind_method(D_METHOD("set_source", "position", "strength"),
                         &GDSignalNetwork::set_source);
    ClassDB::bind_method(D_METHOD("remove_source", "position"),
                         &GDSignalNetwork::remove_source);
    ClassDB::bind_method(D_METHOD("get_source_strength", "position"),
                         &GDSignalNetwork::get_source_strength);

    // Recompute
    ClassDB::bind_method(D_METHOD("update_network"),
                         &GDSignalNetwork::update_network);

    // Queries
    ClassDB::bind_method(D_METHOD("get_signal_at", "position"),
                         &GDSignalNetwork::get_signal_at);
    ClassDB::bind_method(D_METHOD("is_powered", "position"),
                         &GDSignalNetwork::is_powered);
    ClassDB::bind_method(D_METHOD("get_powered_positions"),
                         &GDSignalNetwork::get_powered_positions);
    ClassDB::bind_method(D_METHOD("get_powered_strengths"),
                         &GDSignalNetwork::get_powered_strengths);

    ClassDB::bind_method(D_METHOD("clear"), &GDSignalNetwork::clear);

    // Signal emitted when any wire or adjacent consumer changes powered state.
    // (Useful for triggering visual updates in GDScript.)
    ADD_SIGNAL(MethodInfo("network_changed"));
}

// --- Private helpers ---

godot::Vector3i GDSignalNetwork::_to_godot(const gt::MapPosition& pos) {
    return godot::Vector3i(pos.x, pos.y, pos.z);
}

gt::MapPosition GDSignalNetwork::_from_godot(godot::Vector3i pos) {
    return gt::MapPosition{pos.x, pos.y, pos.z};
}

} // namespace science_and_theology
