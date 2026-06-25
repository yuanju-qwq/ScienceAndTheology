#pragma once

#include <cstdint>
#include <vector>

#include "creature_species.hpp"

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

    // --- Hunting pressure parameters ---

    // Amount added to hunting_pressure_herb/pred per player kill.
    // This translates to an extra death rate of:
    //   hunting_death = pressure * density
    // So a pressure of 0.05 with density 0.3 gives 0.015 extra death/tick.
    float hunting_kill_contribution = 0.05f;

    // Per-tick decay multiplier for hunting pressure.
    // 0.99 means pressure decays by 1% per tick.
    // At 20 TPS, half-life ≈ 69 ticks ≈ 3.5 seconds.
    float hunting_pressure_decay = 0.99f;

    // --- Captive / husbandry parameters ---

    // Taming progress gained per tick for a captive creature.
    // At 20 TPS, tame_duration_ticks ≈ 1 / tame_rate_per_tick.
    // 0.000083f ≈ 12000 ticks ≈ 1 game day (day_length_seconds=600 * 20).
    float tame_rate_per_tick = 0.000083f;

    // Taming progress boost per feeding (added to tame_progress).
    float feed_tame_boost = 0.05f;

    // Gestation duration in ticks (from breeding trigger to baby birth).
    // 6000 ticks ≈ 0.5 game day.
    int64_t gestation_ticks = 6000;

    // Baby growth duration in ticks (from birth to adult).
    // 12000 ticks ≈ 1 game day.
    int64_t baby_growth_ticks = 12000;

    // Breed cooldown in ticks after a successful breeding.
    // 24000 ticks ≈ 2 game days.
    int64_t breed_cooldown_ticks = 24000;

    // Maximum captive creatures allowed per chunk (pen capacity cap).
    int max_captive_per_chunk = 24;

    // Maximum interior cells scanned during enclosure flood-fill.
    // Prevents expensive searches on open terrain.
    int enclosure_max_cells = 4096;

    // Maximum half-extent of enclosure flood-fill bounding box.
    // The search box is [center - extent, center + extent] per axis.
    int enclosure_max_extent = 24;

    // Distance within which two captive creatures are considered
    // "same pen" partners for breeding.
    float breed_partner_distance = 24.0f;

    // Density reduction applied to the wild PopulationCell when a
    // creature is captured (it leaves the wild population).
    float capture_density_reduction = 0.05f;

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

        // Species that spawn as herbivores in this biome.
        // Empty = use default (first registered herbivore species).
        // Stored as species_key strings; resolved to IDs at runtime.
        std::vector<std::string> herb_species_keys;

        // Species that spawn as predators in this biome.
        std::vector<std::string> pred_species_keys;
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
