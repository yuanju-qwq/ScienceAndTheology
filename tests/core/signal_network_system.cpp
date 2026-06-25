// ============================================================
// Signal network integration tests
// ============================================================
//
// Verifies the per-block digital signal network:
//   - Wire connectivity via 6-face adjacency
//   - OR-semantics: max source strength wins
//   - No attenuation across wire components
//   - Consumer (non-wire) signal reading
//   - Source removal / wire break behavior
//
// Design: docs/network_system_design.md (signal network, digital model)

#include <cassert>
#include <iostream>
#include <vector>

#include "core/network/signal_network.hpp"
#include "core/network/voxel_network_graph.hpp"

using science_and_theology::gt::MapPosition;
using science_and_theology::gt::SignalNetwork;
using science_and_theology::gt::VoxelNetworkGraph;

namespace {

// Helper: build a straight wire line from (x0,y,z0) along X axis.
void build_line_x(SignalNetwork& net, int x0, int x1, int y, int z) {
    for (int x = x0; x <= x1; ++x) {
        net.add_wire(MapPosition{x, y, z});
    }
}

// ============================================================
// Test 1: Single source powers entire connected wire component
// ============================================================
void test_single_source_powers_component() {
    SignalNetwork net;
    // Wires: (0,0,0) - (1,0,0) - (2,0,0) - (3,0,0)
    build_line_x(net, 0, 3, 0, 0);
    // Source adjacent to wire at (0,0,0): placed at (0,1,0)
    net.set_source(MapPosition{0, 1, 0}, 1);
    net.update_network();

    // Every wire should be powered with strength 1 (no attenuation).
    for (int x = 0; x <= 3; ++x) {
        assert(net.is_powered(MapPosition{x, 0, 0}));
        assert(net.get_signal_at(MapPosition{x, 0, 0}) == 1);
    }
    std::cout << "[PASS] test_single_source_powers_component\n";
}

// ============================================================
// Test 2: OR-semantics — multiple sources, max wins
// ============================================================
void test_or_semantics_max_wins() {
    SignalNetwork net;
    build_line_x(net, 0, 3, 0, 0);
    // Two sources with different strengths feeding the same component.
    net.set_source(MapPosition{0, 1, 0}, 5);
    net.set_source(MapPosition{3, 1, 0}, 9);
    net.update_network();

    // The whole component should carry the max strength (9).
    for (int x = 0; x <= 3; ++x) {
        assert(net.get_signal_at(MapPosition{x, 0, 0}) == 9);
    }
    std::cout << "[PASS] test_or_semantics_max_wins\n";
}

// ============================================================
// Test 3: Disconnected components stay independent
// ============================================================
void test_disconnected_components_independent() {
    SignalNetwork net;
    // Component A: (0,0,0) - (1,0,0)
    build_line_x(net, 0, 1, 0, 0);
    // Component B: (10,0,0) - (11,0,0)  (gap, not connected)
    build_line_x(net, 10, 11, 0, 0);
    // Only source feeds component A.
    net.set_source(MapPosition{0, 1, 0}, 1);
    net.update_network();

    assert(net.is_powered(MapPosition{0, 0, 0}));
    assert(net.is_powered(MapPosition{1, 0, 0}));
    assert(!net.is_powered(MapPosition{10, 0, 0}));
    assert(!net.is_powered(MapPosition{11, 0, 0}));
    std::cout << "[PASS] test_disconnected_components_independent\n";
}

// ============================================================
// Test 4: Consumer (non-wire) reads signal from adjacent wire
// ============================================================
void test_consumer_reads_adjacent_signal() {
    SignalNetwork net;
    build_line_x(net, 0, 2, 0, 0);
    net.set_source(MapPosition{0, 1, 0}, 7);
    net.update_network();

    // Consumer at (2,1,0) is adjacent to wire (2,0,0).
    assert(net.is_powered(MapPosition{2, 1, 0}));
    assert(net.get_signal_at(MapPosition{2, 1, 0}) == 7);

    // Non-adjacent position should be unpowered.
    assert(!net.is_powered(MapPosition{5, 5, 5}));
    std::cout << "[PASS] test_consumer_reads_adjacent_signal\n";
}

// ============================================================
// Test 5: Breaking a wire splits the component
// ============================================================
void test_wire_break_splits_component() {
    SignalNetwork net;
    // (0,0,0) - (1,0,0) - (2,0,0) - (3,0,0)
    build_line_x(net, 0, 3, 0, 0);
    net.set_source(MapPosition{0, 1, 0}, 1);
    net.update_network();
    assert(net.is_powered(MapPosition{3, 0, 0}));

    // Break the middle wire.
    net.remove_wire(MapPosition{1, 0, 0});
    net.update_network();

    // Left side still powered.
    assert(net.is_powered(MapPosition{0, 0, 0}));
    // Right side now unpowered (source was on the left).
    assert(!net.is_powered(MapPosition{2, 0, 0}));
    assert(!net.is_powered(MapPosition{3, 0, 0}));
    std::cout << "[PASS] test_wire_break_splits_component\n";
}

// ============================================================
// Test 6: Source removal clears the component
// ============================================================
void test_source_removal_clears_component() {
    SignalNetwork net;
    build_line_x(net, 0, 2, 0, 0);
    net.set_source(MapPosition{0, 1, 0}, 1);
    net.update_network();
    assert(net.is_powered(MapPosition{0, 0, 0}));

    net.remove_source(MapPosition{0, 1, 0});
    net.update_network();
    assert(!net.is_powered(MapPosition{0, 0, 0}));
    assert(!net.is_powered(MapPosition{2, 0, 0}));
    std::cout << "[PASS] test_source_removal_clears_component\n";
}

// ============================================================
// Test 7: Zero-strength source acts as "off"
// ============================================================
void test_zero_strength_source_is_off() {
    SignalNetwork net;
    build_line_x(net, 0, 2, 0, 0);
    net.set_source(MapPosition{0, 1, 0}, 0);
    net.update_network();
    assert(!net.is_powered(MapPosition{0, 0, 0}));
    std::cout << "[PASS] test_zero_strength_source_is_off\n";
}

// ============================================================
// Test 8: VoxelNetworkGraph component discovery
// ============================================================
void test_voxel_graph_component_discovery() {
    VoxelNetworkGraph graph;
    // L-shape: (0,0,0)-(1,0,0)-(1,1,0)
    graph.add_block(MapPosition{0, 0, 0});
    graph.add_block(MapPosition{1, 0, 0});
    graph.add_block(MapPosition{1, 1, 0});
    // Separate: (5,5,5)
    graph.add_block(MapPosition{5, 5, 5});

    auto components = graph.find_all_components();
    assert(components.size() == 2);

    // Find the 3-block component.
    size_t big_size = 0;
    for (const auto& c : components) {
        big_size = std::max(big_size, c.size());
    }
    assert(big_size == 3);

    // Adjacent outside of the L-shape component.
    auto comp = graph.find_component(MapPosition{0, 0, 0});
    assert(comp.size() == 3);
    auto outside = graph.find_adjacent_outside(comp);
    // (5,5,5) is NOT adjacent, so outside should not contain it.
    bool has_far = false;
    for (const auto& p : outside) {
        if (p.x == 5 && p.y == 5 && p.z == 5) has_far = true;
    }
    assert(!has_far);
    std::cout << "[PASS] test_voxel_graph_component_discovery\n";
}

} // namespace

int main() {
    test_voxel_graph_component_discovery();
    test_single_source_powers_component();
    test_or_semantics_max_wins();
    test_disconnected_components_independent();
    test_consumer_reads_adjacent_signal();
    test_wire_break_splits_component();
    test_source_removal_clears_component();
    test_zero_strength_source_is_off();

    std::cout << "All signal network tests passed.\n";
    return 0;
}
