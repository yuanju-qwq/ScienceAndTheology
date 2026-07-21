// CreatureSpeciesRegistry implementation.
//
// Ported from src/core/simulation/creature_species.cpp.
// Namespace: science_and_theology -> snt::game.

#include "game/world/defs/creature_species.h"

#include <algorithm>
#include <stdexcept>

namespace snt::game {

// --- CreatureSpeciesRegistry ---

bool CreatureSpeciesRegistry::register_species(
    CreatureSpeciesDef& def) {
    if (def.species_key.empty()) return false;
    // Idempotent: if species_key is already registered, return true and
    // update def.species_id to the existing ID via the reference parameter.
    auto it = id_by_key_.find(def.species_key);
    if (it != id_by_key_.end()) {
        def.species_id = it->second;
        return true;
    }
    // Force explicit ID: species_id == 0 is rejected (auto-allocation not supported).
    if (def.species_id == 0) {
        return false;
    }
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
    std::sort(ids.begin(), ids.end());
    return ids;
}

void CreatureSpeciesRegistry::clear() {
    species_by_id_.clear();
    id_by_key_.clear();
}

const CreatureSpeciesRegistry& builtin_creature_species() {
    static const CreatureSpeciesRegistry catalog = [] {
        CreatureSpeciesRegistry result;
        const auto add = [&result](CreatureSpeciesDef definition) {
            if (!result.register_species(definition)) {
                throw std::logic_error("Invalid built-in creature species definition");
            }
        };

        add({
            .species_id = 1,
            .species_key = "glow_deer",
            .title_key = "creature.glow_deer",
            .role = CreatureRole::HERBIVORE,
            .model_key = "glow_deer",
            .move_speed = 0.12f,
            .base_health = 0.8f,
            .flee_detection_radius = 12.0f,
            .wander_radius = 10.0f,
            .model_scale = 1.2f,
            .biomes = {0},
            .drops = {
                {"snt:glow_deer_antler", 0.7f, 1, 2},
                {"snt:purifying_pollen", 0.5f, 1, 1},
                {"snt:raw_meat.glow_deer", 1.0f, 1, 2},
            },
        });
        add({
            .species_id = 2,
            .species_key = "rock_lizard",
            .title_key = "creature.rock_lizard",
            .role = CreatureRole::HERBIVORE,
            .model_key = "rock_lizard",
            .move_speed = 0.08f,
            .base_health = 1.0f,
            .flee_detection_radius = 8.0f,
            .wander_radius = 6.0f,
            .model_scale = 0.7f,
            .biomes = {0, 1, 2},
            .drops = {
                {"snt:rock_lizard_scale", 0.8f, 1, 3},
                {"snt:crystallized_bone_powder", 0.4f, 1, 1},
                {"snt:raw_meat.rock_lizard", 1.0f, 1, 2},
            },
        });
        add({
            .species_id = 129,
            .species_key = "thunderbird",
            .title_key = "creature.thunderbird",
            .role = CreatureRole::PREDATOR,
            .model_key = "thunderbird",
            .move_speed = 0.18f,
            .base_health = 0.9f,
            .wander_radius = 14.0f,
            .model_scale = 1.0f,
            .biomes = {0, 1, 2},
            .drops = {
                {"snt:thunderbird_feather", 0.7f, 1, 2},
                {"snt:magnetic_crystal_shard", 0.3f, 1, 1},
                {"snt:raw_meat.thunderbird", 1.0f, 1, 1},
            },
        });
        add({
            .species_id = 130,
            .species_key = "sea_serpent",
            .title_key = "creature.sea_serpent",
            .role = CreatureRole::PREDATOR,
            .model_key = "sea_serpent",
            .move_speed = 0.14f,
            .base_health = 1.0f,
            .wander_radius = 10.0f,
            .model_scale = 1.3f,
            .biomes = {3},
            .drops = {
                {"snt:sea_serpent_scale", 0.7f, 1, 2},
                {"snt:tidal_gland", 0.3f, 1, 1},
                {"snt:raw_meat.sea_serpent", 1.0f, 1, 3},
            },
        });
        add({
            .species_id = 131,
            .species_key = "blaze_beast",
            .title_key = "creature.blaze_beast",
            .role = CreatureRole::PREDATOR,
            .model_key = "blaze_beast",
            .move_speed = 0.10f,
            .base_health = 1.2f,
            .wander_radius = 8.0f,
            .model_scale = 1.4f,
            .biomes = {0, 2},
            .drops = {
                {"snt:blazing_core", 0.5f, 1, 1},
                {"snt:molten_blood_sample", 0.4f, 1, 1},
                {"snt:raw_meat.blaze_beast", 1.0f, 1, 2},
            },
        });
        add({
            .species_id = 132,
            .species_key = "aether_wraith",
            .title_key = "creature.aether_wraith",
            .role = CreatureRole::PREDATOR,
            .model_key = "aether_wraith",
            .move_speed = 0.15f,
            .base_health = 1.5f,
            .wander_radius = 12.0f,
            .model_scale = 1.0f,
            .drops = {
                {"snt:aether_fragment", 0.6f, 1, 2},
                {"snt:blueprint_shard", 0.2f, 1, 1},
            },
        });
        add({
            .species_id = 133,
            .species_key = "aberrant_ascended",
            .title_key = "creature.aberrant_ascended",
            .role = CreatureRole::PREDATOR,
            .model_key = "aberrant_ascended",
            .move_speed = 0.13f,
            .base_health = 2.0f,
            .wander_radius = 10.0f,
            .model_scale = 1.2f,
            .drops = {
                {"snt:aberrant_organ", 0.5f, 1, 2},
                {"snt:polluted_source_essence", 0.4f, 1, 1},
            },
        });
        return result;
    }();
    return catalog;
}

} // namespace snt::game
