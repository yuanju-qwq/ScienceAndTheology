#include "power_network.hpp"

#include <algorithm>
#include <queue>

namespace science_and_theology::gt {

// --- Cable block lifecycle ---

void PowerNetwork::add_cable(MapPosition pos, VoltageTier tier) {
    graph_.add_block(pos);
    cable_props_[pos] = cable_for_tier(tier);
}

void PowerNetwork::remove_cable(MapPosition pos) {
    graph_.remove_block(pos);
    cable_props_.erase(pos);
}

// --- Generator / consumer lifecycle ---

void PowerNetwork::set_generator(MapPosition pos, int64_t capacity,
                                  VoltageTier tier) {
    generators_[pos] = GeneratorEntry{capacity, tier};
}

void PowerNetwork::remove_generator(MapPosition pos) {
    generators_.erase(pos);
}

void PowerNetwork::set_consumer(MapPosition pos, int64_t demand,
                                 int64_t max_input_voltage) {
    consumers_[pos] = ConsumerEntry{demand, max_input_voltage};
}

void PowerNetwork::remove_consumer(MapPosition pos) {
    consumers_.erase(pos);
}

// --- Network recomputation ---

// Recomputes power distribution.
// For each cable component:
//   1. Discover generators/consumers adjacent to the component.
//   2. Component voltage tier = min tier among all cables (bottleneck).
//   3. Total supply = sum of generator capacities (capped by tier).
//   4. Total demand = sum of consumer demands.
//   5. For each consumer: BFS from nearest generator to compute path
//      length; delivered power = demand - (loss_per_tile * path_len),
//      clamped to [0, demand]. Supply is rationed proportionally if
//      total demand exceeds total supply.
//   6. Overload checks: cables carrying current > capacity, consumers
//      with component voltage > max_input_voltage.
void PowerNetwork::update_network() {
    power_at_.clear();
    overload_.clear();
    total_power_loss_ = 0;
    total_generation_ = 0;
    total_demand_ = 0;

    std::vector<std::vector<MapPosition>> components = graph_.find_all_components();

    for (const std::vector<MapPosition>& component : components) {
        // Set of cable positions in this component for fast lookup.
        std::unordered_set<MapPosition> comp_set(component.begin(),
                                                  component.end());

        // Phase 1: find generators and consumers adjacent to this component.
        std::vector<MapPosition> comp_generators;
        std::vector<MapPosition> comp_consumers;
        for (const MapPosition& wire : component) {
            // Check 6 neighbors for generators/consumers.
            const MapPosition nbrs[6] = {
                {wire.x + 1, wire.y, wire.z},
                {wire.x - 1, wire.y, wire.z},
                {wire.x, wire.y + 1, wire.z},
                {wire.x, wire.y - 1, wire.z},
                {wire.x, wire.y, wire.z + 1},
                {wire.x, wire.y, wire.z - 1},
            };
            for (const MapPosition& n : nbrs) {
                if (generators_.count(n) > 0) {
                    comp_generators.push_back(n);
                }
                if (consumers_.count(n) > 0) {
                    comp_consumers.push_back(n);
                }
            }
        }
        // Deduplicate (a generator/consumer may border multiple wires).
        std::sort(comp_generators.begin(), comp_generators.end());
        comp_generators.erase(std::unique(comp_generators.begin(),
                                          comp_generators.end()),
                              comp_generators.end());
        std::sort(comp_consumers.begin(), comp_consumers.end());
        comp_consumers.erase(std::unique(comp_consumers.begin(),
                                         comp_consumers.end()),
                             comp_consumers.end());

        if (comp_consumers.empty()) {
            continue;  // No consumers; nothing to power.
        }

        // Phase 2: component voltage tier = min cable tier (bottleneck).
        VoltageTier comp_tier = VoltageTier::MAX;
        for (const MapPosition& wire : component) {
            auto it = cable_props_.find(wire);
            if (it != cable_props_.end()) {
                if (static_cast<uint8_t>(it->second.max_voltage_tier) <
                    static_cast<uint8_t>(comp_tier)) {
                    comp_tier = it->second.max_voltage_tier;
                }
            }
        }
        int64_t comp_voltage = get_voltage(comp_tier);

        // Phase 3: total supply and demand.
        int64_t supply = 0;
        for (const MapPosition& g : comp_generators) {
            supply += generators_[g].capacity;
        }
        int64_t demand = 0;
        for (const MapPosition& c : comp_consumers) {
            demand += consumers_[c].demand;
        }
        total_generation_ += supply;
        total_demand_ += demand;

        // Rationing factor: if supply < demand, each consumer gets a share.
        // Use fixed-point scaling to avoid float (1<<20 = ~1M precision).
        constexpr int64_t kScale = 1 << 20;
        int64_t ration = (demand <= 0) ? kScale
            : std::min<int64_t>(kScale, (supply * kScale) / demand);

        // Phase 4: for each consumer, BFS from nearest generator through
        // the cable component to compute path length and loss.
        // Multi-source BFS: seed the frontier with all generator-adjacent
        // wires at distance 1.
        std::unordered_map<MapPosition, int64_t> dist;
        std::queue<MapPosition> bfs;
        for (const MapPosition& g : comp_generators) {
            // Seed wires adjacent to this generator.
            const MapPosition nbrs[6] = {
                {g.x + 1, g.y, g.z},
                {g.x - 1, g.y, g.z},
                {g.x, g.y + 1, g.z},
                {g.x, g.y - 1, g.z},
                {g.x, g.y, g.z + 1},
                {g.x, g.y, g.z - 1},
            };
            for (const MapPosition& n : nbrs) {
                if (comp_set.count(n) > 0 && dist.count(n) == 0) {
                    dist[n] = 1;
                    bfs.push(n);
                }
            }
        }

        while (!bfs.empty()) {
            MapPosition cur = bfs.front();
            bfs.pop();
            int64_t next_d = dist[cur] + 1;
            const MapPosition nbrs[6] = {
                {cur.x + 1, cur.y, cur.z},
                {cur.x - 1, cur.y, cur.z},
                {cur.x, cur.y + 1, cur.z},
                {cur.x, cur.y - 1, cur.z},
                {cur.x, cur.y, cur.z + 1},
                {cur.x, cur.y, cur.z - 1},
            };
            for (const MapPosition& n : nbrs) {
                if (comp_set.count(n) > 0 && dist.count(n) == 0) {
                    dist[n] = next_d;
                    bfs.push(n);
                }
            }
        }

        // Phase 5: compute delivered power per consumer and per-cable load.
        // Track current flowing through each cable for overload checks.
        std::unordered_map<MapPosition, int64_t> cable_load;
        for (const MapPosition& wire : component) {
            cable_load[wire] = 0;
        }

        for (const MapPosition& c : comp_consumers) {
            const ConsumerEntry& ce = consumers_[c];
            // Find the nearest wire to this consumer and its distance.
            int64_t min_dist = INT64_MAX;
            MapPosition nearest_wire{};
            bool found = false;
            const MapPosition nbrs[6] = {
                {c.x + 1, c.y, c.z},
                {c.x - 1, c.y, c.z},
                {c.x, c.y + 1, c.z},
                {c.x, c.y - 1, c.z},
                {c.x, c.y, c.z + 1},
                {c.x, c.y, c.z - 1},
            };
            for (const MapPosition& n : nbrs) {
                auto it = dist.find(n);
                if (it != dist.end() && it->second < min_dist) {
                    min_dist = it->second;
                    nearest_wire = n;
                    found = true;
                }
            }
            if (!found) {
                continue;  // Consumer not reachable from any generator.
            }

            // Loss along the path: use the nearest cable's loss_per_tile
            // as a representative value (simplified; full per-edge loss
            // would require tracking the actual BFS path).
            auto cable_it = cable_props_.find(nearest_wire);
            int64_t loss_per_tile = (cable_it != cable_props_.end())
                ? cable_it->second.loss_per_tile : 1;
            int64_t path_loss = loss_per_tile * min_dist;

            // Rationed demand.
            int64_t rationed_demand = (ce.demand * ration) / kScale;
            int64_t delivered = std::max<int64_t>(0, rationed_demand - path_loss);
            power_at_[c] = delivered;
            total_power_loss_ += path_loss;

            // Accumulate load on the nearest cable (simplified: all current
            // flows through the entry cable).
            cable_load[nearest_wire] += delivered;

            // Over-voltage check: component voltage > consumer max.
            if (comp_voltage > ce.max_input_voltage && ce.max_input_voltage > 0) {
                OverloadInfo info;
                info.state = OverloadState::OVER_VOLTAGE;
                info.actual_voltage = comp_voltage;
                info.max_voltage = ce.max_input_voltage;
                overload_[c] = info;
            }
        }

        // Phase 6: per-cable overload check (over-capacity).
        for (const MapPosition& wire : component) {
            int64_t load = cable_load[wire];
            auto cable_it = cable_props_.find(wire);
            if (cable_it == cable_props_.end()) continue;
            int64_t capacity = get_cable_capacity(cable_it->second);
            if (load > capacity) {
                OverloadInfo info;
                info.state = OverloadState::OVER_CAPACITY;
                info.actual_load = load;
                info.max_capacity = capacity;
                overload_[wire] = info;
            }
        }
    }

    // Fire overload callbacks for newly overloaded positions.
    if (overload_callback_) {
        for (const auto& entry : overload_) {
            overload_callback_(entry.first, entry.second);
        }
    }
}

// --- Power state queries ---

int64_t PowerNetwork::get_power_at(MapPosition pos) const {
    auto it = power_at_.find(pos);
    return it == power_at_.end() ? 0 : it->second;
}

bool PowerNetwork::is_overloaded(MapPosition pos) const {
    return overload_.count(pos) > 0;
}

OverloadInfo PowerNetwork::get_overload_info(MapPosition pos) const {
    auto it = overload_.find(pos);
    return it == overload_.end() ? OverloadInfo{} : it->second;
}

bool PowerNetwork::are_in_same_network(MapPosition a, MapPosition b) const {
    if (!graph_.has_block(a) || !graph_.has_block(b)) {
        return false;
    }
    std::vector<MapPosition> comp = graph_.find_component(a);
    for (const MapPosition& p : comp) {
        if (p == b) return true;
    }
    return false;
}

// --- Callbacks ---

void PowerNetwork::set_overload_callback(PowerOverloadCallback callback) {
    overload_callback_ = std::move(callback);
}

void PowerNetwork::clear() {
    graph_.clear();
    cable_props_.clear();
    generators_.clear();
    consumers_.clear();
    power_at_.clear();
    overload_.clear();
    total_power_loss_ = 0;
    total_generation_ = 0;
    total_demand_ = 0;
}

// --- Static helpers ---

CableProperties PowerNetwork::cable_for_tier(VoltageTier tier) {
    // Return the first material matching the tier (representative cable).
    for (size_t i = 0; i < kCableMaterialCount; ++i) {
        if (kCableMaterials[i].max_voltage_tier == tier) {
            return kCableMaterials[i];
        }
    }
    // Fallback: ULV tin.
    return CableProperties{};
}

} // namespace science_and_theology::gt
