#pragma once

#include <cstdint>
#include <vector>

#include "../config/gt_values.hpp"

namespace science_and_theology::gt {

// Unique identifier for a power node in the network.
using PowerNodeId = uint32_t;

inline constexpr PowerNodeId kInvalidNodeId = 0;

// Represents a 3D position in the voxel grid.
// Engine-independent: Godot adapters map Vector3i to/from this type.
struct MapPosition {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const MapPosition& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const MapPosition& other) const {
        return !(*this == other);
    }
};

} // namespace science_and_theology::gt

// Hash specialization for MapPosition so it can be used as unordered_map key.
namespace std {
template <>
struct hash<science_and_theology::gt::MapPosition> {
    size_t operator()(const science_and_theology::gt::MapPosition& p) const noexcept {
        // 64-bit FNV-style combine of three 32-bit coordinates.
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint32_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        mix(static_cast<uint32_t>(p.x));
        mix(static_cast<uint32_t>(p.y));
        mix(static_cast<uint32_t>(p.z));
        return static_cast<size_t>(h);
    }
};
} // namespace std

namespace science_and_theology::gt {

// Possible overload result states for a node or edge.
enum class OverloadState : uint8_t {
    OK = 0,
    OVER_VOLTAGE,   // Machine receiving power above its max voltage
    OVER_CAPACITY,  // Cable/wire carrying more power than its capacity
};

// Reasons why a node or edge is overloaded.
struct OverloadInfo {
    OverloadState state = OverloadState::OK;
    int64_t actual_load = 0;
    int64_t max_capacity = 0;
    int64_t actual_voltage = 0;
    int64_t max_voltage = 0;
};

// A power node in the grid: pole, machine, generator, or transformer.
// This is the fundamental building block of the Factorio-style pole network.
struct PowerNode {
    PowerNodeId id = kInvalidNodeId;
    VoltageTier tier = VoltageTier::ULV;
    MapPosition position;

    // Maximum power this node can supply to the network (0 for consumers).
    int64_t generation_capacity = 0;

    // Maximum voltage this node can accept before overloading.
    // Defaults to the nominal voltage of its tier.
    int64_t max_input_voltage = 0;

    // Current power demand from machines connected to this node.
    int64_t power_demand = 0;

    // Whether this node is a transformer.
    // Transformers isolate networks and convert voltage.
    bool is_transformer = false;

    // The output voltage tier on the secondary side (only for transformers).
    VoltageTier transformer_output_tier = VoltageTier::ULV;

    // Max tier steps this transformer can convert (1 = adjacent tier only).
    // Advanced transformers unlock higher values later in progression.
    int max_step = 1;

    // Fixed power loss per tier step difference for transformer operation.
    static constexpr int64_t kTransformerLossPerStep = 4;

    // Whether this node was detected as overloaded last update.
    OverloadInfo overload_info;

    PowerNode() {
        max_input_voltage = get_voltage(tier);
    }

    explicit PowerNode(PowerNodeId id, VoltageTier tier, MapPosition pos)
        : id(id), tier(tier), position(pos) {
        max_input_voltage = get_voltage(tier);
    }
};

// Represents a wire connection between two power nodes.
// Carries cable material properties that determine capacity and loss.
struct PowerEdge {
    PowerNodeId node_a = kInvalidNodeId;
    PowerNodeId node_b = kInvalidNodeId;

    // Cable material properties (name, tier, amperage, loss_per_tile).
    CableProperties cable;

    // Cached capacity: voltage × amperage.
    int64_t max_capacity = 0;

    // Cached distance between the two nodes in tiles (Manhattan).
    int64_t distance_tiles = 0;

    // Power loss incurred over this edge due to distance and material.
    int64_t power_loss = 0;

    // Current power flowing through this edge (set during update_network).
    int64_t current_load = 0;

    OverloadInfo overload_info;

    PowerEdge() {
        max_capacity = get_cable_capacity(cable);
    }

    PowerEdge(PowerNodeId a, PowerNodeId b, const CableProperties& cable_props,
              int64_t distance)
        : node_a(a), node_b(b), cable(cable_props),
          distance_tiles(distance) {
        max_capacity = get_cable_capacity(cable);
        power_loss = get_cable_loss(cable, distance_tiles);
    }

    bool connects(PowerNodeId a, PowerNodeId b) const {
        return (node_a == a && node_b == b) || (node_a == b && node_b == a);
    }
};

} // namespace science_and_theology::gt