#include "region_system.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"
#include "event_types.hpp"

namespace science_and_theology {

// --- RegionSystem ---

RegionSystem::RegionSystem() {
    for (size_t i = 0; i < static_cast<size_t>(RegionType::COUNT); ++i) {
        graphs_[i] = std::make_unique<RegionGraph>(
            static_cast<RegionType>(i));
    }
}

void RegionSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void RegionSystem::tick_active(const ChunkKey& chunk, float delta,
                               const TickContext* ctx) {
    // Avoid duplicate processing per tick.
    if (processed_chunks_.count(chunk)) return;
    processed_chunks_.insert(chunk);

    // Rebuild any dirty regions before simulation.
    rebuild_dirty_regions();

    // Run pollution and temperature simulation.
    simulate_chunk(chunk, delta);
}

void RegionSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                const TickContext* ctx) {
    // Low-frequency simulation: only run diffusion at reduced rate.
    simulate_chunk(chunk, delta * 0.25f);
}

void RegionSystem::shutdown() {
    for (auto& graph : graphs_) {
        graph.reset();
    }
    processed_chunks_.clear();
}

// --- Graph access ---

RegionGraph* RegionSystem::get_graph(RegionType type) {
    size_t idx = static_cast<size_t>(type);
    if (idx >= static_cast<size_t>(RegionType::COUNT)) return nullptr;
    return graphs_[idx].get();
}

const RegionGraph* RegionSystem::get_graph(RegionType type) const {
    size_t idx = static_cast<size_t>(type);
    if (idx >= static_cast<size_t>(RegionType::COUNT)) return nullptr;
    return graphs_[idx].get();
}

// --- Node management ---

uint64_t RegionSystem::add_node(RegionType type, const RegionNode& node) {
    auto* graph = get_graph(type);
    if (!graph) return 0;

    uint64_t rid = graph->add_node(node);

    // Emit region created event if this is a new single-node region.
    if (event_bus_) {
        event_bus_->enqueue(GameEvent::region_created(
            rid, kRegionTypeNames[static_cast<size_t>(type)],
            node.dimension_id));
    }

    return rid;
}

void RegionSystem::remove_node(RegionType type, const RegionNode& node) {
    auto* graph = get_graph(type);
    if (!graph) return;

    uint64_t rid = graph->find_region(node);
    graph->remove_node(node);

    // If the region becomes empty after removal, emit destroyed event.
    if (rid != 0) {
        auto* data = graph->get_region_data(rid);
        if (!data || data->node_count == 0) {
            if (event_bus_) {
                event_bus_->enqueue(GameEvent::region_destroyed(
                    rid, kRegionTypeNames[static_cast<size_t>(type)],
                    node.dimension_id));
            }
        }
    }
}

uint64_t RegionSystem::link(
    RegionType type, const RegionNode& a, const RegionNode& b) {
    auto* graph = get_graph(type);
    if (!graph) return 0;

    uint64_t ra_before = graph->find_region(a);
    uint64_t rb_before = graph->find_region(b);
    uint64_t merged = graph->link(a, b);

    // Emit merge event if two different regions were joined.
    if (merged != 0 && ra_before != rb_before && ra_before != 0 && rb_before != 0) {
        if (event_bus_) {
            uint64_t absorbed = (merged == ra_before) ? rb_before : ra_before;
            event_bus_->enqueue(GameEvent::region_merged(
                merged, absorbed,
                kRegionTypeNames[static_cast<size_t>(type)],
                a.dimension_id));
        }
    }

    return merged;
}

// --- Pollution / temperature setters ---

void RegionSystem::set_pollution(
    RegionType type, uint64_t region_id, double level) {
    auto* graph = get_graph(type);
    if (!graph) return;

    auto* data = graph->get_region_data_mut(region_id);
    if (!data) return;

    double old_level = data->pollution;
    data->pollution = std::clamp(level, 0.0, 1.0);

    if (event_bus_ && std::abs(data->pollution - old_level) > 1e-6) {
        event_bus_->enqueue(GameEvent::region_pollution_changed(
            region_id, old_level, data->pollution,
            data->dimension_id));
    }
}

void RegionSystem::set_temperature(
    RegionType type, uint64_t region_id, double temp) {
    auto* graph = get_graph(type);
    if (!graph) return;

    auto* data = graph->get_region_data_mut(region_id);
    if (!data) return;

    double old_temp = data->temperature;
    data->temperature = temp;

    if (event_bus_ && std::abs(data->temperature - old_temp) > 1e-6) {
        event_bus_->enqueue(GameEvent::region_temperature_changed(
            region_id, old_temp, data->temperature,
            data->dimension_id));
    }
}

// --- Query ---

size_t RegionSystem::total_region_count() const {
    size_t count = 0;
    for (const auto& graph : graphs_) {
        if (graph) count += graph->region_count();
    }
    return count;
}

size_t RegionSystem::region_count(RegionType type) const {
    const auto* graph = get_graph(type);
    return graph ? graph->region_count() : 0;
}

// --- Private ---

void RegionSystem::rebuild_dirty_regions() {
    for (size_t i = 0; i < static_cast<size_t>(RegionType::COUNT); ++i) {
        auto* graph = graphs_[i].get();
        if (!graph || graph->dirty_regions().empty()) continue;

        auto type = static_cast<RegionType>(i);
        auto adj_fn = make_adjacency_fn(type);

        auto rebuilt = graph->rebuild_dirty(adj_fn);

        // Emit split events for newly created regions.
        if (event_bus_) {
            for (uint64_t rid : rebuilt) {
                auto* data = graph->get_region_data(rid);
                if (data && data->node_count > 0) {
                    event_bus_->enqueue(GameEvent::region_split(
                        rid, rid,
                        kRegionTypeNames[i],
                        data->dimension_id));
                }
            }
        }
    }
}

void RegionSystem::simulate_chunk(const ChunkKey& chunk, float delta) {
    (void)delta;

    // Run pollution diffusion and temperature conduction for
    // the pollution and temperature region types.
    auto* poll_graph = get_graph(RegionType::POLLUTION);
    if (poll_graph && poll_graph->region_count() > 0) {
        auto neighbor_fn = make_region_neighbor_fn(RegionType::POLLUTION);
        poll_graph->tick_pollution(neighbor_fn, pollution_diffusion_rate_);
    }

    auto* temp_graph = get_graph(RegionType::TEMPERATURE);
    if (temp_graph && temp_graph->region_count() > 0) {
        auto neighbor_fn = make_region_neighbor_fn(RegionType::TEMPERATURE);
        temp_graph->tick_temperature(neighbor_fn, temperature_conduction_rate_);
    }
}

std::function<std::vector<RegionNode>(const RegionNode&)>
RegionSystem::make_adjacency_fn(RegionType type) const {
    const auto* graph = get_graph(type);

    return [graph](const RegionNode& node) -> std::vector<RegionNode> {
        std::vector<RegionNode> neighbors;

        if (!graph) return neighbors;

        // 6-connected neighbors (Manhattan adjacency).
        static const int kDx[] = {1, -1, 0, 0, 0, 0};
        static const int kDy[] = {0, 0, 1, -1, 0, 0};
        static const int kDz[] = {0, 0, 0, 0, 1, -1};

        for (int d = 0; d < 6; ++d) {
            RegionNode nb;
            nb.dimension_id = node.dimension_id;
            nb.block_x = node.block_x + kDx[d];
            nb.block_y = node.block_y + kDy[d];
            nb.block_z = node.block_z + kDz[d];

            if (graph->has_node(nb)) {
                neighbors.push_back(nb);
            }
        }

        return neighbors;
    };
}

std::function<std::vector<uint64_t>(uint64_t)>
RegionSystem::make_region_neighbor_fn(RegionType type) const {
    const auto* graph = get_graph(type);

    return [graph](uint64_t region_id) -> std::vector<uint64_t> {
        std::vector<uint64_t> neighbors;

        if (!graph) return neighbors;

        // Collect unique adjacent region IDs by scanning all nodes
        // in this region and checking their neighbors.
        std::unordered_set<uint64_t> seen;
        seen.insert(region_id);

        for (const auto& [node, rid] : graph->node_map()) {
            if (graph->find_region(node) != region_id) continue;

            static const int kDx[] = {1, -1, 0, 0, 0, 0};
            static const int kDy[] = {0, 0, 1, -1, 0, 0};
            static const int kDz[] = {0, 0, 0, 0, 1, -1};

            for (int d = 0; d < 6; ++d) {
                RegionNode nb;
                nb.dimension_id = node.dimension_id;
                nb.block_x = node.block_x + kDx[d];
                nb.block_y = node.block_y + kDy[d];
                nb.block_z = node.block_z + kDz[d];

                if (graph->has_node(nb)) {
                    uint64_t nb_rid = graph->find_region(nb);
                    if (nb_rid != 0 && seen.find(nb_rid) == seen.end()) {
                        seen.insert(nb_rid);
                        neighbors.push_back(nb_rid);
                    }
                }
            }
        }

        return neighbors;
    };
}

} // namespace science_and_theology
