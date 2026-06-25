// ============================================================
// Network topology builder integration tests
// ============================================================
//
// Verifies the bridge between BlockEntityRegistry and the per-block
// network systems (SignalNetwork, PowerNetwork):
//   - Signal wire entities feed into SignalNetwork
//   - Connection bitmasks are auto-computed from neighbors
//   - Signal strength is written back to entity state
//   - Cable entities feed into PowerNetwork
//   - Machine generators/consumers are resolved via callbacks
//
// Design: docs/network_system_design.md (stage 4 BlockEntity integration)

#include <cassert>
#include <iostream>
#include <unordered_set>

#include "core/network/network_topology_builder.hpp"
#include "core/network/signal_network.hpp"
#include "core/network/power_network.hpp"
#include "core/world/block_entity.hpp"
#include "core/world/block_entity_registry.hpp"

using science_and_theology::BlockEntityRegistry;
using science_and_theology::BlockEntityType;
using science_and_theology::CONN_NEG_X;
using science_and_theology::CONN_POS_X;
using science_and_theology::gt::MapPosition;
using science_and_theology::gt::PowerNetwork;
using science_and_theology::gt::SignalNetwork;
using science_and_theology::gt::VoltageTier;
using science_and_theology::gt::get_voltage;

namespace {

constexpr const char* kDim = "overworld";

// ============================================================
// Test 1: Signal wire entities propagate signal through the builder
// ============================================================
void test_signal_wire_topology_rebuild() {
    BlockEntityRegistry registry;
    SignalNetwork signal_net;

    // Place three signal wires in a line: (0,0,0)-(1,0,0)-(2,0,0)
    registry.register_signal_wire_entity(kDim, 0, 0, 0, 0, false);
    registry.register_signal_wire_entity(kDim, 1, 0, 0, 0, false);
    registry.register_signal_wire_entity(kDim, 2, 0, 0, 0, true);  // source

    // No external sources, no connectable non-conductors.
    science_and_theology::gt::SignalSourcePredicate src_pred = nullptr;
    science_and_theology::gt::SignalConnectablePredicate conn_pred = nullptr;

    science_and_theology::gt::NetworkTopologyBuilder::rebuild_signal_network(
        registry, signal_net, src_pred, conn_pred);

    // All three wires should be powered (digital, no attenuation).
    assert(signal_net.is_powered(MapPosition{0, 0, 0}));
    assert(signal_net.is_powered(MapPosition{1, 0, 0}));
    assert(signal_net.is_powered(MapPosition{2, 0, 0}));

    // The signal strength should be written back to entity state.
    // Find the entity at (0,0,0) by scanning all entities.
    int32_t found_strength = -1;
    for (const auto& pair : registry.all_entities()) {
        const auto& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::SIGNAL_WIRE) continue;
        if (entry.placement.root_x == 0 && entry.placement.root_y == 0 &&
            entry.placement.root_z == 0) {
            found_strength = entry.signal_wire_state.signal_strength;
            break;
        }
    }
    assert(found_strength > 0);
    std::cout << "[PASS] test_signal_wire_topology_rebuild\n";
}

// ============================================================
// Test 2: Connection bitmask auto-computation
// ============================================================
void test_connection_bitmask_auto_compute() {
    BlockEntityRegistry registry;
    SignalNetwork signal_net;

    // Two adjacent wires: (0,0,0) and (1,0,0)
    registry.register_signal_wire_entity(kDim, 0, 0, 0, 0, false);
    registry.register_signal_wire_entity(kDim, 1, 0, 0, 0, false);

    science_and_theology::gt::NetworkTopologyBuilder::rebuild_signal_network(
        registry, signal_net, nullptr, nullptr);

    // Wire at (0,0,0) should connect to +X (towards (1,0,0)).
    uint8_t mask0 = 0;
    uint8_t mask1 = 0;
    for (const auto& pair : registry.all_entities()) {
        const auto& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::SIGNAL_WIRE) continue;
        if (entry.placement.root_x == 0 && entry.placement.root_y == 0 &&
            entry.placement.root_z == 0) {
            mask0 = entry.signal_wire_state.connections;
        } else if (entry.placement.root_x == 1 && entry.placement.root_y == 0 &&
                   entry.placement.root_z == 0) {
            mask1 = entry.signal_wire_state.connections;
        }
    }
    assert((mask0 & CONN_POS_X) != 0);

    // Wire at (1,0,0) should connect to -X (towards (0,0,0)).
    assert((mask1 & CONN_NEG_X) != 0);
    std::cout << "[PASS] test_connection_bitmask_auto_compute\n";
}

// ============================================================
// Test 3: External signal source via predicate
// ============================================================
void test_external_signal_source_predicate() {
    BlockEntityRegistry registry;
    SignalNetwork signal_net;

    // One wire at (0,0,0), not a source itself.
    registry.register_signal_wire_entity(kDim, 0, 0, 0, 0, false);

    // Predicate: a lever at (0,1,0) emits signal.
    auto src_pred = [](int32_t x, int32_t y, int32_t z) -> bool {
        return (x == 0 && y == 1 && z == 0);
    };

    science_and_theology::gt::NetworkTopologyBuilder::rebuild_signal_network(
        registry, signal_net, src_pred, nullptr);

    // The wire should be powered because the adjacent lever is a source.
    assert(signal_net.is_powered(MapPosition{0, 0, 0}));
    std::cout << "[PASS] test_external_signal_source_predicate\n";
}

// ============================================================
// Test 4: Power network rebuild from cable + machine entities
// ============================================================
void test_power_network_topology_rebuild() {
    BlockEntityRegistry registry;
    PowerNetwork power_net;

    // Cables: (0,0,0)-(1,0,0)-(2,0,0)
    registry.register_cable_entity(kDim, 0, 0, 0, VoltageTier::LV, 0);
    registry.register_cable_entity(kDim, 1, 0, 0, VoltageTier::LV, 0);
    registry.register_cable_entity(kDim, 2, 0, 0, VoltageTier::LV, 0);

    // Machine at (0,1,0) — generator
    registry.register_machine_entity(kDim, 0, 1, 0, "generator", 0);
    // Machine at (2,1,0) — consumer
    registry.register_machine_entity(kDim, 2, 1, 0, "consumer", 0);

    // Generator lookup: machine at (0,1,0) is a generator.
    auto gen_lookup = [](int32_t x, int32_t y, int32_t z,
                         science_and_theology::gt::PowerGeneratorInfo* out) -> bool {
        if (x == 0 && y == 1 && z == 0) {
            out->capacity = 128;
            out->tier = VoltageTier::LV;
            return true;
        }
        return false;
    };
    // Consumer lookup: machine at (2,1,0) is a consumer.
    auto cons_lookup = [](int32_t x, int32_t y, int32_t z,
                          science_and_theology::gt::PowerConsumerInfo* out) -> bool {
        if (x == 2 && y == 1 && z == 0) {
            out->demand = 32;
            out->max_input_voltage = get_voltage(VoltageTier::LV);
            return true;
        }
        return false;
    };

    science_and_theology::gt::NetworkTopologyBuilder::rebuild_power_network(
        registry, power_net, gen_lookup, cons_lookup, nullptr);

    // Consumer should receive power.
    assert(power_net.get_power_at(MapPosition{2, 1, 0}) > 0);
    std::cout << "[PASS] test_power_network_topology_rebuild\n";
}

} // namespace

int main() {
    test_signal_wire_topology_rebuild();
    test_connection_bitmask_auto_compute();
    test_external_signal_source_predicate();
    test_power_network_topology_rebuild();

    std::cout << "All network topology builder tests passed.\n";
    return 0;
}
