#pragma once

#include <cstdint>
#include <vector>

#include "network/voxel_network_graph.hpp"
#include "network/signal_network.hpp"
#include "network/power_network.hpp"
#include "world/block_entity.hpp"
#include "world/block_entity_registry.hpp"

namespace science_and_theology::gt {

// ============================================================
// NetworkTopologyBuilder — bridges BlockEntityRegistry and networks
// ============================================================
//
// The per-block network systems (SignalNetwork, PowerNetwork) operate
// on VoxelNetworkGraph instances that store conductor positions. The
// BlockEntityRegistry stores the authoritative per-block state
// (SIGNAL_WIRE / CABLE entities) including connection bitmasks used
// for rendering.
//
// This builder synchronizes the two:
//   1. rebuild_signal_network(): scans all SIGNAL_WIRE entities,
//      feeds their positions into a SignalNetwork, runs the update,
//      and writes back per-wire signal_strength + connection masks.
//   2. rebuild_power_network(): scans all CABLE entities plus
//      MACHINE entities (as generators/consumers), feeds them into
//      a PowerNetwork, runs the update, and writes back connection
//      masks for cables.
//
// Connection bitmask auto-calculation:
//   For each conductor block, the mask is computed by checking the
//   6 face-neighbors. A bit is set if the neighbor is either:
//     - another conductor of the same kind (wire-wire, cable-cable), or
//     - a non-conductor block that the network should connect to
//       (machine, lever, button for signal wires; machine for cables).
//   The "connectable non-conductor" check is delegated to a callback
//   so the caller can decide what counts (e.g. only formed machines).

// --- Callback type definitions ---

// Returns true if the block at (x,y,z) is a connectable non-conductor
// for a signal network (e.g. a machine, lever, or button that a signal
// wire should visually connect to).
using SignalConnectablePredicate = bool (*)(int32_t x, int32_t y, int32_t z);

// Returns true if the block at (x,y,z) emits signal (e.g. a lever in
// the ON state, or a sensor with a triggered condition).
using SignalSourcePredicate = bool (*)(int32_t x, int32_t y, int32_t z);

// Generator info populated by PowerGeneratorLookup when the machine at
// a position is a generator.
struct PowerGeneratorInfo {
    int64_t capacity = 0;
    VoltageTier tier = VoltageTier::ULV;
};

// Consumer info populated by PowerConsumerLookup when the machine at
// a position is a consumer.
struct PowerConsumerInfo {
    int64_t demand = 0;
    int64_t max_input_voltage = 0;
};

// Return true if the machine at (x,y,z) is a generator; if so, fill
// *out with capacity and tier.
using PowerGeneratorLookup = bool (*)(int32_t x, int32_t y, int32_t z,
                                      PowerGeneratorInfo* out);

// Return true if the machine at (x,y,z) is a consumer; if so, fill
// *out with demand and max_input_voltage.
using PowerConsumerLookup = bool (*)(int32_t x, int32_t y, int32_t z,
                                     PowerConsumerInfo* out);

// Returns true if the block at (x,y,z) is a connectable non-conductor
// for the power network (e.g. a machine that a cable should connect to).
using PowerConnectablePredicate = bool (*)(int32_t x, int32_t y, int32_t z);

// --- Builder class ---

class NetworkTopologyBuilder {
public:
    // Rebuilds the signal network from SIGNAL_WIRE entities.
    // After this call:
    //   - signal_net contains the current wire topology + signal values.
    //   - Each SIGNAL_WIRE entity's signal_strength is updated.
    //   - Each SIGNAL_WIRE entity's connections mask is recomputed.
    // is_signal_source: callback returning true if the block at the
    //   given position emits signal.
    // is_connectable: callback returning true if the block at the
    //   given position should visually connect to a signal wire.
    static void rebuild_signal_network(
        BlockEntityRegistry& registry,
        SignalNetwork& signal_net,
        SignalSourcePredicate is_signal_source,
        SignalConnectablePredicate is_connectable);

    // Rebuilds the power network from CABLE + MACHINE entities.
    // After this call:
    //   - power_net contains the current cable topology + power flow.
    //   - Each CABLE entity's connections mask is recomputed.
    // gen_lookup: resolves generator capacity/tier for machine positions.
    // cons_lookup: resolves consumer demand/max_voltage for machine positions.
    // is_connectable: returns true if a non-cable block should connect
    //   to cables visually (e.g. a machine).
    static void rebuild_power_network(
        BlockEntityRegistry& registry,
        PowerNetwork& power_net,
        PowerGeneratorLookup gen_lookup,
        PowerConsumerLookup cons_lookup,
        PowerConnectablePredicate is_connectable);

private:
    // Computes the 6-face connection bitmask for a signal wire at
    // (x,y,z). A bit is set if the neighbor is either another
    // SIGNAL_WIRE entity or a connectable non-conductor.
    static uint8_t compute_connections_signal(
        const BlockEntityRegistry& registry,
        int32_t x, int32_t y, int32_t z,
        SignalConnectablePredicate is_connectable);

    // Computes the 6-face connection bitmask for a cable at (x,y,z).
    // A bit is set if the neighbor is either another CABLE entity or
    // a connectable non-conductor (typically a machine).
    static uint8_t compute_connections_cable(
        const BlockEntityRegistry& registry,
        int32_t x, int32_t y, int32_t z,
        PowerConnectablePredicate is_connectable);
};

} // namespace science_and_theology::gt
