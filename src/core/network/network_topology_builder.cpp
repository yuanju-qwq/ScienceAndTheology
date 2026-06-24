#include "network_topology_builder.hpp"

namespace science_and_theology::gt {

// ============================================================
// Signal network rebuild
// ============================================================

void NetworkTopologyBuilder::rebuild_signal_network(
    BlockEntityRegistry& registry,
    SignalNetwork& signal_net,
    SignalSourcePredicate is_signal_source,
    SignalConnectablePredicate is_connectable) {
    // Reset the network and re-seed it from the registry.
    signal_net.clear();

    // Phase 1: add all signal wire positions to the network and
    // register sources (wires flagged is_source, or adjacent blocks
    // reported by is_signal_source).
    // We need positions, so iterate via all_entities.
    const auto& all = registry.all_entities();
    for (const auto& pair : all) {
        const BlockEntityRegistry::BlockEntityEntry& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::SIGNAL_WIRE) {
            continue;
        }
        MapPosition pos{entry.placement.root_x,
                        entry.placement.root_y,
                        entry.placement.root_z};
        signal_net.add_wire(pos);

        // Register signal sources.
        // A wire is a source if its is_source flag is set, or if any
        // 6-face neighbor is reported by is_signal_source.
        bool source = entry.signal_wire_state.is_source;
        if (!source) {
            const MapPosition nbrs[6] = {
                {pos.x + 1, pos.y, pos.z},
                {pos.x - 1, pos.y, pos.z},
                {pos.x, pos.y + 1, pos.z},
                {pos.x, pos.y - 1, pos.z},
                {pos.x, pos.y, pos.z + 1},
                {pos.x, pos.y, pos.z - 1},
            };
            for (const MapPosition& n : nbrs) {
                if (is_signal_source && is_signal_source(n.x, n.y, n.z)) {
                    source = true;
                    break;
                }
            }
        }
        if (source) {
            // Digital signal: strength 1 (powered). The network uses
            // OR-semantics, so any source powers the component.
            signal_net.set_source(pos, 1);
        }
    }

    // Phase 2: run the signal propagation.
    signal_net.update_network();

    // Phase 3: write back per-wire signal_strength and recompute
    // connection bitmasks.
    for (const auto& pair : all) {
        EntityId id = pair.first;
        const BlockEntityRegistry::BlockEntityEntry& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::SIGNAL_WIRE) {
            continue;
        }
        int32_t x = entry.placement.root_x;
        int32_t y = entry.placement.root_y;
        int32_t z = entry.placement.root_z;
        MapPosition pos{x, y, z};

        // Write back signal strength.
        int32_t strength = signal_net.get_signal_at(pos);
        registry.update_signal_wire_strength(id, strength);

        // Recompute connection bitmask.
        uint8_t mask = compute_connections_signal(registry, x, y, z, is_connectable);
        registry.update_signal_wire_connections(id, mask);
    }
}

// ============================================================
// Power network rebuild
// ============================================================

void NetworkTopologyBuilder::rebuild_power_network(
    BlockEntityRegistry& registry,
    PowerNetwork& power_net,
    PowerGeneratorLookup gen_lookup,
    PowerConsumerLookup cons_lookup,
    PowerConnectablePredicate is_connectable) {
    // Reset the network and re-seed it from the registry.
    power_net.clear();

    // Phase 1: add all cable positions to the network.
    const auto& all = registry.all_entities();
    for (const auto& pair : all) {
        const BlockEntityRegistry::BlockEntityEntry& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::CABLE) {
            continue;
        }
        MapPosition pos{entry.placement.root_x,
                        entry.placement.root_y,
                        entry.placement.root_z};
        power_net.add_cable(pos, entry.cable_state.cable_tier);
    }

    // Phase 2: register generators and consumers from MACHINE entities.
    for (const auto& pair : all) {
        const BlockEntityRegistry::BlockEntityEntry& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::MACHINE) {
            continue;
        }
        int32_t x = entry.placement.root_x;
        int32_t y = entry.placement.root_y;
        int32_t z = entry.placement.root_z;

        PowerGeneratorInfo gen_info{};
        if (gen_lookup && gen_lookup(x, y, z, &gen_info)) {
            power_net.set_generator(MapPosition{x, y, z},
                                    gen_info.capacity, gen_info.tier);
        }

        PowerConsumerInfo cons_info{};
        if (cons_lookup && cons_lookup(x, y, z, &cons_info)) {
            power_net.set_consumer(MapPosition{x, y, z},
                                   cons_info.demand,
                                   cons_info.max_input_voltage);
        }
    }

    // Phase 3: run the power distribution.
    power_net.update_network();

    // Phase 4: recompute cable connection bitmasks.
    for (const auto& pair : all) {
        EntityId id = pair.first;
        const BlockEntityRegistry::BlockEntityEntry& entry = pair.second;
        if (entry.placement.entity_type != BlockEntityType::CABLE) {
            continue;
        }
        int32_t x = entry.placement.root_x;
        int32_t y = entry.placement.root_y;
        int32_t z = entry.placement.root_z;
        uint8_t mask = compute_connections_cable(registry, x, y, z, is_connectable);
        registry.update_cable_connections(id, mask);
    }
}

// ============================================================
// Connection bitmask computation
// ============================================================

// Neighbor offsets in the same order as BlockEntityConnectionMask:
//   bit 0 = +X, bit 1 = -X, bit 2 = +Y, bit 3 = -Y, bit 4 = +Z, bit 5 = -Z.
struct NeighborOffset {
    int32_t dx, dy, dz;
    uint8_t bit;
};

static constexpr NeighborOffset kNeighborOffsets[6] = {
    { 1,  0,  0, CONN_POS_X},
    {-1,  0,  0, CONN_NEG_X},
    { 0,  1,  0, CONN_POS_Y},
    { 0, -1,  0, CONN_NEG_Y},
    { 0,  0,  1, CONN_POS_Z},
    { 0,  0, -1, CONN_NEG_Z},
};

uint8_t NetworkTopologyBuilder::compute_connections_signal(
    const BlockEntityRegistry& registry,
    int32_t x, int32_t y, int32_t z,
    SignalConnectablePredicate is_connectable) {
    uint8_t mask = 0;
    for (const NeighborOffset& n : kNeighborOffsets) {
        int32_t nx = x + n.dx;
        int32_t ny = y + n.dy;
        int32_t nz = z + n.dz;
        // Connect to another signal wire entity.
        EntityId owner = registry.find_owner_at(nx, ny, nz);
        bool connect = false;
        if (owner.id != 0) {
            BlockEntityType t = registry.get_entity_type(owner);
            if (t == BlockEntityType::SIGNAL_WIRE) {
                connect = true;
            }
        }
        // Connect to a non-conductor block the predicate accepts.
        if (!connect && is_connectable && is_connectable(nx, ny, nz)) {
            connect = true;
        }
        if (connect) {
            mask |= n.bit;
        }
    }
    return mask;
}

uint8_t NetworkTopologyBuilder::compute_connections_cable(
    const BlockEntityRegistry& registry,
    int32_t x, int32_t y, int32_t z,
    PowerConnectablePredicate is_connectable) {
    uint8_t mask = 0;
    for (const NeighborOffset& n : kNeighborOffsets) {
        int32_t nx = x + n.dx;
        int32_t ny = y + n.dy;
        int32_t nz = z + n.dz;
        // Connect to another cable entity.
        EntityId owner = registry.find_owner_at(nx, ny, nz);
        bool connect = false;
        if (owner.id != 0) {
            BlockEntityType t = registry.get_entity_type(owner);
            if (t == BlockEntityType::CABLE) {
                connect = true;
            }
        }
        // Connect to a non-conductor block the predicate accepts.
        if (!connect && is_connectable && is_connectable(nx, ny, nz)) {
            connect = true;
        }
        if (connect) {
            mask |= n.bit;
        }
    }
    return mask;
}

} // namespace science_and_theology::gt
