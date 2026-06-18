#pragma once

#include <cstdint>

namespace science_and_theology {

// ============================================================
// EcosystemParams — tunable parameters for the ecosystem model
// ============================================================
//
// Controls the Lotka-Volterra-style population dynamics in
// EcosystemSystem. Parameters can be overridden per biome type
// via biome_overrides[].
//
// All rate values are per-tick at 20 TPS. To convert from
// real-world intuition:
//   rate_per_tick = rate_per_second / 20
//
// Default values are tuned for a stable but dynamic ecosystem
// with visible seasonal fluctuations.

struct EcosystemParams {
    // --- Vegetation dynamics ---

    // Base vegetation growth rate per tick.
    // Multiplied by fertility * water * season_modifier.
    float veg_growth_rate = 0.0008f;

    // Vegetation decay rate per tick (natural die-off).
    // Multiplied by current vegetation_density.
    float veg_decay_rate = 0.0001f;

    // Herbivore grazing consumption rate per tick.
    // Multiplied by herbivore_density.
    float graze_rate = 0.0006f;

    // --- Herbivore dynamics ---

    // Herbivore reproduction rate per tick.
    // Multiplied by vegetation * herbivore * season_modifier.
    float herb_repro_rate = 0.0005f;

    // Herbivore natural death rate per tick (age, disease).
    float herb_natural_death = 0.0001f;

    // Predation rate: how much predator density reduces herbivore per tick.
    float predation_rate = 0.0004f;

    // --- Predator dynamics ---

    // Predator reproduction rate per tick.
    // Multiplied by herbivore * predator * season_modifier.
    float pred_repro_rate = 0.0003f;

    // Predator natural death rate per tick (starvation, age).
    float pred_natural_death = 0.0001f;

    // --- Nutrient cycling ---

    // Decomposition rate per tick.
    // Converts dead_biomass into soil_fertility.
    float decompose_rate = 0.0002f;

    // Fraction of animal death biomass that enters dead_biomass pool.
    float death_to_biomass_fraction = 0.8f;

    // Fraction of vegetation decay that enters dead_biomass pool.
    float veg_decay_to_biomass_fraction = 0.5f;

    // --- Seasonal modifiers (multipliers applied per season) ---

    // Vegetation growth multiplier per season: [Spring, Summer, Autumn, Winter].
    float season_veg_growth_mod[4] = { 1.2f, 1.0f, 0.6f, 0.1f };

    // Herbivore reproduction multiplier per season.
    float season_herb_repro_mod[4] = { 1.3f, 1.1f, 0.7f, 0.2f };

    // Predator reproduction multiplier per season.
    float season_pred_repro_mod[4] = { 1.1f, 1.0f, 0.9f, 0.5f };

    // Water availability modifier per season (added to biome base).
    float season_water_mod[4] = { 0.1f, -0.1f, -0.05f, 0.05f };

    // --- Day/night modifiers ---

    // Herbivore activity multiplier at night (applied to grazing + reproduction).
    // < 1.0 means herbivores are less active at night.
    float night_herb_activity_mod = 0.6f;

    // Predator activity multiplier at night (applied to predation + reproduction).
    // > 1.0 means predators are more active at night.
    float night_pred_activity_mod = 1.4f;

    // Vegetation growth multiplier at night.
    // Slightly reduced at night (no photosynthesis).
    float night_veg_growth_mod = 0.7f;

    // --- Diffusion ---

    // Population diffusion rate between adjacent chunks.
    // Higher = faster migration / spread.
    float diffusion_rate = 0.02f;

    // How often (in ticks) diffusion is computed.
    // 60 ticks = every 3 seconds at 20 TPS.
    int diffusion_interval_ticks = 60;

    // Maximum number of chunk pairs to process per diffusion pass.
    // Prevents frame spikes when many chunks are loaded.
    // 0 = unlimited (process all pairs).
    int max_diffusion_pairs_per_pass = 512;

    // --- Proxy creature parameters ---

    // Maximum number of proxy creatures per chunk per type.
    // Controls visual density of creatures in active chunks.
    int max_proxies_per_chunk = 8;

    // Minimum herbivore density required to spawn at least 1 proxy.
    // Below this threshold, no proxy herbivores are visible.
    float min_herb_density_for_proxy = 0.05f;

    // Minimum predator density required to spawn at least 1 proxy.
    float min_pred_density_for_proxy = 0.03f;

    // How often (in ticks) proxy counts are re-evaluated.
    // 100 ticks = every 5 seconds at 20 TPS.
    int proxy_rebalance_interval_ticks = 100;

    // --- Proxy creature AI parameters ---

    // How often (in ticks) a creature picks a new wander target.
    // 60 ticks = every 3 seconds at 20 TPS.
    int wander_interval_ticks = 60;

    // Movement speed in blocks per tick.
    // 0.1 = 2 blocks/second at 20 TPS.
    float creature_move_speed = 0.1f;

    // Distance (in blocks) at which a herbivore detects a predator
    // and begins fleeing.
    float flee_detection_radius = 10.0f;

    // Duration of flee behavior in ticks.
    // 40 ticks = 2 seconds at 20 TPS.
    int flee_duration_ticks = 40;

    // Speed multiplier when fleeing (applied to creature_move_speed).
    float flee_speed_multiplier = 2.0f;

    // Maximum wander distance from current position (in blocks).
    float wander_radius = 8.0f;

    // --- Per-biome overrides ---

    // Per-biome parameter overrides. If a biome has an entry,
    // its non-zero values override the global defaults.
    // Indexed by PopulationCell::biome_type.
    struct BiomeOverride {
        // Biome identifier (matches biome_type index).
        uint8_t biome_type = 0;

        // Base water availability for this biome [0, 1].
        float base_water = 0.5f;

        // Base soil fertility for this biome [0, 1].
        float base_fertility = 0.5f;

        // Vegetation growth rate multiplier for this biome.
        float veg_growth_multiplier = 1.0f;

        // Maximum vegetation density cap for this biome.
        float max_vegetation = 1.0f;

        // Maximum herbivore density cap for this biome.
        float max_herbivore = 1.0f;

        // Maximum predator density cap for this biome.
        float max_predator = 1.0f;
    };

    static constexpr int kMaxBiomeOverrides = 16;
    BiomeOverride biome_overrides[kMaxBiomeOverrides] = {};
    int biome_override_count = 0;

    // --- Accessors ---

    // Find the biome override for a given biome_type, or nullptr.
    const BiomeOverride* find_biome_override(uint8_t biome_type) const {
        for (int i = 0; i < biome_override_count; ++i) {
            if (biome_overrides[i].biome_type == biome_type) {
                return &biome_overrides[i];
            }
        }
        return nullptr;
    }
};

} // namespace science_and_theology
