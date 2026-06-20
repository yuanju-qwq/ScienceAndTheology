#include "universe_world_core.hpp"

namespace science_and_theology {

// ============================================================
// Universe 元信息
// ============================================================

void UniverseWorldCore::set_universe_id(UniverseId id) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    universe_id_ = id;
}

UniverseId UniverseWorldCore::universe_id() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return universe_id_;
}

void UniverseWorldCore::set_seed(int64_t seed) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    seed_ = seed;
}

int64_t UniverseWorldCore::seed() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return seed_;
}

void UniverseWorldCore::set_data_version(uint32_t version) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    data_version_ = version;
}

uint32_t UniverseWorldCore::data_version() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return data_version_;
}

bool UniverseWorldCore::is_legacy_save() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return data_version_ < kCurrentDataVersion;
}

// ============================================================
// Sector 管理
// ============================================================

SectorManager& UniverseWorldCore::sector_manager() {
    return sector_manager_;
}

const SectorManager& UniverseWorldCore::sector_manager() const {
    return sector_manager_;
}

bool UniverseWorldCore::register_sector(
    const SectorDesc& desc,
    SectorRegistryDiagnostics::Overlap* out_overlap) {
    if (!sector_manager_.register_sector_checked(desc, out_overlap)) {
        return false;
    }
    // 建立 legacy dimension_id 映射
    if (desc.legacy_storage_shard.is_valid()) {
        storage_shard_map_.register_shard(desc.legacy_storage_shard.value, desc.id);
    }
    return true;
}

// ============================================================
// 旧 dimension_id 适配
// ============================================================

std::optional<SectorId> UniverseWorldCore::find_sector_by_dimension(
    const std::string& dimension_id) const {
    return storage_shard_map_.find_sector(dimension_id);
}

std::string UniverseWorldCore::find_legacy_dimension_id(SectorId sector) const {
    return storage_shard_map_.find_legacy_dimension_id(sector);
}

StorageShardMap& UniverseWorldCore::storage_shard_map() {
    return storage_shard_map_;
}

const StorageShardMap& UniverseWorldCore::storage_shard_map() const {
    return storage_shard_map_;
}

// ============================================================
// 天体管理
// ============================================================

bool UniverseWorldCore::register_celestial_body(const CelestialBodyDesc& body) {
    if (!body.is_valid()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(core_mutex_);
    if (celestial_bodies_.count(body.id) > 0) {
        return false;
    }
    celestial_bodies_.emplace(body.id, body);
    celestial_order_.push_back(body.id);
    return true;
}

const CelestialBodyDesc* UniverseWorldCore::find_celestial_body(
    const std::string& id) const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    auto it = celestial_bodies_.find(id);
    if (it == celestial_bodies_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const CelestialBodyDesc*> UniverseWorldCore::all_celestial_bodies() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    std::vector<const CelestialBodyDesc*> result;
    result.reserve(celestial_order_.size());
    for (const auto& id : celestial_order_) {
        auto it = celestial_bodies_.find(id);
        if (it != celestial_bodies_.end()) {
            result.push_back(&it->second);
        }
    }
    return result;
}

// ============================================================
// 星球环境管理（U4）
// ============================================================

bool UniverseWorldCore::register_planet_environment(const PlanetEnvironment& env) {
    if (!env.is_valid()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(core_mutex_);
    auto [it, inserted] = planet_environments_.emplace(env.celestial_id, env);
    if (!inserted) {
        // 已存在则更新
        it->second = env;
    }
    return true;
}

const PlanetEnvironment* UniverseWorldCore::find_planet_environment(
    const std::string& celestial_id) const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    auto it = planet_environments_.find(celestial_id);
    if (it == planet_environments_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const PlanetEnvironment*> UniverseWorldCore::all_planet_environments() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    std::vector<const PlanetEnvironment*> result;
    result.reserve(planet_environments_.size());
    for (const auto& [id, env] : planet_environments_) {
        result.push_back(&env);
    }
    return result;
}

// ============================================================
// 坐标查询
// ============================================================

SectorQueryResult UniverseWorldCore::find_sector(const GlobalBlockPos& pos) const {
    return sector_manager_.find_sector(pos);
}

std::optional<SectorChunkKey> UniverseWorldCore::make_chunk_key(
    const GlobalBlockPos& pos) const {
    return sector_manager_.make_chunk_key(pos);
}

// ============================================================
// 诊断
// ============================================================

SectorRegistryDiagnostics UniverseWorldCore::compute_diagnostics() const {
    return sector_manager_.compute_diagnostics();
}

// ============================================================
// 管理
// ============================================================

void UniverseWorldCore::clear() {
    std::lock_guard<std::mutex> lock(core_mutex_);
    universe_id_ = UniverseId{1};
    seed_ = 0;
    data_version_ = kCurrentDataVersion;
    sector_manager_.clear();
    storage_shard_map_.clear();
    celestial_bodies_.clear();
    celestial_order_.clear();
    planet_environments_.clear();
}

UniverseWorldCore& global_universe_core() {
    static UniverseWorldCore instance;
    return instance;
}

} // namespace science_and_theology
