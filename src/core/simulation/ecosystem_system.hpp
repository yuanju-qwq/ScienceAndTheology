#pragma once

#include <unordered_map>
#include <vector>

#include "simulation_system.hpp"
#include "population_cell.hpp"
#include "ecosystem_params.hpp"
#include "season_def.hpp"
#include "creature_species.hpp"
#include "captive_creature.hpp"

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
    void tick_active(const ChunkKey& chunk, float delta,
                     const TickContext* ctx = nullptr) override;
    void tick_sleeping(const ChunkKey& chunk, float delta,
                       const TickContext* ctx = nullptr) override;
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

    // --- Species registry ---

    // Access the creature species registry.
    CreatureSpeciesRegistry& species_registry() { return species_registry_; }
    const CreatureSpeciesRegistry& species_registry() const { return species_registry_; }

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

    // --- Player combat (hunting) ---

    // Result of an attack attempt on a proxy creature.
    struct AttackResult {
        // Whether the attack hit a valid creature.
        bool hit = false;
        // Whether the creature was killed by this attack.
        bool killed = false;
        // The entity ID of the attacked creature (0 if miss).
        uint64_t creature_id = 0;
        // Species ID of the attacked creature.
        uint16_t species_id = 0;
        // Damage dealt (clamped to remaining health).
        float damage_dealt = 0.0f;
        // Health remaining after damage [0, 1].
        float remaining_health = 0.0f;
        // Chunk key where the creature resides.
        ChunkKey chunk_key;
    };

    // Attempt to attack a creature near the player's look direction.
    // player_pos: player world position (x, y, z packed into float3).
    // look_dir: normalized player look direction.
    // reach: maximum attack distance in blocks.
    // damage: damage amount [0, 1] to apply.
    // tick: current simulation tick.
    // Returns AttackResult with hit/kill info.
    AttackResult attack_creature(
        const std::string& dimension,
        float player_x, float player_y, float player_z,
        float look_dir_x, float look_dir_y, float look_dir_z,
        float reach, float damage, int64_t tick);

    // Apply damage to a specific creature by EntityId.
    // Returns AttackResult (hit=false if entity not found or not a creature).
    // This is the lower-level API used by attack_creature() internally
    // and can also be called directly for targeted effects.
    AttackResult apply_damage_to_creature(
        EntityId creature_id, float damage, int64_t tick);

    // --- Player stewardship (feeding) ---

    // Feed creatures in a chunk, increasing population density.
    // role: which population to boost (HERBIVORE or PREDATOR).
    // amount: positive value added to the role's density.
    // Triggers proxy rebalance so new creatures appear immediately.
    // Returns true if the chunk had a population cell.
    bool feed_creatures(const ChunkKey& key, CreatureRole role, float amount);

    // --- Captive / husbandry ---

    // Result of feeding a specific creature (targeted by view cone).
    struct FeedResult {
        // Whether a creature was hit by the feed attempt.
        bool hit = false;
        // Runtime id of the targeted creature (proxy entity or captive).
        uint64_t creature_id = 0;
        // Species id of the targeted creature.
        uint16_t species_id = 0;
        // Outcome of the feed action:
        //   "miss"        — no creature targeted.
        //   "captured"    — a wild creature was captured into a pen.
        //   "taming"      — fed an untamed captive (taming boosted).
        //   "bred"        — fed a tamed adult, breeding started.
        //   "fed"         — fed a tamed adult with no partner / on cooldown.
        //   "no_enclosure"— wild creature not inside an enclosure.
        //   "pen_full"    — pen at capacity, cannot capture.
        std::string outcome = "miss";
        // Chunk where the targeted creature resides.
        ChunkKey chunk_key;
    };

    // Attempt to feed / interact with a creature the player is aiming at.
    // If the target is a wild proxy inside a fence enclosure, it is
    // captured (detached from the wild population). If the target is a
    // captive creature, feeding boosts taming or triggers breeding.
    // Returns a FeedResult describing the outcome.
    FeedResult feed_creature_at(
        const std::string& dimension,
        float player_x, float player_y, float player_z,
        float look_dir_x, float look_dir_y, float look_dir_z,
        float reach);

    // Returns the total number of captive creatures across all chunks.
    size_t total_captive_count() const;

    // Returns captive creature data for a chunk as an Array-like vector.
    // Each entry: { runtime_id, species_id, age_stage, is_tamed,
    //               is_pregnant, pos_x/y/z }
    struct CaptiveInfo {
        uint64_t runtime_id = 0;
        uint16_t species_id = 0;
        uint8_t age_stage = 0;   // 0=BABY, 1=ADULT
        bool is_tamed = false;
        bool is_pregnant = false;
        float pos_x = 0.0f;
        float pos_y = 0.0f;
        float pos_z = 0.0f;
    };
    std::vector<CaptiveInfo> get_captive_data(
        const ChunkKey& key) const;

    // --- Diagnostics ---

    // Returns the total vegetation density across all tracked chunks.
    float total_vegetation() const;

    // Returns the total herbivore density across all tracked chunks.
    float total_herbivore() const;

    // Returns the total predator density across all tracked chunks.
    float total_predator() const;

    // --- Persistence ---

    // Sync the in-memory PopulationCell for a chunk to its ChunkData
    // so that it will be included in the next save.
    void sync_population_to_chunk(const ChunkKey& key);

    // Sync all tracked population cells to their ChunkData.
    void sync_all_populations_to_chunks();

    // Restore population cells from ChunkData that have persisted
    // ecosystem data. Called after world load.
    void restore_populations_from_chunks();

    // Sync captive creatures for a chunk to its ChunkData for save.
    void sync_captive_to_chunk(const ChunkKey& key);

    // Sync all captive creatures to their ChunkData.
    void sync_all_captive_to_chunks();

    // Restore captive creatures from ChunkData on load.
    void restore_captive_from_chunks();

    // --- GDScript biome override registration ---

    // Register a biome override from GDScript (via GDBiomeConfigRegistry).
    // Stored in a staging buffer; imported by initialize().
    // Returns false if staging is full (kMaxBiomeOverrides).
    static bool register_biome_override(
        const EcosystemParams::BiomeOverride& bo);

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

    // Select a random species ID for a given role in a given biome.
    // Falls back to the first registered species of that role if
    // the biome has no species list configured.
    uint16_t pick_species_for_biome(
        CreatureRole role, uint8_t biome_type) const;

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
    // Returns true if the creature actually moved (position changed).
    bool move_creature_toward_target(
        CreatureBlockEntityState& creature,
        float speed, float dt) const;

    // Kill a proxy creature: remove from registry, remove from
    // proxy group, add hunting pressure, emit events.
    void kill_proxy_creature(
        EntityId creature_id, const ChunkKey& chunk,
        CreatureRole role, uint16_t species_id, int64_t tick);

    // Find the chunk key that contains a given world position.
    ChunkKey chunk_key_for_position(
        const std::string& dimension,
        float world_x, float world_y, float world_z) const;

    // Infer biome type from chunk terrain material composition.
    // Scans surface-level cells and classifies by dominant material.
    uint8_t infer_biome_type(const ChunkKey& key) const;

    // Register default BiomeOverride entries with species lists
    // for each ecosystem_biome constant.
    void register_default_biome_overrides();

    // --- Captive creature private helpers ---

    // Check whether the world cell at (bx, by, bz) is solid (blocks
    // flood-fill / acts as a fence/wall). Returns true if solid.
    bool is_cell_solid(const std::string& dimension,
                       int32_t bx, int32_t by, int32_t bz) const;

    // Bounded flood-fill from (start_x, start_y, start_z) treating
    // solid cells as boundaries. Returns true if the region is fully
    // enclosed (flood-fill did not exceed the max bounding box or
    // cell count). On success, out_min/out_max are filled with the
    // interior bounding box.
    bool detect_enclosure(
        const std::string& dimension,
        int32_t start_x, int32_t start_y, int32_t start_z,
        int32_t& out_min_x, int32_t& out_min_y, int32_t& out_min_z,
        int32_t& out_max_x, int32_t& out_max_y, int32_t& out_max_z) const;

    // Capture a wild proxy creature into a pen. Removes it from the
    // wild proxy group, reduces wild density, creates a CaptiveCreature
    // in the chunk's captive list, assigns a runtime id, and emits
    // capture + captive_added events.
    // Returns the runtime id of the new captive, or 0 on failure.
    uint64_t capture_wild_creature(
        EntityId proxy_id, const ChunkKey& chunk,
        int32_t bounds_min_x, int32_t bounds_min_y, int32_t bounds_min_z,
        int32_t bounds_max_x, int32_t bounds_max_y, int32_t bounds_max_z,
        int64_t tick);

    // Spawn render nodes (emit captive_creature_added) for all captive
    // creatures in a chunk. Called when a chunk becomes active.
    void spawn_captive_for_chunk(const ChunkKey& chunk);

    // Despawn render nodes (emit captive_creature_removed) for all
    // captive creatures in a chunk. Called when a chunk goes to sleep.
    void despawn_captive_for_chunk(const ChunkKey& chunk);

    // Tick captive creatures in a chunk: taming, growth, gestation,
    // and wander AI. Emits move events when positions change.
    void tick_captive(const ChunkKey& chunk, int64_t tick, float delta);

    // Pick a wander target clamped to the captive's pen bounds.
    void pick_captive_wander_target(CaptiveCreature& cc, int64_t tick) const;

    // Move a captive creature toward its wander target, clamped to bounds.
    void move_captive(CaptiveCreature& cc, float speed, float dt) const;

    // Attempt to start breeding for a tamed adult captive creature.
    // Searches for a partner in the same chunk within
    // breed_partner_distance. If found, marks both as pregnant and
    // sets gestation/cooldown timers. Returns true if breeding started.
    bool try_start_breeding(
        const ChunkKey& chunk,
        std::vector<CaptiveCreature>& creatures,
        size_t feeder_index, int64_t tick);

    // Complete gestation: spawn a baby captive creature.
    void birth_baby(
        const ChunkKey& chunk,
        std::vector<CaptiveCreature>& creatures,
        size_t mother_index, int64_t tick);

    // Assign a fresh runtime id for a captive creature.
    uint64_t next_captive_runtime_id();

    // Ecosystem parameters (tunable at runtime).
    EcosystemParams params_;

    // Creature species definitions (data-driven).
    CreatureSpeciesRegistry species_registry_;

    // Per-chunk population data. Owned by this system (方案B).
    std::unordered_map<ChunkKey, PopulationCell> populations_;

    // Per-chunk proxy creature tracking. Only populated for active chunks.
    std::unordered_map<ChunkKey, ProxyGroup> active_proxies_;

    // Per-chunk captive creatures (persistent individuals detached from
    // the wild population). Populated for all chunks that have captive
    // data (active or sleeping); keyed by chunk.
    std::unordered_map<ChunkKey, std::vector<CaptiveCreature>> captive_;

    // Cached season state, updated each tick.
    Season current_season_ = Season::SPRING;

    // Cached day/night state, updated each tick.
    bool is_daytime_ = true;

    // Diffusion tracking.
    int64_t diffusion_count_ = 0;

    // Diffusion budget: tracks how many pairs processed in current pass.
    int diffusion_pairs_processed_ = 0;

    // Monotonic runtime id counter for captive creatures.
    // Starts high to avoid collisions with proxy EntityId values.
    uint64_t captive_runtime_id_counter_ = 0x100000000ULL;
};

} // namespace science_and_theology
