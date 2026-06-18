#include "creature_species.hpp"

namespace science_and_theology {

// --- CreatureSpeciesRegistry ---

bool CreatureSpeciesRegistry::register_species(
    const CreatureSpeciesDef& def) {
    if (def.species_id == 0) return false;

    if (species_by_id_.find(def.species_id) != species_by_id_.end()) {
        return false;
    }

    species_by_id_[def.species_id] = def;
    id_by_key_[def.species_key] = def.species_id;
    return true;
}

const CreatureSpeciesDef* CreatureSpeciesRegistry::get_species(
    uint16_t species_id) const {
    auto it = species_by_id_.find(species_id);
    return (it != species_by_id_.end()) ? &it->second : nullptr;
}

const CreatureSpeciesDef* CreatureSpeciesRegistry::get_species_by_key(
    const std::string& key) const {
    auto kit = id_by_key_.find(key);
    if (kit == id_by_key_.end()) return nullptr;
    return get_species(kit->second);
}

std::vector<uint16_t> CreatureSpeciesRegistry::all_species_ids() const {
    std::vector<uint16_t> ids;
    ids.reserve(species_by_id_.size());
    for (const auto& [id, _] : species_by_id_) {
        ids.push_back(id);
    }
    return ids;
}

void CreatureSpeciesRegistry::clear() {
    species_by_id_.clear();
    id_by_key_.clear();
}

// ============================================================
// Built-in V0.6 species definitions
// ============================================================
//
// Species are defined per the V0.6 design document (源律升华体系 §15.3).
// Each species has a role, model key, and drop table that links
// to the source-law sublimation path system.

void CreatureSpeciesRegistry::register_builtin_species() {
    using namespace creature_species;

    // --- Herbivores ---

    // Glow Deer (辉光鹿) — Radiance path material source.
    {
        CreatureSpeciesDef def;
        def.species_id = kGlowDeer;
        def.species_key = "glow_deer";
        def.display_name = "Glow Deer";
        def.role = CreatureRole::HERBIVORE;
        def.model_key = "glow_deer";
        def.move_speed = 0.12f;
        def.base_health = 0.8f;
        def.flee_detection_radius = 12.0f;
        def.wander_radius = 10.0f;
        def.model_scale = 1.2f;
        def.drops.push_back({"snt:glow_deer_antler", 0.7f, 1, 2});
        def.drops.push_back({"snt:purifying_pollen", 0.5f, 1, 1});
        register_species(def);
    }

    // Rock Lizard (岩蜥) — Sand Armor path material source.
    {
        CreatureSpeciesDef def;
        def.species_id = kRockLizard;
        def.species_key = "rock_lizard";
        def.display_name = "Rock Lizard";
        def.role = CreatureRole::HERBIVORE;
        def.model_key = "rock_lizard";
        def.move_speed = 0.08f;
        def.base_health = 1.0f;
        def.flee_detection_radius = 8.0f;
        def.wander_radius = 6.0f;
        def.model_scale = 0.7f;
        def.drops.push_back({"snt:rock_lizard_scale", 0.8f, 1, 3});
        def.drops.push_back({"snt:crystallized_bone_powder", 0.4f, 1, 1});
        register_species(def);
    }

    // --- Predators ---

    // Thunderbird (雷鸟) — Storm path material source.
    {
        CreatureSpeciesDef def;
        def.species_id = kThunderbird;
        def.species_key = "thunderbird";
        def.display_name = "Thunderbird";
        def.role = CreatureRole::PREDATOR;
        def.model_key = "thunderbird";
        def.move_speed = 0.18f;
        def.base_health = 0.9f;
        def.wander_radius = 14.0f;
        def.model_scale = 1.0f;
        def.drops.push_back({"snt:thunderbird_feather", 0.7f, 1, 2});
        def.drops.push_back({"snt:magnetic_crystal_shard", 0.3f, 1, 1});
        register_species(def);
    }

    // Sea Serpent (海蛇) — Tidal path material source.
    {
        CreatureSpeciesDef def;
        def.species_id = kSeaSerpent;
        def.species_key = "sea_serpent";
        def.display_name = "Sea Serpent";
        def.role = CreatureRole::PREDATOR;
        def.model_key = "sea_serpent";
        def.move_speed = 0.14f;
        def.base_health = 1.0f;
        def.wander_radius = 10.0f;
        def.model_scale = 1.3f;
        def.drops.push_back({"snt:sea_serpent_scale", 0.7f, 1, 2});
        def.drops.push_back({"snt:tidal_gland", 0.3f, 1, 1});
        register_species(def);
    }

    // Blaze Beast (炽核兽) — Furnace path material source.
    {
        CreatureSpeciesDef def;
        def.species_id = kBlazeBeast;
        def.species_key = "blaze_beast";
        def.display_name = "Blaze Beast";
        def.role = CreatureRole::PREDATOR;
        def.model_key = "blaze_beast";
        def.move_speed = 0.10f;
        def.base_health = 1.2f;
        def.wander_radius = 8.0f;
        def.model_scale = 1.4f;
        def.drops.push_back({"snt:blazing_core", 0.5f, 1, 1});
        def.drops.push_back({"snt:molten_blood_sample", 0.4f, 1, 1});
        register_species(def);
    }

    // --- Special / Boss-tier ---

    // Aether Wraith (以太幽影) — Ruin guardian.
    {
        CreatureSpeciesDef def;
        def.species_id = kAetherWraith;
        def.species_key = "aether_wraith";
        def.display_name = "Aether Wraith";
        def.role = CreatureRole::PREDATOR;
        def.model_key = "aether_wraith";
        def.move_speed = 0.15f;
        def.base_health = 1.5f;
        def.wander_radius = 12.0f;
        def.model_scale = 1.0f;
        def.drops.push_back({"snt:aether_fragment", 0.6f, 1, 2});
        def.drops.push_back({"snt:blueprint_shard", 0.2f, 1, 1});
        register_species(def);
    }

    // Aberrant Ascended (畸变升华者) — High-risk enemy.
    {
        CreatureSpeciesDef def;
        def.species_id = kAberrantAscended;
        def.species_key = "aberrant_ascended";
        def.display_name = "Aberrant Ascended";
        def.role = CreatureRole::PREDATOR;
        def.model_key = "aberrant_ascended";
        def.move_speed = 0.13f;
        def.base_health = 2.0f;
        def.wander_radius = 10.0f;
        def.model_scale = 1.2f;
        def.drops.push_back({"snt:aberrant_organ", 0.5f, 1, 2});
        def.drops.push_back({"snt:polluted_source_essence", 0.4f, 1, 1});
        register_species(def);
    }
}

} // namespace science_and_theology
