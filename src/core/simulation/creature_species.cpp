#include "creature_species.hpp"

namespace science_and_theology {

namespace {
    uint16_t g_next_species_id = 1;
}

// --- CreatureSpeciesRegistry ---

bool CreatureSpeciesRegistry::register_species(
    CreatureSpeciesDef& def) {
    if (def.species_key.empty()) return false;
    if (id_by_key_.find(def.species_key) != id_by_key_.end()) {
        return false;
    }
    if (def.species_id == 0) {
        def.species_id = g_next_species_id++;
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
    return ids;
}

void CreatureSpeciesRegistry::clear() {
    species_by_id_.clear();
    id_by_key_.clear();
}

void CreatureSpeciesRegistry::import_from(
    const CreatureSpeciesRegistry& other) {
    for (const auto& [id, def] : other.species_by_id_) {
        if (species_by_id_.find(id) == species_by_id_.end()) {
            species_by_id_[id] = def;
            id_by_key_[def.species_key] = id;
        }
    }
}

CreatureSpeciesRegistry& CreatureSpeciesRegistry::staging() {
    static CreatureSpeciesRegistry g_staging;
    return g_staging;
}

} // namespace science_and_theology
