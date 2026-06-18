#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "simulation_system.hpp"
#include "region_graph.hpp"

namespace science_and_theology {

// Region simulation subsystem.
// Manages multiple RegionGraphs (one per RegionType) and drives
// pollution diffusion, temperature conduction, and split detection
// each tick.
//
// Priority 5 — runs after Machine (2) so that machine state changes
// can trigger region rebuilds in the same frame.
//
// Design:
//   - Maintains one RegionGraph per RegionType.
//   - On tick_active(), rebuilds dirty regions and runs pollution/
//     temperature simulation for active chunks.
//   - On tick_sleeping(), runs a simplified low-frequency simulation.
//   - Emits region lifecycle events (created, destroyed, merged, split)
//     and value change events (pollution, temperature) via EventBus.
//
// Thread safety: main thread only. Not thread-safe.
class RegionSystem : public SimulationSystem {
public:
    RegionSystem();
    ~RegionSystem() override = default;

    SIMULATION_SYSTEM_NAME(RegionSystem, "RegionSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta,
                     const TickContext* ctx = nullptr) override;
    void tick_sleeping(const ChunkKey& chunk, float delta,
                       const TickContext* ctx = nullptr) override;
    void shutdown() override;

    // Runs after Machine (priority 2), before Season (priority 6).
    int priority() const override { return 5; }

    // --- Graph access ---

    // Returns the RegionGraph for a given type, or nullptr.
    RegionGraph* get_graph(RegionType type);
    const RegionGraph* get_graph(RegionType type) const;

    // --- Node management (convenience) ---

    // Add a node to a specific region type graph.
    uint64_t add_node(RegionType type, const RegionNode& node);

    // Remove a node from a specific region type graph.
    void remove_node(RegionType type, const RegionNode& node);

    // Link two nodes in a specific region type graph.
    uint64_t link(RegionType type, const RegionNode& a, const RegionNode& b);

    // --- Pollution / temperature setters ---

    // Set pollution level for a region. Emits event if changed.
    void set_pollution(RegionType type, uint64_t region_id, double level);

    // Set temperature for a region. Emits event if changed.
    void set_temperature(RegionType type, uint64_t region_id, double temp);

    // --- Simulation parameters ---

    // Pollution diffusion rate [0, 1]. Higher = faster spread.
    float pollution_diffusion_rate() const { return pollution_diffusion_rate_; }
    void set_pollution_diffusion_rate(float rate) {
        pollution_diffusion_rate_ = rate;
    }

    // Temperature conduction rate [0, 1]. Higher = faster equalization.
    float temperature_conduction_rate() const {
        return temperature_conduction_rate_;
    }
    void set_temperature_conduction_rate(float rate) {
        temperature_conduction_rate_ = rate;
    }

    // --- Query ---

    // Returns the total number of regions across all types.
    size_t total_region_count() const;

    // Returns the number of regions for a specific type.
    size_t region_count(RegionType type) const;

private:
    // Rebuild dirty regions for all graphs and emit lifecycle events.
    void rebuild_dirty_regions();

    // Run pollution and temperature simulation for a chunk.
    void simulate_chunk(const ChunkKey& chunk, float delta);

    // Build the adjacency function for a given region type.
    // This looks up 6-connected neighbors in the graph.
    std::function<std::vector<RegionNode>(const RegionNode&)>
    make_adjacency_fn(RegionType type) const;

    // Build the region neighbor function for a given region type.
    // Returns the set of region IDs adjacent to a given region.
    std::function<std::vector<uint64_t>(uint64_t)>
    make_region_neighbor_fn(RegionType type) const;

    // One graph per RegionType.
    std::array<std::unique_ptr<RegionGraph>,
               static_cast<size_t>(RegionType::COUNT)> graphs_;

    // Simulation parameters.
    float pollution_diffusion_rate_ = 0.1f;
    float temperature_conduction_rate_ = 0.05f;

    // Track which chunks have been processed this tick to avoid
    // duplicate processing.
    std::unordered_set<ChunkKey> processed_chunks_;
};

} // namespace science_and_theology
