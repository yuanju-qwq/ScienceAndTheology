#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "voxel_network_graph.hpp"

namespace science_and_theology::gt {

// ============================================================
// SignalNetwork — per-block digital signal conduction network
// ============================================================
//
// A signal network connects signal sources (buttons, levers, machine
// state outputs, SFM flow outputs) to signal consumers (machine
// inputs, doors, lights, SFM triggers) via signal-wire blocks.
//
// Conduction model: DIGITAL, NO ATTENUATION.
// Within a connected component of signal wires, the signal strength
// is uniform and equals the MAX of all sources feeding that
// component. A source feeds a component if it is adjacent (6-face)
// to any wire in the component. This gives OR-semantics: any active
// source powers the whole wire group.
//
// Signal strength is an int32_t (0 = off, >0 = on). This supports
// both boolean signals (0/1) and richer numeric signals (e.g.
// container fill percentage, machine progress) without changing
// the API.
//
// Topology is maintained via VoxelNetworkGraph (6-face adjacency,
// BFS component discovery). Recompute by calling update_network()
// after any wire or source change.
//
// Integration:
// - Signal-wire blocks register their position here on placement.
// - Machines/SFM register sources and read consumer values here.
// - BlockEntity layer (stage 4) drives add_wire/remove_wire on
//   block place/break.

class SignalNetwork {
public:
    SignalNetwork() = default;
    ~SignalNetwork() = default;

    // --- Wire block lifecycle ---

    // Adds a signal-wire conductor block at the given position.
    void add_wire(MapPosition pos) { graph_.add_block(pos); }

    // Removes a signal-wire conductor block at the given position.
    void remove_wire(MapPosition pos) { graph_.remove_block(pos); }

    // Returns whether a signal-wire block exists at the given position.
    bool has_wire(MapPosition pos) const { return graph_.has_block(pos); }

    // Returns the number of signal-wire blocks.
    size_t wire_count() const { return graph_.block_count(); }

    // --- Signal sources ---

    // Sets or updates a signal source at the given position.
    // A source is typically a non-wire block (button, lever, machine)
    // adjacent to one or more wires. strength == 0 effectively disables
    // the source but keeps it registered.
    void set_source(MapPosition pos, int32_t strength);

    // Removes a signal source entirely.
    void remove_source(MapPosition pos);

    // Returns the strength of a registered source, or 0 if not registered.
    int32_t get_source_strength(MapPosition pos) const;

    // --- Network recomputation ---

    // Recomputes signal distribution across all wire components.
    // Call once after a batch of wire/source changes, not every tick.
    // After this call, get_signal_at() returns up-to-date values.
    void update_network();

    // --- Signal queries ---

    // Returns the signal strength reaching the given position.
    // For a wire position: the component's uniform signal.
    // For a non-wire position (consumer): the max signal of any
    //   wire component adjacent to it.
    // Returns 0 if no signal reaches the position.
    int32_t get_signal_at(MapPosition pos) const;

    // Returns whether the position has any signal (>0).
    bool is_powered(MapPosition pos) const { return get_signal_at(pos) > 0; }

    // Returns all positions (wires + adjacent consumers) currently
    // receiving a non-zero signal. Useful for debugging/visualization.
    std::vector<std::pair<MapPosition, int32_t>> powered_positions() const;

    // Clears all wires and sources.
    void clear();

private:
    // The wire topology graph.
    VoxelNetworkGraph graph_;

    // Registered signal sources: position -> strength.
    std::unordered_map<MapPosition, int32_t> sources_;

    // Recomputed by update_network(): maps any position (wire or
    // adjacent consumer) to the signal strength reaching it.
    std::unordered_map<MapPosition, int32_t> signal_map_;
};

} // namespace science_and_theology::gt
