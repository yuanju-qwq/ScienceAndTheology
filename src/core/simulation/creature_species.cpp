#include "creature_species.hpp"

namespace science_and_theology {

// --- CreatureSpeciesRegistry ---

bool CreatureSpeciesRegistry::register_species(
    CreatureSpeciesDef& def) {
    if (def.species_key.empty()) return false;
    // 幂等：若 species_key 已注册，返回 true 并通过引用参数
    // 将 def.species_id 更新为已有 ID。
    auto it = id_by_key_.find(def.species_key);
    if (it != id_by_key_.end()) {
        def.species_id = it->second;
        return true;
    }
    // 强制显式 ID：species_id == 0 则拒绝注册（不再支持自动分配）
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
    return ids;
}

void CreatureSpeciesRegistry::clear() {
    species_by_id_.clear();
    id_by_key_.clear();
}

void CreatureSpeciesRegistry::reset() {
    // 彻底复位：清空 staging 注册数据。
    // species_id 现由 GD 端显式分配，无内部 ID 计数器需要复位。
    staging().clear();
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
