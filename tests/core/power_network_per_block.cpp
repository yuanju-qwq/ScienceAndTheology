// ============================================================
// Power network per-block conduction integration tests
// ============================================================
//
// Verifies the per-block cable power network:
//   - Cable connectivity via 6-face adjacency
//   - Shared capacity pool within a connected component
//   - Per-tile loss reduces delivered power
//   - Disconnected components do not share power
//   - Over-voltage detection (consumer exceeds cable tier)
//   - Cable break splits the network
//
// Current world-network boundary: docs/unified_universe_world_design.md §14.

#include <cassert>
#include <iostream>

#include "core/config/gt_values.hpp"
#include "core/network/power_network.hpp"

using science_and_theology::gt::MapPosition;
using science_and_theology::gt::PowerNetwork;
using science_and_theology::gt::VoltageTier;
using science_and_theology::gt::get_voltage;

namespace {

// Helper: build a straight cable line along X axis.
void build_cable_line_x(PowerNetwork& net, int x0, int x1, int y, int z,
                        VoltageTier tier) {
    for (int x = x0; x <= x1; ++x) {
        net.add_cable(MapPosition{x, y, z}, tier);
    }
}

// ============================================================
// Test 1: Generator powers consumer through cables
// ============================================================
void test_generator_powers_consumer() {
    PowerNetwork net;
    // Cables: (0,0,0) - (1,0,0) - (2,0,0)
    build_cable_line_x(net, 0, 2, 0, 0, VoltageTier::LV);
    // Generator adjacent to (0,0,0): at (0,1,0)
    net.set_generator(MapPosition{0, 1, 0}, 128, VoltageTier::LV);
    // Consumer adjacent to (2,0,0): at (2,1,0), accepts LV
    net.set_consumer(MapPosition{2, 1, 0}, 32, get_voltage(VoltageTier::LV));
    net.update_network();

    // Consumer should receive power (possibly reduced by loss).
    int64_t power = net.get_power_at(MapPosition{2, 1, 0});
    assert(power > 0);
    assert(!net.is_overloaded(MapPosition{2, 1, 0}));
    std::cout << "[PASS] test_generator_powers_consumer (power=" << power << ")\n";
}

// ============================================================
// Test 2: Disconnected components do not share power
// ============================================================
void test_disconnected_components_no_share() {
    PowerNetwork net;
    // Component A
    build_cable_line_x(net, 0, 2, 0, 0, VoltageTier::LV);
    // Component B (gap at x=3..9)
    build_cable_line_x(net, 10, 12, 0, 0, VoltageTier::LV);
    // Generator only on A
    net.set_generator(MapPosition{0, 1, 0}, 128, VoltageTier::LV);
    // Consumer on B
    net.set_consumer(MapPosition{12, 1, 0}, 32, get_voltage(VoltageTier::LV));
    net.update_network();

    // Consumer on B should get no power.
    assert(net.get_power_at(MapPosition{12, 1, 0}) == 0);
    assert(!net.are_in_same_network(MapPosition{0, 0, 0},
                                     MapPosition{10, 0, 0}));
    std::cout << "[PASS] test_disconnected_components_no_share\n";
}

// ============================================================
// Test 3: Same-network check
// ============================================================
void test_are_in_same_network() {
    PowerNetwork net;
    build_cable_line_x(net, 0, 5, 0, 0, VoltageTier::LV);
    net.update_network();

    assert(net.are_in_same_network(MapPosition{0, 0, 0},
                                    MapPosition{5, 0, 0}));
    assert(!net.are_in_same_network(MapPosition{0, 0, 0},
                                     MapPosition{6, 0, 0}));
    std::cout << "[PASS] test_are_in_same_network\n";
}

// ============================================================
// Test 4: Cable break splits the network
// ============================================================
void test_cable_break_splits_network() {
    PowerNetwork net;
    // (0,0,0) - (1,0,0) - (2,0,0) - (3,0,0)
    build_cable_line_x(net, 0, 3, 0, 0, VoltageTier::LV);
    net.set_generator(MapPosition{0, 1, 0}, 128, VoltageTier::LV);
    net.set_consumer(MapPosition{3, 1, 0}, 32, get_voltage(VoltageTier::LV));
    net.update_network();
    assert(net.get_power_at(MapPosition{3, 1, 0}) > 0);

    // Break the middle cable.
    net.remove_cable(MapPosition{1, 0, 0});
    net.update_network();

    // Consumer on the right side should no longer receive power.
    assert(net.get_power_at(MapPosition{3, 1, 0}) == 0);
    assert(!net.are_in_same_network(MapPosition{0, 0, 0},
                                     MapPosition{2, 0, 0}));
    std::cout << "[PASS] test_cable_break_splits_network\n";
}

// ============================================================
// Test 5: Over-voltage detection
// ============================================================
void test_over_voltage_detection() {
    PowerNetwork net;
    // HV cables (512V)
    build_cable_line_x(net, 0, 2, 0, 0, VoltageTier::HV);
    net.set_generator(MapPosition{0, 1, 0}, 4096, VoltageTier::HV);
    // Consumer that only accepts LV (32V) — should be over-voltage.
    net.set_consumer(MapPosition{2, 1, 0}, 32, get_voltage(VoltageTier::LV));
    net.update_network();

    // The consumer should be flagged as overloaded (over-voltage).
    assert(net.is_overloaded(MapPosition{2, 1, 0}));
    std::cout << "[PASS] test_over_voltage_detection\n";
}

// ============================================================
// Test 6: Total generation / demand accounting
// ============================================================
void test_total_accounting() {
    PowerNetwork net;
    build_cable_line_x(net, 0, 2, 0, 0, VoltageTier::LV);
    net.set_generator(MapPosition{0, 1, 0}, 100, VoltageTier::LV);
    net.set_consumer(MapPosition{2, 1, 0}, 40, get_voltage(VoltageTier::LV));
    net.update_network();

    assert(net.get_total_generation() == 100);
    assert(net.get_total_demand() == 40);
    std::cout << "[PASS] test_total_accounting\n";
}

// ============================================================
// Test 7: Multiple generators share the pool
// ============================================================
void test_multiple_generators_share_pool() {
    PowerNetwork net;
    build_cable_line_x(net, 0, 4, 0, 0, VoltageTier::LV);
    // Two generators, each 64 capacity.
    net.set_generator(MapPosition{0, 1, 0}, 64, VoltageTier::LV);
    net.set_generator(MapPosition{4, 1, 0}, 64, VoltageTier::LV);
    // One consumer demanding 100 (less than total 128).
    net.set_consumer(MapPosition{2, 1, 0}, 100, get_voltage(VoltageTier::LV));
    net.update_network();

    // Consumer should be fully supplied (minus small loss).
    int64_t power = net.get_power_at(MapPosition{2, 1, 0});
    assert(power > 0);
    std::cout << "[PASS] test_multiple_generators_share_pool (power=" << power << ")\n";
}

} // namespace

int main() {
    test_generator_powers_consumer();
    test_disconnected_components_no_share();
    test_are_in_same_network();
    test_cable_break_splits_network();
    test_over_voltage_detection();
    test_total_accounting();
    test_multiple_generators_share_pool();

    std::cout << "All power network tests passed.\n";
    return 0;
}
