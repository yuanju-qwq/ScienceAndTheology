// Game-owned topology and scalar-field simulation implementation.

#include "game/simulation/region_topology.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace snt::game {

namespace {

constexpr double kPollutionDiffusionRate = 0.1;
constexpr double kTemperatureConductionRate = 0.05;

constexpr int32_t kNeighborOffsets[][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
    {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

bool node_less(const RegionNode& first, const RegionNode& second) noexcept {
    if (first.dimension_id != second.dimension_id) {
        return first.dimension_id < second.dimension_id;
    }
    if (first.block_x != second.block_x) return first.block_x < second.block_x;
    if (first.block_y != second.block_y) return first.block_y < second.block_y;
    return first.block_z < second.block_z;
}

}  // namespace

struct RegionTopology::Graph {
    using NeighborSet = std::unordered_set<RegionNode, RegionNodeHash>;

    std::unordered_map<RegionNode, RegionId, RegionNodeHash> node_to_region;
    std::unordered_map<RegionNode, NeighborSet, RegionNodeHash> links;
    std::unordered_map<RegionId, RegionState> regions;
    std::unordered_map<RegionId, RegionId> parent;
    std::unordered_map<RegionId, uint8_t> rank;
    std::unordered_set<RegionId> dirty_regions;
    RegionId next_region_id = 1;
};

std::string_view region_domain_name(RegionDomain domain) noexcept {
    switch (domain) {
    case RegionDomain::kPower:
        return "power";
    case RegionDomain::kFluid:
        return "fluid";
    case RegionDomain::kPollution:
        return "pollution";
    case RegionDomain::kTemperature:
        return "temperature";
    case RegionDomain::kCount:
        break;
    }
    return "invalid";
}

size_t RegionNodeHash::operator()(const RegionNode& node) const noexcept {
    size_t hash = std::hash<std::string>{}(node.dimension_id);
    const auto combine = [&hash](size_t value) {
        hash ^= value + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    };
    combine(std::hash<int32_t>{}(node.block_x));
    combine(std::hash<int32_t>{}(node.block_y));
    combine(std::hash<int32_t>{}(node.block_z));
    return hash;
}

RegionTopology::RegionTopology() {
    for (auto& domain_graph : graphs_) {
        domain_graph = std::make_unique<Graph>();
    }
}

RegionTopology::~RegionTopology() = default;

void RegionTopology::set_event_sink(IRegionTopologyEventSink* event_sink) noexcept {
    event_sink_ = event_sink;
}

RegionHandle RegionTopology::add_node(RegionDomain domain, const RegionNode& node) {
    Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr || node.dimension_id.empty()) return {};

    const auto existing = domain_graph->node_to_region.find(node);
    if (existing != domain_graph->node_to_region.end()) {
        return RegionHandle{domain, find_root(*domain_graph, existing->second)};
    }

    const RegionId region_id = domain_graph->next_region_id++;
    domain_graph->parent.emplace(region_id, region_id);
    domain_graph->rank.emplace(region_id, 0);
    domain_graph->node_to_region.emplace(node, region_id);
    domain_graph->links.try_emplace(node);

    RegionState state;
    state.id = region_id;
    state.domain = domain;
    state.dimension_id = node.dimension_id;
    state.node_count = 1;
    domain_graph->regions.emplace(region_id, std::move(state));

    notify(RegionTopologyEvent{
        .kind = RegionTopologyEventKind::kCreated,
        .region = RegionHandle{domain, region_id},
        .dimension_id = node.dimension_id,
    });
    return RegionHandle{domain, region_id};
}

bool RegionTopology::remove_node(RegionDomain domain, const RegionNode& node) {
    Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr) return false;

    const auto node_it = domain_graph->node_to_region.find(node);
    if (node_it == domain_graph->node_to_region.end()) return false;

    const RegionId region_id = find_root(*domain_graph, node_it->second);
    if (const auto links_it = domain_graph->links.find(node);
        links_it != domain_graph->links.end()) {
        for (const RegionNode& neighbor : links_it->second) {
            if (const auto neighbor_links = domain_graph->links.find(neighbor);
                neighbor_links != domain_graph->links.end()) {
                neighbor_links->second.erase(node);
            }
        }
        domain_graph->links.erase(links_it);
    }
    domain_graph->node_to_region.erase(node_it);

    const auto state_it = domain_graph->regions.find(region_id);
    if (state_it == domain_graph->regions.end()) return false;
    if (state_it->second.node_count > 0) --state_it->second.node_count;

    if (state_it->second.node_count == 0) {
        const std::string dimension_id = state_it->second.dimension_id;
        domain_graph->regions.erase(state_it);
        domain_graph->dirty_regions.erase(region_id);
        notify(RegionTopologyEvent{
            .kind = RegionTopologyEventKind::kDestroyed,
            .region = RegionHandle{domain, region_id},
            .dimension_id = dimension_id,
        });
    } else {
        domain_graph->dirty_regions.insert(region_id);
    }
    return true;
}

RegionHandle RegionTopology::connect(RegionDomain domain,
                                     const RegionNode& first,
                                     const RegionNode& second) {
    Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr || first.dimension_id != second.dimension_id) return {};

    const auto first_it = domain_graph->node_to_region.find(first);
    const auto second_it = domain_graph->node_to_region.find(second);
    if (first_it == domain_graph->node_to_region.end() ||
        second_it == domain_graph->node_to_region.end()) {
        return {};
    }

    if (first == second) {
        return RegionHandle{domain, find_root(*domain_graph, first_it->second)};
    }

    domain_graph->links[first].insert(second);
    domain_graph->links[second].insert(first);

    const RegionId first_root = find_root(*domain_graph, first_it->second);
    const RegionId second_root = find_root(*domain_graph, second_it->second);
    if (first_root == second_root) return RegionHandle{domain, first_root};

    const RegionId merged_root = unite(*domain_graph, first_root, second_root);
    const RegionId absorbed_root = merged_root == first_root ? second_root : first_root;
    auto merged_it = domain_graph->regions.find(merged_root);
    auto absorbed_it = domain_graph->regions.find(absorbed_root);
    if (merged_it == domain_graph->regions.end() || absorbed_it == domain_graph->regions.end()) {
        return {};
    }

    RegionState& merged = merged_it->second;
    const RegionState absorbed = absorbed_it->second;
    const size_t merged_count = merged.node_count;
    const size_t total_count = merged_count + absorbed.node_count;
    if (total_count > 0) {
        const double merged_weight = static_cast<double>(merged_count) /
                                     static_cast<double>(total_count);
        const double absorbed_weight = static_cast<double>(absorbed.node_count) /
                                      static_cast<double>(total_count);
        merged.pollution = merged.pollution * merged_weight + absorbed.pollution * absorbed_weight;
        merged.temperature_celsius = merged.temperature_celsius * merged_weight +
                                     absorbed.temperature_celsius * absorbed_weight;
    }
    merged.node_count = total_count;
    domain_graph->regions.erase(absorbed_it);

    const bool first_was_dirty = domain_graph->dirty_regions.erase(first_root) > 0;
    const bool second_was_dirty = domain_graph->dirty_regions.erase(second_root) > 0;
    const bool had_dirty_region = first_was_dirty || second_was_dirty;
    if (had_dirty_region) domain_graph->dirty_regions.insert(merged_root);

    notify(RegionTopologyEvent{
        .kind = RegionTopologyEventKind::kMerged,
        .region = RegionHandle{domain, merged_root},
        .related_region = RegionHandle{domain, absorbed_root},
        .dimension_id = merged.dimension_id,
    });
    return RegionHandle{domain, merged_root};
}

bool RegionTopology::disconnect(RegionDomain domain,
                                const RegionNode& first,
                                const RegionNode& second) {
    Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr || first.dimension_id != second.dimension_id || first == second) {
        return false;
    }

    const auto first_it = domain_graph->node_to_region.find(first);
    const auto second_it = domain_graph->node_to_region.find(second);
    if (first_it == domain_graph->node_to_region.end() ||
        second_it == domain_graph->node_to_region.end()) {
        return false;
    }

    const auto first_links = domain_graph->links.find(first);
    if (first_links == domain_graph->links.end() || first_links->second.erase(second) == 0) {
        return false;
    }
    if (const auto second_links = domain_graph->links.find(second);
        second_links != domain_graph->links.end()) {
        second_links->second.erase(first);
    }
    domain_graph->dirty_regions.insert(find_root(*domain_graph, first_it->second));
    return true;
}

std::optional<RegionHandle> RegionTopology::region_for_node(
    RegionDomain domain, const RegionNode& node) const {
    const Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr) return std::nullopt;

    const auto node_it = domain_graph->node_to_region.find(node);
    if (node_it == domain_graph->node_to_region.end()) return std::nullopt;
    return RegionHandle{domain, find_root(*domain_graph, node_it->second)};
}

const RegionState* RegionTopology::find_state(RegionHandle handle) const {
    const Graph* domain_graph = graph(handle.domain);
    if (domain_graph == nullptr || !handle.valid()) return nullptr;

    const RegionId root = find_root(*domain_graph, handle.id);
    const auto state_it = domain_graph->regions.find(root);
    return state_it == domain_graph->regions.end() ? nullptr : &state_it->second;
}

size_t RegionTopology::region_count(RegionDomain domain) const noexcept {
    const Graph* domain_graph = graph(domain);
    return domain_graph == nullptr ? 0 : domain_graph->regions.size();
}

size_t RegionTopology::node_count(RegionDomain domain) const noexcept {
    const Graph* domain_graph = graph(domain);
    return domain_graph == nullptr ? 0 : domain_graph->node_to_region.size();
}

bool RegionTopology::set_pollution(RegionHandle handle, double concentration) {
    if (handle.domain != RegionDomain::kPollution || !std::isfinite(concentration)) {
        return false;
    }
    const RegionState* state = find_state(handle);
    if (state == nullptr) return false;

    Graph* domain_graph = graph(handle.domain);
    const RegionId root = find_root(*domain_graph, handle.id);
    domain_graph->regions.at(root).pollution = std::clamp(concentration, 0.0, 1.0);
    return true;
}

bool RegionTopology::set_temperature(RegionHandle handle, double celsius) {
    if (handle.domain != RegionDomain::kTemperature || !std::isfinite(celsius)) {
        return false;
    }
    const RegionState* state = find_state(handle);
    if (state == nullptr) return false;

    Graph* domain_graph = graph(handle.domain);
    const RegionId root = find_root(*domain_graph, handle.id);
    domain_graph->regions.at(root).temperature_celsius = celsius;
    return true;
}

bool RegionTopology::fixed_tick(uint64_t source_tick) {
    if (last_fixed_tick_.has_value() && source_tick <= *last_fixed_tick_) return false;

    for (size_t index = 0; index < kDomainCount; ++index) {
        rebuild_dirty_regions(static_cast<RegionDomain>(index), *graphs_[index]);
    }
    diffuse_scalar_field(RegionDomain::kPollution, kPollutionDiffusionRate, true);
    diffuse_scalar_field(RegionDomain::kTemperature, kTemperatureConductionRate, false);
    last_fixed_tick_ = source_tick;
    return true;
}

void RegionTopology::clear() noexcept {
    for (auto& domain_graph : graphs_) {
        domain_graph->node_to_region.clear();
        domain_graph->links.clear();
        domain_graph->regions.clear();
        domain_graph->parent.clear();
        domain_graph->rank.clear();
        domain_graph->dirty_regions.clear();
        domain_graph->next_region_id = 1;
    }
    last_fixed_tick_.reset();
}

RegionTopology::Graph* RegionTopology::graph(RegionDomain domain) noexcept {
    if (!is_region_domain(domain)) return nullptr;
    return graphs_[static_cast<size_t>(domain)].get();
}

const RegionTopology::Graph* RegionTopology::graph(RegionDomain domain) const noexcept {
    if (!is_region_domain(domain)) return nullptr;
    return graphs_[static_cast<size_t>(domain)].get();
}

RegionId RegionTopology::find_root(const Graph& graph, RegionId id) const noexcept {
    auto parent_it = graph.parent.find(id);
    while (parent_it != graph.parent.end() && parent_it->second != id) {
        id = parent_it->second;
        parent_it = graph.parent.find(id);
    }
    return id;
}

RegionId RegionTopology::unite(Graph& graph, RegionId first, RegionId second) noexcept {
    RegionId first_root = find_root(graph, first);
    RegionId second_root = find_root(graph, second);
    if (first_root == second_root) return first_root;

    uint8_t first_rank = graph.rank[first_root];
    uint8_t second_rank = graph.rank[second_root];
    if (first_rank < second_rank ||
        (first_rank == second_rank && second_root < first_root)) {
        std::swap(first_root, second_root);
        std::swap(first_rank, second_rank);
    }
    graph.parent[second_root] = first_root;
    if (first_rank == second_rank) ++graph.rank[first_root];
    return first_root;
}

void RegionTopology::rebuild_dirty_regions(RegionDomain domain, Graph& domain_graph) {
    if (domain_graph.dirty_regions.empty()) return;

    std::vector<RegionId> dirty_region_ids(
        domain_graph.dirty_regions.begin(), domain_graph.dirty_regions.end());
    std::sort(dirty_region_ids.begin(), dirty_region_ids.end());
    domain_graph.dirty_regions.clear();

    std::vector<RegionTopologyEvent> events;
    for (const RegionId requested_id : dirty_region_ids) {
        const RegionId region_id = find_root(domain_graph, requested_id);
        const auto state_it = domain_graph.regions.find(region_id);
        if (state_it == domain_graph.regions.end()) continue;

        std::unordered_set<RegionNode, RegionNodeHash> remaining;
        for (const auto& [node, node_region_id] : domain_graph.node_to_region) {
            if (find_root(domain_graph, node_region_id) == region_id) {
                remaining.insert(node);
            }
        }
        if (remaining.empty()) {
            domain_graph.regions.erase(region_id);
            continue;
        }

        std::vector<std::vector<RegionNode>> components;
        while (!remaining.empty()) {
            const auto seed_it = std::min_element(
                remaining.begin(), remaining.end(), node_less);
            RegionNode seed = *seed_it;
            remaining.erase(seed_it);

            std::queue<RegionNode> pending;
            pending.push(seed);
            std::vector<RegionNode> component;
            while (!pending.empty()) {
                RegionNode current = std::move(pending.front());
                pending.pop();
                component.push_back(current);

                const auto links_it = domain_graph.links.find(current);
                if (links_it == domain_graph.links.end()) continue;
                for (const RegionNode& neighbor : links_it->second) {
                    if (remaining.erase(neighbor) > 0) pending.push(neighbor);
                }
            }
            std::sort(component.begin(), component.end(), node_less);
            components.push_back(std::move(component));
        }
        std::sort(components.begin(), components.end(), [](const auto& first, const auto& second) {
            return node_less(first.front(), second.front());
        });

        RegionState& retained_state = state_it->second;
        retained_state.node_count = components.front().size();
        for (const RegionNode& node : components.front()) {
            domain_graph.node_to_region[node] = region_id;
        }
        if (components.size() == 1) continue;

        const RegionState split_source = retained_state;
        for (size_t index = 1; index < components.size(); ++index) {
            const RegionId new_region_id = domain_graph.next_region_id++;
            domain_graph.parent.emplace(new_region_id, new_region_id);
            domain_graph.rank.emplace(new_region_id, 0);

            RegionState split_state = split_source;
            split_state.id = new_region_id;
            split_state.node_count = components[index].size();
            domain_graph.regions.emplace(new_region_id, std::move(split_state));
            for (const RegionNode& node : components[index]) {
                domain_graph.node_to_region[node] = new_region_id;
            }
            events.push_back(RegionTopologyEvent{
                .kind = RegionTopologyEventKind::kSplit,
                .region = RegionHandle{domain, region_id},
                .related_region = RegionHandle{domain, new_region_id},
                .dimension_id = retained_state.dimension_id,
            });
        }
    }

    for (const RegionTopologyEvent& event : events) notify(event);
}

void RegionTopology::diffuse_scalar_field(RegionDomain domain,
                                           double rate,
                                           bool clamp_to_unit_interval) {
    Graph* domain_graph = graph(domain);
    if (domain_graph == nullptr || domain_graph->regions.empty()) return;

    std::unordered_map<RegionId, double> next_values;
    next_values.reserve(domain_graph->regions.size());
    for (const auto& [region_id, state] : domain_graph->regions) {
        const double value = domain == RegionDomain::kPollution
            ? state.pollution
            : state.temperature_celsius;
        const std::vector<RegionId> neighbors = neighboring_regions(
            domain, *domain_graph, region_id);
        if (neighbors.empty()) {
            next_values.emplace(region_id, value);
            continue;
        }

        double total = value;
        for (const RegionId neighbor_id : neighbors) {
            const auto neighbor_it = domain_graph->regions.find(neighbor_id);
            if (neighbor_it == domain_graph->regions.end()) continue;
            total += domain == RegionDomain::kPollution
                ? neighbor_it->second.pollution
                : neighbor_it->second.temperature_celsius;
        }
        const double average = total / static_cast<double>(neighbors.size() + 1);
        next_values.emplace(region_id, value + (average - value) * rate);
    }

    for (auto& [region_id, state] : domain_graph->regions) {
        const auto next_it = next_values.find(region_id);
        if (next_it == next_values.end()) continue;
        const double next_value = clamp_to_unit_interval
            ? std::clamp(next_it->second, 0.0, 1.0)
            : next_it->second;
        if (domain == RegionDomain::kPollution) {
            state.pollution = next_value;
        } else {
            state.temperature_celsius = next_value;
        }
    }
}

std::vector<RegionId> RegionTopology::neighboring_regions(
    RegionDomain domain, const Graph& domain_graph, RegionId region_id) const {
    std::unordered_set<RegionId> neighbors;
    for (const auto& [node, node_region_id] : domain_graph.node_to_region) {
        if (find_root(domain_graph, node_region_id) != region_id) continue;
        for (const auto& offset : kNeighborOffsets) {
            RegionNode neighbor = node;
            neighbor.block_x += offset[0];
            neighbor.block_y += offset[1];
            neighbor.block_z += offset[2];
            const auto neighbor_it = domain_graph.node_to_region.find(neighbor);
            if (neighbor_it == domain_graph.node_to_region.end()) continue;
            const RegionId neighbor_region_id = find_root(domain_graph, neighbor_it->second);
            if (neighbor_region_id != region_id) neighbors.insert(neighbor_region_id);
        }
    }

    std::vector<RegionId> result(neighbors.begin(), neighbors.end());
    std::sort(result.begin(), result.end());
    return result;
}

void RegionTopology::notify(const RegionTopologyEvent& event) const {
    if (event_sink_ != nullptr) event_sink_->on_region_topology_event(event);
}

}  // namespace snt::game
