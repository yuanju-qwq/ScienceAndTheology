#pragma once

#include <unordered_map>
#include <vector>

#include "simulation_system.hpp"
#include "population_cell.hpp"
#include "ecosystem_params.hpp"
#include "season_def.hpp"

namespace science_and_theology {

// ============================================================
// EcosystemSystem — population-density ecosystem simulation
// ============================================================
//
// Implements a hybrid (方案C) ecosystem using per-chunk PopulationCell
// density tracking with optional proxy creature entities for
// visual representation in active chunks.
//
// P0 scope: density equation advancement + diffusion.
//   - No proxy creature entities yet (P3-P4).
//   - No Godot binding yet (P5).
//
// Design:
//   - Each chunk with natural terrain has a PopulationCell.
//   - tick_active(): advance Lotka-Volterra equations for one tick.
//   - tick_sleeping(): same-speed low-frequency advancement
//     (same equations, lower cadence, same delta accumulation).
//   - Diffusion: every diffusion_interval_ticks, population
//     densities diffuse between adjacent chunks.
//   - Seasonal modulation: reads SeasonSystem's cached season
//     to adjust growth/reproduction/water parameters.
//
// Priority: 7 (runs after Season at 6, before TreeGrowth at 8).
// This ensures the current season is up-to-date when ecosystem
// equations run, and vegetation_density is available for
// TreeGrowthSystem to query.
//
// Thread safety: main thread only. Not thread-safe.

class EcosystemSystem : public SimulationSystem {
public:
    EcosystemSystem() = default;

    SIMULATION_SYSTEM_NAME(EcosystemSystem, "EcosystemSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    // Runs after Season (priority 6) so season is current.
    // Runs before TreeGrowth (priority 8) so vegetation_density
    // is available for tree growth decisions.
    int priority() const override { return 7; }

    // --- Configuration ---

    // Mutable reference to ecosystem parameters.
    EcosystemParams& params() { return params_; }
    const EcosystemParams& params() const { return params_; }

    void set_params(const EcosystemParams& p) { params_ = p; }

    // --- Population cell access ---

    // Get the population cell for a chunk, or nullptr if not tracked.
    PopulationCell* get_population_cell(const ChunkKey& key);
    const PopulationCell* get_population_cell(const ChunkKey& key) const;

    // Ensure a population cell exists for a chunk. Returns reference.
    // If the chunk has no cell yet, one is created with defaults
    // adjusted by biome override (if available).
    PopulationCell& ensure_population_cell(const ChunkKey& key);

    // Remove the population cell for a chunk (e.g., when chunk unloads).
    void remove_population_cell(const ChunkKey& key);

    // Returns the number of tracked population cells.
    size_t population_cell_count() const { return populations_.size(); }

    // --- Season integration ---

    // Set the current season (called externally, typically by
    // SeasonSystem or by reading SeasonSystem's cached state).
    void set_current_season(Season season) { current_season_ = season; }
    Season current_season() const { return current_season_; }

    // --- Day/night integration ---

    // Set whether it is currently daytime.
    // Called internally from tick_active() by computing time-of-day
    // from WorldData tick counter.
    void set_is_daytime(bool daytime) { is_daytime_ = daytime; }
    bool is_daytime() const { return is_daytime_; }

    // --- Diffusion ---

    // Run one diffusion pass over all population cells.
    // Moves density from high to low between adjacent chunks.
    // Called automatically at diffusion_interval_ticks cadence.
    void diffuse_populations();

    // Returns the number of diffusion passes performed.
    int64_t diffusion_count() const { return diffusion_count_; }

    // --- Proxy creature management ---

    // Proxy creature tracking per chunk.
    struct ProxyGroup {
        std::vector<EntityId> herbivore_ids;
        std::vector<EntityId> predator_ids;
    };

    // Get the proxy group for a chunk, or nullptr.
    const ProxyGroup* get_proxy_group(const ChunkKey& key) const;

    // Returns the total number of active proxy creatures.
    size_t total_proxy_count() const;

    // Spawn proxy creatures for a chunk based on its PopulationCell density.
    // Called when a chunk first becomes active or when rebalancing.
    void spawn_proxies_for_chunk(const ChunkKey& chunk, int64_t tick);

    // Despawn all proxy creatures for a chunk.
    // Called when a chunk transitions to sleeping.
    void despawn_proxies_for_chunk(const ChunkKey& chunk);

    // Rebalance proxy count for a chunk: add or remove proxies
    // to match the current PopulationCell density.
    void rebalance_proxies(const ChunkKey& chunk, int64_t tick);

    // Update AI for all proxy creatures in a chunk.
    // Handles wandering, fleeing, and position updates.
    void tick_proxies(const ChunkKey& chunk, int64_t tick, float delta);

    // --- Diagnostics ---

    // Returns the total vegetation density across all tracked chunks.
    float total_vegetation() const;

    // Returns the total herbivore density across all tracked chunks.
    float total_herbivore() const;

    // Returns the total predator density across all tracked chunks.
    float total_predator() const;

private:
    // Advance the population equations for a single cell by dt ticks.
    // dt is in simulation ticks (not seconds).
    void advance_population(PopulationCell& cell, float dt);

    // Resolve effective parameters for a population cell,
    // applying biome overrides if available.
    struct ResolvedParams {
        float veg_growth_rate;
        float veg_decay_rate;
        float graze_rate;
        float herb_repro_rate;
        float herb_natural_death;
        float predation_rate;
        float pred_repro_rate;
        float pred_natural_death;
        float decompose_rate;
        float death_to_biomass_fraction;
        float veg_decay_to_biomass_fraction;
        float max_vegetation;
        float max_herbivore;
        float max_predator;
        float base_water;
    };

    ResolvedParams resolve_params(const PopulationCell& cell) const;

    // Get the 6-axis neighbor keys for a chunk (±x, ±y, ±z).
    std::vector<ChunkKey> get_neighbor_keys(const ChunkKey& key) const;

    // Compute the desired number of proxy creatures for a given density.
    int density_to_proxy_count(float density, float min_threshold) const;

    // Compute a random spawn position within a chunk.
    void random_spawn_position_in_chunk(
        const ChunkKey& chunk,
        int32_t& out_x, int32_t& out_y, int32_t& out_z) const;

    // Pick a random wander target near the creature's current position.
    void pick_wander_target(
        CreatureBlockEntityState& creature) const;

    // Check if a herbivore should flee from nearby predators.
    // Returns true if a predator is within flee_detection_radius.
    bool check_flee_for_herbivore(
        CreatureBlockEntityState& herbivore,
        const ProxyGroup& group) const;

    // Move a creature toward its target by the given speed.
    void move_creature_toward_target(
        CreatureBlockEntityState& creature,
        float speed, float dt) const;

    // Ecosystem parameters (tunable at runtime).
    EcosystemParams params_;

    // Per-chunk population data. Owned by this system (方案B).
    std::unordered_map<ChunkKey, PopulationCell> populations_;

    // Per-chunk proxy creature tracking. Only populated for active chunks.
    std::unordered_map<ChunkKey, ProxyGroup> active_proxies_;

    // Cached season state, updated each tick.
    Season current_season_ = Season::SPRING;

    // Cached day/night state, updated each tick.
    bool is_daytime_ = true;

    // Diffusion tracking.
    int64_t diffusion_count_ = 0;

    // Diffusion budget: tracks how many pairs processed in current pass.
    int diffusion_pairs_processed_ = 0;
};

} // namespace science_and_theology
