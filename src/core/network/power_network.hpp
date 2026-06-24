#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "voxel_network_graph.hpp"
#include "../config/gt_values.hpp"
#include "../power/power_node.hpp"  // MapPosition, OverloadInfo

namespace science_and_theology::gt {

// ============================================================
// PowerNetwork — per-block cable conduction network
// ============================================================
//
// Replaces the old point-to-point pole network with a per-tile cable
// model: every cable block is a conductor, connectivity is implicit
// 6-face adjacency (like Minecraft GT cables, not Factorio poles).
//
// Conduction model: CONNECTED-COMPONENT SHARED POOL + PER-TILE LOSS.
// Within a connected component of cables:
//   1. All generators feed a shared supply pool (sum of capacities,
//      capped by the component's min cable voltage tier).
//   2. All consumers draw from that pool (sum of demands).
//   3. Per-tile loss accumulates along the BFS tree from each
//      generator to each consumer; effective delivered power is
//      reduced by cable loss_per_tile * path length.
//   4. A cable block is OVER_CAPACITY if the current flowing through
//      it exceeds its voltage*amperage capacity -> cable burns.
//   5. A consumer is OVER_VOLTAGE if the component's voltage tier
//      exceeds the consumer's max input voltage -> machine explodes.
//
// Generators and consumers are NON-CABLE blocks adjacent (6-face)
// to at least one cable in a component. They register their
// position, capacity/demand, and voltage tier here.
//
// Topology is maintained via VoxelNetworkGraph. Recompute by calling
// update_network() after any cable/generator/consumer change.

// Callback type for overload notifications.
// Parameters: position, overload_info
using PowerOverloadCallback = std::function<void(MapPosition, const OverloadInfo&)>;

class PowerNetwork {
public:
    PowerNetwork() = default;
    ~PowerNetwork() = default;

    // --- Cable block lifecycle ---

    // Adds a cable conductor block at the given position with the
    // given tier (determines capacity and loss via CableProperties).
    void add_cable(MapPosition pos, VoltageTier tier);

    // Removes a cable conductor block at the given position.
    void remove_cable(MapPosition pos);

    // Returns whether a cable block exists at the given position.
    bool has_cable(MapPosition pos) const { return graph_.has_block(pos); }

    // Returns the number of cable blocks.
    size_t cable_count() const { return graph_.block_count(); }

    // --- Generator / consumer lifecycle ---

    // Sets or updates a generator at the given position.
    // capacity: max power it can supply per tick.
    // tier: voltage tier the generator outputs.
    void set_generator(MapPosition pos, int64_t capacity, VoltageTier tier);

    // Removes a generator.
    void remove_generator(MapPosition pos);

    // Sets or updates a consumer at the given position.
    // demand: power it wants to draw per tick.
    // max_input_voltage: highest voltage it can safely accept.
    void set_consumer(MapPosition pos, int64_t demand, int64_t max_input_voltage);

    // Removes a consumer.
    void remove_consumer(MapPosition pos);

    // --- Network recomputation ---

    // Recomputes power distribution across all cable components.
    // Call once after a batch of cable/generator/consumer changes.
    void update_network();

    // --- Power state queries ---

    // Returns the power available to a consumer at the given position
    // (after losses). Returns 0 if the position is not a registered
    // consumer or receives no power.
    int64_t get_power_at(MapPosition pos) const;

    // Returns whether the cable or consumer at the given position is
    // currently overloaded.
    bool is_overloaded(MapPosition pos) const;

    // Returns the overload info for a cable or consumer position.
    OverloadInfo get_overload_info(MapPosition pos) const;

    // Returns the total power loss across the entire network.
    int64_t get_total_power_loss() const { return total_power_loss_; }

    // Returns the total generation across the entire network.
    int64_t get_total_generation() const { return total_generation_; }

    // Returns the total demand across the entire network.
    int64_t get_total_demand() const { return total_demand_; }

    // Returns whether two positions are in the same cable component
    // (i.e., power can flow between them via cables).
    bool are_in_same_network(MapPosition a, MapPosition b) const;

    // --- Callbacks ---

    // Sets a callback invoked when a cable or consumer enters/exits
    // overload. Position identifies the affected block.
    void set_overload_callback(PowerOverloadCallback callback);

    // Clears all cables, generators, and consumers.
    void clear();

private:
    // Per-cable material properties, keyed by position.
    // Derived from the tier passed to add_cable().
    std::unordered_map<MapPosition, CableProperties> cable_props_;

    // The cable topology graph.
    VoxelNetworkGraph graph_;

    // Registered generators: position -> (capacity, tier).
    struct GeneratorEntry {
        int64_t capacity = 0;
        VoltageTier tier = VoltageTier::ULV;
    };
    std::unordered_map<MapPosition, GeneratorEntry> generators_;

    // Registered consumers: position -> (demand, max_input_voltage).
    struct ConsumerEntry {
        int64_t demand = 0;
        int64_t max_input_voltage = 0;
    };
    std::unordered_map<MapPosition, ConsumerEntry> consumers_;

    // Recomputed by update_network():
    //   power_at_: delivered power per consumer position.
    //   overload_: overload info per cable/consumer position.
    std::unordered_map<MapPosition, int64_t> power_at_;
    std::unordered_map<MapPosition, OverloadInfo> overload_;

    int64_t total_power_loss_ = 0;
    int64_t total_generation_ = 0;
    int64_t total_demand_ = 0;

    PowerOverloadCallback overload_callback_;

    // Resolves CableProperties for a given voltage tier.
    // Uses the first matching material in kCableMaterials.
    static CableProperties cable_for_tier(VoltageTier tier);
};

} // namespace science_and_theology::gt
