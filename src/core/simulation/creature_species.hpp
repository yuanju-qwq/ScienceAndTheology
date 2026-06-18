#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace science_and_theology {

// ============================================================
// CreatureRole — behavioral role classification for creatures
// ============================================================
//
// Determines AI behavior pattern:
//   HERBIVORE — wanders, flees from predators
//   PREDATOR  — wanders, does not flee

enum class CreatureRole : uint8_t {
    HERBIVORE = 0,
    PREDATOR  = 1,
    COUNT     = 2,
};

constexpr const char* kCreatureRoleNames[] = {
    "Herbivore", "Predator",
};

// ============================================================
// CreatureDropDef — item dropped when a creature is killed
// ============================================================

struct CreatureDropDef {
    // Item key string (e.g. "snt:rock_lizard_scale").
    std::string item_key;

    // Drop chance [0, 1]. 1.0 = always drops.
    float chance = 1.0f;

    // Minimum count per drop.
    int min_count = 1;

    // Maximum count per drop.
    int max_count = 1;
};

// ============================================================
// CreatureSpeciesDef — data-driven species definition
// ============================================================
//
// Defines a single creature species with its appearance, behavior
// parameters, and loot table. Species are registered in
// CreatureSpeciesRegistry and referenced by species_id.
//
// Design:
//   - CreatureRole (HERBIVORE/PREDATOR) determines AI behavior.
//   - Species-specific parameters (speed, health, flee radius)
//     override the global EcosystemParams defaults.
//   - Drop table links to the source-law system (V0.6).

struct CreatureSpeciesDef {
    // Unique species identifier. 0 = invalid/none.
    uint16_t species_id = 0;

    // Human-readable species key (e.g. "rock_lizard").
    std::string species_key;

    // Translation key (e.g. "creature.rock_lizard").
    std::string title_key;

    // Behavioral role: determines AI state machine.
    CreatureRole role = CreatureRole::HERBIVORE;

    // 3D model resource key for rendering (e.g. "rock_lizard").
    // Godot side resolves this to a scene/resource path.
    std::string model_key;

    // Movement speed in blocks per tick (overrides EcosystemParams).
    // 0.0 = use global default from EcosystemParams.
    float move_speed = 0.0f;

    // Maximum health [0, 1]. 1.0 = full health.
    float base_health = 1.0f;

    // Flee detection radius in blocks (herbivores only).
    // 0.0 = use global default from EcosystemParams.
    float flee_detection_radius = 0.0f;

    // Wander radius in blocks.
    // 0.0 = use global default from EcosystemParams.
    float wander_radius = 0.0f;

    // Scale multiplier for 3D model rendering.
    float model_scale = 1.0f;

    // Items dropped when this creature is killed.
    std::vector<CreatureDropDef> drops;
};

// ============================================================
// CreatureSpeciesRegistry — global registry of species definitions
// ============================================================
//
// Provides O(1) lookup by species_id and by species_key.
// Built-in species are registered in register_builtin_species().
// Additional species can be loaded from JSON (future).

class CreatureSpeciesRegistry {
public:
    CreatureSpeciesRegistry() = default;

    // Register a species definition. Returns false if species_id
    // is already registered.
    bool register_species(const CreatureSpeciesDef& def);

    // Get species definition by ID, or nullptr if not found.
    const CreatureSpeciesDef* get_species(uint16_t species_id) const;

    // Get species definition by key string, or nullptr if not found.
    const CreatureSpeciesDef* get_species_by_key(
        const std::string& key) const;

    // Returns the number of registered species.
    size_t species_count() const { return species_by_id_.size(); }

    // Returns all registered species IDs.
    std::vector<uint16_t> all_species_ids() const;

    // Register built-in V0.6 species definitions.
    // Called once during initialization.
    void register_builtin_species();

    // Clear all registered species.
    void clear();

private:
    std::unordered_map<uint16_t, CreatureSpeciesDef> species_by_id_;
    std::unordered_map<std::string, uint16_t> id_by_key_;
};

// ============================================================
// Built-in species ID constants
// ============================================================
//
// These IDs are stable and must not change between versions.
// New species should be appended with incrementing IDs.

namespace creature_species {
    constexpr uint16_t kNone        = 0;

    // Herbivores
    constexpr uint16_t kGlowDeer    = 1;   // 辉光鹿
    constexpr uint16_t kRockLizard  = 2;   // 岩蜥

    // Predators
    constexpr uint16_t kThunderbird = 3;   // 雷鸟
    constexpr uint16_t kSeaSerpent  = 4;   // 海蛇
    constexpr uint16_t kBlazeBeast  = 5;   // 炽核兽

    // Special / Boss-tier
    constexpr uint16_t kAetherWraith   = 6;   // 以太幽影
    constexpr uint16_t kAberrantAscended = 7; // 畸变升华者

    constexpr uint16_t kMaxBuiltinId = 7;
} // namespace creature_species

} // namespace science_and_theology
