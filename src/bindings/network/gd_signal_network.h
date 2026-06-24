#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "core/network/signal_network.hpp"

namespace science_and_theology {

// ============================================================
// GDSignalNetwork — GDExtension wrapper for the per-block signal network
// ============================================================
//
// Exposes the C++ SignalNetwork (per-tile digital wire model) to GDScript.
//
// Usage in GDScript:
//   var net = GDSignalNetwork.new()
//   net.add_wire(Vector3i(10, 5, 0))
//   net.add_wire(Vector3i(11, 5, 0))
//   net.set_source(Vector3i(10, 6, 0), 1)  # button above wire
//   net.update_network()
//   if net.is_powered(Vector3i(11, 5, 0)):
//       print("signal reaches second wire")
//   if net.is_powered(Vector3i(12, 5, 0)):
//       print("signal reaches consumer adjacent to wire group")
class GDSignalNetwork : public godot::Resource {
    GDCLASS(GDSignalNetwork, godot::Resource)

public:
    GDSignalNetwork();
    ~GDSignalNetwork() override;

    // --- Wire block lifecycle ---
    void add_wire(godot::Vector3i position);
    void remove_wire(godot::Vector3i position);
    bool has_wire(godot::Vector3i position) const;
    int64_t get_wire_count() const;

    // --- Signal sources ---
    void set_source(godot::Vector3i position, int32_t strength);
    void remove_source(godot::Vector3i position);
    int32_t get_source_strength(godot::Vector3i position) const;

    // --- Network recomputation ---
    void update_network();

    // --- Signal queries ---
    int32_t get_signal_at(godot::Vector3i position) const;
    bool is_powered(godot::Vector3i position) const;

    // Returns all positions currently receiving a non-zero signal as
    // parallel arrays (positions, strengths) for easy iteration in GDScript.
    godot::Array get_powered_positions() const;
    godot::PackedInt32Array get_powered_strengths() const;

    // --- Lifecycle ---
    void clear();

protected:
    static void _bind_methods();

private:
    gt::SignalNetwork network_;

    // Convert C++ types to Godot variants.
    static godot::Vector3i _to_godot(const gt::MapPosition& pos);
    static gt::MapPosition _from_godot(godot::Vector3i pos);
};

} // namespace science_and_theology
