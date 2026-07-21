// Deterministic game-owned ecosystem simulation.
//
// Ownership: PopulationCell and CaptiveCreature values live in durable game
// chunk sidecars. This system advances only current-format typed values and
// never retains legacy WorldData, Godot signals, or global script staging.
//
// Thread affinity: all mutation methods run on the simulation main thread.
// Presentation, wild-proxy, and captive gameplay consumers attach through
// narrow value-only interfaces declared here before their implementations.

#pragma once

#include "core/expected.h"
#include "game/simulation/season_cycle.h"
#include "game/world/defs/creature_species.h"
#include "game/world/game_chunk.h"
#include "game/worldgen/world_gen_config.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace snt::game {

struct GameEcosystemBiomeRules {
    uint8_t biome_type = ecosystem_biome::kPlains;
    float base_water = 0.5f;
    float base_fertility = 0.5f;
    float vegetation_growth_multiplier = 1.0f;
    float max_vegetation = 1.0f;
    float max_herbivore = 1.0f;
    float max_predator = 1.0f;
};

// Current-format deterministic tuning. Population density remains the only
// durable wild state; proxy counts below only control the stable projection
// emitted to presentation/gameplay consumers.
struct GameEcosystemConfig {
    bool enabled = true;
    float vegetation_growth_rate = 0.0008f;
    float vegetation_decay_rate = 0.0001f;
    float grazing_rate = 0.0006f;
    float herbivore_reproduction_rate = 0.0005f;
    float herbivore_natural_death_rate = 0.0001f;
    float predation_rate = 0.0004f;
    float predator_reproduction_rate = 0.0003f;
    float predator_natural_death_rate = 0.0001f;
    float decomposition_rate = 0.0002f;
    float death_to_biomass_fraction = 0.8f;
    float vegetation_decay_to_biomass_fraction = 0.5f;
    std::array<float, 4> season_vegetation_growth = {1.2f, 1.0f, 0.6f, 0.1f};
    std::array<float, 4> season_herbivore_reproduction = {1.3f, 1.1f, 0.7f, 0.2f};
    std::array<float, 4> season_predator_reproduction = {1.1f, 1.0f, 0.9f, 0.5f};
    std::array<float, 4> season_water_adjustment = {0.1f, -0.1f, -0.05f, 0.05f};
    float night_vegetation_activity = 0.7f;
    float night_herbivore_activity = 0.6f;
    float night_predator_activity = 1.4f;
    float hunting_pressure_decay = 0.99f;
    float hunting_kill_contribution = 0.05f;
    // Capturing a wild creature detaches one representative from the
    // aggregate population immediately, unlike a kill which contributes a
    // decaying hunting-pressure signal.
    float wild_capture_density_contribution = 0.05f;
    // First activation requests a proxy rebalance immediately. Subsequent
    // requests use this cadence so presentation adapters never receive a
    // per-tick stream of unchanged aggregate population values.
    uint64_t wild_proxy_rebalance_interval_ticks = 100;
    // Per-role density-to-proxy mapping migrated from EcosystemParams. A
    // proxy is a transient projection, so these caps do not affect the
    // authoritative aggregate population equations or save data.
    float wild_proxy_herbivore_min_density = 0.15f;
    float wild_proxy_predator_min_density = 0.12f;
    uint32_t max_wild_herbivore_proxies_per_chunk = 2;
    uint32_t max_wild_predator_proxies_per_chunk = 2;
    // Macro cells inside this wider visual circle may emit render-only
    // representatives. They never become gameplay agents until the smaller
    // interactive circle promotes the same stable proxy id. Keeping their
    // count low makes far-LOD output cheap without retaining far-away AI.
    uint64_t far_visual_rebalance_interval_ticks = 300;
    float far_visual_herbivore_min_density = 0.25f;
    float far_visual_predator_min_density = 0.20f;
    uint32_t max_far_visual_herbivore_proxies_per_chunk = 1;
    uint32_t max_far_visual_predator_proxies_per_chunk = 1;
    float diffusion_rate = 0.02f;
    uint64_t diffusion_interval_ticks = 60;
    uint32_t max_diffusion_pairs_per_pass = 512;
    // Ecology is evaluated only around player-provided interest centers.
    // The macro circle advances aggregate population values; the smaller
    // interactive circle also projects temporary wild creatures. Radius zero
    // means the center chunk only. No interest provider means no active
    // ecology, which keeps unloaded/far-away terrain agent-free by default.
    uint32_t macro_horizontal_radius_chunks = 4;
    uint32_t macro_vertical_radius_chunks = 1;
    // The visual circle is clamped to the macro circle at runtime. A visual
    // representative is render-only and may therefore exist farther away
    // than an interactive wild creature without allocating behavior state.
    uint32_t visual_horizontal_radius_chunks = 4;
    uint32_t visual_vertical_radius_chunks = 1;
    uint32_t interactive_horizontal_radius_chunks = 1;
    uint32_t interactive_vertical_radius_chunks = 1;
    // A reactivated cell may have missed many fixed ticks. Integrate at most
    // this many source ticks in at most this many coarse steps; this gives a
    // deterministic macro catch-up without a player teleport causing an
    // unbounded fixed-tick replay.
    uint64_t max_macro_catchup_ticks = 86400;
    uint32_t max_macro_catchup_substeps = 256;
    std::array<GameEcosystemBiomeRules, ecosystem_biome::kCount> biome_rules = {{
        {ecosystem_biome::kPlains, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f},
        {ecosystem_biome::kDesert, 0.1f, 0.2f, 0.4f, 0.4f, 0.5f, 0.4f},
        {ecosystem_biome::kRocky, 0.3f, 0.3f, 0.5f, 0.5f, 0.4f, 0.5f},
        {ecosystem_biome::kOcean, 1.0f, 0.1f, 0.2f, 0.2f, 0.1f, 0.6f},
        {ecosystem_biome::kBarren, 0.05f, 0.05f, 0.1f, 0.1f, 0.0f, 0.0f},
    }};

    [[nodiscard]] const GameEcosystemBiomeRules& rules_for(uint8_t biome_type) const noexcept;
};

// A world/planet adapter may provide deterministic climate or a richer biome
// classification without giving the ecosystem system world ownership. Missing
// optional values deliberately keep terrain-derived/default behavior.
struct GameEcosystemEnvironmentSample {
    // Session policy may disable one dimension while other loaded dimensions
    // continue simulating. Disabled chunks neither create cells nor diffuse.
    bool enabled = true;
    // GameplayConfig supplies this per dimension. Zero freezes equations and
    // pressure decay without treating a valid configuration as an error.
    float rate_multiplier = 1.0f;
    std::optional<uint8_t> biome_type;
    std::optional<float> water_availability;
    std::optional<float> soil_fertility;
    bool is_daytime = true;
};

// One player-centered ecology circle in chunk space. The provider supplies
// only authoritative centers; the ecosystem owns radii so gameplay tuning is
// kept in GameEcosystemConfig and every consumer gets the same activation
// policy. Duplicate centers are harmless and are normalized by the system.
struct GameEcosystemInterestCenter {
    ChunkKey chunk;
};

class IGameEcosystemInterestProvider {
public:
    virtual ~IGameEcosystemInterestProvider() = default;

    // Called once per authoritative ecosystem tick. Implementations normally
    // expose currently connected player chunks, not visual-camera positions.
    virtual void collect_ecosystem_interest_centers(
        uint64_t current_tick,
        std::vector<GameEcosystemInterestCenter>& out_centers) const = 0;
};

class IGameEcosystemEnvironmentProvider {
public:
    virtual ~IGameEcosystemEnvironmentProvider() = default;

    [[nodiscard]] virtual bool sample_ecosystem_environment(
        const ChunkKey& chunk, GameEcosystemEnvironmentSample& out_sample) const = 0;
};

enum class GameEcosystemPopulationMutationKind : uint8_t {
    kInitialized,
    kHuntRecorded,
    kCaptureRecorded,
    kAdvanced,
    kMacroCatchUp,
    kDiffused,
};

struct GameEcosystemPopulationMutation {
    GameEcosystemPopulationMutationKind kind =
        GameEcosystemPopulationMutationKind::kInitialized;
    ChunkKey chunk;
    uint64_t source_tick = 0;
    PopulationCell previous;
    PopulationCell current;
};

class IGameEcosystemMutationSink {
public:
    virtual ~IGameEcosystemMutationSink() = default;
    virtual void on_ecosystem_population_mutated(
        const GameEcosystemPopulationMutation& mutation) = 0;
};

// One deterministic wild-proxy slot. stable_id is derived from the chunk,
// biome, role, and slot, never from the rebalance tick. It is a reconciliation
// key and spawn seed for consumers, not an entity ID or persisted state.
struct GameEcosystemWildProxyPlan {
    uint64_t stable_id = 0;
    uint16_t species_id = 0;
    CreatureRole role = CreatureRole::HERBIVORE;
    uint32_t slot = 0;
};

// Wild proxies remain a presentation/gameplay projection of aggregate
// density, rather than a second persistence model. The current core chooses
// catalog species deterministically so later entity/render consumers can
// reconcile this value plan without owning wild population state.
struct GameEcosystemWildProxyRebalanceRequest {
    ChunkKey chunk;
    uint64_t source_tick = 0;
    PopulationCell population;
    std::vector<GameEcosystemWildProxyPlan> proxies;
};

class IGameEcosystemWildProxySink {
public:
    virtual ~IGameEcosystemWildProxySink() = default;
    virtual void request_wild_proxy_rebalance(
        const GameEcosystemWildProxyRebalanceRequest& request) = 0;
};

// Render-only representatives use the same deterministic plan and stable id
// as an interactive wild representative. Consumers must not expose combat,
// capture, or taming against this interface; promotion into the interactive
// sink is the only transition that creates a gameplay entity.
using GameEcosystemFarVisualRebalanceRequest = GameEcosystemWildProxyRebalanceRequest;

class IGameEcosystemFarVisualSink {
public:
    virtual ~IGameEcosystemFarVisualSink() = default;
    virtual void request_far_visual_rebalance(
        const GameEcosystemFarVisualRebalanceRequest& request) = 0;
};

// Captive creatures are already durable sidecar values. This lifecycle seam
// reserves main-thread husbandry/proxy work without reintroducing the retired
// global CreatureSpeciesRegistry staging path into the population core.
struct GameEcosystemCaptiveTickRequest {
    ChunkKey chunk;
    uint64_t source_tick = 0;
    std::vector<CaptiveCreature> creatures;
};

class IGameEcosystemCaptiveLifecycleSink {
public:
    virtual ~IGameEcosystemCaptiveLifecycleSink() = default;
    virtual void tick_captive_creatures(const GameEcosystemCaptiveTickRequest& request) = 0;
};

class GameEcosystemSystem final {
public:
    GameEcosystemSystem(ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
                        const WorldGenConfigSnapshot& worldgen_config,
                        GameEcosystemConfig config = {}) noexcept;

    GameEcosystemSystem(const GameEcosystemSystem&) = delete;
    GameEcosystemSystem& operator=(const GameEcosystemSystem&) = delete;

    void set_environment_provider(const IGameEcosystemEnvironmentProvider* provider) noexcept {
        environment_provider_ = provider;
    }
    void set_interest_provider(const IGameEcosystemInterestProvider* provider) noexcept {
        interest_provider_ = provider;
        force_wild_proxy_rebalance_ = true;
        force_far_visual_rebalance_ = true;
    }
    void set_mutation_sink(IGameEcosystemMutationSink* sink) noexcept {
        mutation_sink_ = sink;
    }
    void set_wild_proxy_sink(IGameEcosystemWildProxySink* sink) noexcept {
        wild_proxy_sink_ = sink;
        force_wild_proxy_rebalance_ = true;
    }
    void set_far_visual_sink(IGameEcosystemFarVisualSink* sink) noexcept {
        far_visual_sink_ = sink;
        force_far_visual_rebalance_ = true;
    }
    void set_captive_lifecycle_sink(IGameEcosystemCaptiveLifecycleSink* sink) noexcept {
        captive_lifecycle_sink_ = sink;
    }
    // The caller retains the catalog for the lifetime of this system. Passing
    // nullptr restores the immutable built-in catalog.
    void set_species_catalog(const CreatureSpeciesRegistry* catalog) noexcept {
        species_catalog_ = catalog != nullptr ? catalog : &builtin_creature_species();
    }

    [[nodiscard]] const GameEcosystemConfig& config() const noexcept { return config_; }
    [[nodiscard]] GameEcosystemConfig& mutable_config() noexcept { return config_; }
    [[nodiscard]] const CreatureSpeciesRegistry& species_catalog() const noexcept {
        return *species_catalog_;
    }

    [[nodiscard]] snt::core::Expected<PopulationCell*> ensure_population_cell(
        const ChunkKey& chunk, uint64_t source_tick = 0);
    [[nodiscard]] PopulationCell* find_population_cell(const ChunkKey& chunk) noexcept;
    [[nodiscard]] const PopulationCell* find_population_cell(const ChunkKey& chunk) const noexcept;
    [[nodiscard]] size_t population_cell_count() const noexcept;

    // Player combat systems report aggregate pressure after a confirmed kill;
    // the next authoritative tick applies and decays it deterministically.
    [[nodiscard]] snt::core::Expected<void> record_hunt(
        const ChunkKey& chunk, CreatureRole role, float contribution = 0.0f,
        uint64_t source_tick = 0);
    // A confirmed capture removes one representative from the aggregate
    // wild population before its captive sidecar record is committed.
    [[nodiscard]] snt::core::Expected<void> record_wild_capture(
        const ChunkKey& chunk, CreatureRole role, float contribution = 0.0f,
        uint64_t source_tick = 0);

    // Advances active loaded chunks once at the shared authoritative tick.
    // Sidecar values are updated in place, so persistence observes the same
    // state without a separate legacy sync pass.
    void tick(uint64_t current_tick, Season current_season);

private:
    enum class ChunkActivity : uint8_t {
        kInactive,
        kMacro,
        kVisual,
        kInteractive,
    };

    [[nodiscard]] uint8_t infer_biome(const ChunkKey& chunk) const;
    [[nodiscard]] bool is_active_loaded_chunk(const ChunkKey& chunk) const;
    [[nodiscard]] ChunkActivity activity_for_chunk(
        const ChunkKey& chunk,
        const std::vector<GameEcosystemInterestCenter>& centers) const noexcept;
    void initialize_population(PopulationCell& cell, uint8_t biome_type,
                               uint64_t current_tick) const;
    void advance_population(PopulationCell& cell, Season season,
                            const GameEcosystemEnvironmentSample& environment,
                            uint64_t elapsed_ticks) const;
    void diffuse_populations(uint64_t current_tick,
                             const std::vector<ChunkKey>& active_chunks);
    [[nodiscard]] GameEcosystemWildProxyRebalanceRequest
    make_wild_proxy_rebalance_request(const ChunkKey& chunk, uint64_t current_tick,
                                      const PopulationCell& population) const;
    [[nodiscard]] GameEcosystemFarVisualRebalanceRequest
    make_far_visual_rebalance_request(const ChunkKey& chunk, uint64_t current_tick,
                                      const PopulationCell& population) const;
    void append_wild_proxy_plans(GameEcosystemWildProxyRebalanceRequest& request,
                                 CreatureRole role, float density,
                                 float minimum_density, uint32_t maximum) const;
    [[nodiscard]] uint32_t density_to_wild_proxy_count(
        float density, float minimum_density, uint32_t maximum) const noexcept;
    void emit_population_mutation(GameEcosystemPopulationMutationKind kind,
                                  const ChunkKey& chunk, uint64_t current_tick,
                                  const PopulationCell& previous,
                                  const PopulationCell& current) const;

    ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    const WorldGenConfigSnapshot* worldgen_config_ = nullptr;
    GameEcosystemConfig config_;
    const IGameEcosystemEnvironmentProvider* environment_provider_ = nullptr;
    IGameEcosystemMutationSink* mutation_sink_ = nullptr;
    const IGameEcosystemInterestProvider* interest_provider_ = nullptr;
    IGameEcosystemWildProxySink* wild_proxy_sink_ = nullptr;
    IGameEcosystemFarVisualSink* far_visual_sink_ = nullptr;
    IGameEcosystemCaptiveLifecycleSink* captive_lifecycle_sink_ = nullptr;
    const CreatureSpeciesRegistry* species_catalog_ = nullptr;
    std::unordered_set<ChunkKey> interactive_proxy_chunks_;
    std::unordered_set<ChunkKey> far_visual_proxy_chunks_;
    bool force_wild_proxy_rebalance_ = true;
    bool force_far_visual_rebalance_ = true;
};

}  // namespace snt::game
