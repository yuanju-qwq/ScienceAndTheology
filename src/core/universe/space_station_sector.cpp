#include "space_station_sector.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 空间站注册
// ============================================================

bool SpaceStationSectorManager::register_station(
    SpaceStationDesc& desc,
    SectorManager& sector_manager,
    SparseChunkPolicy& sparse_policy) {

    if (desc.id.empty()) {
        return false;
    }

    // 如果 bounds 不包含中心点（未显式设置或默认构造），根据核心大小计算
    GlobalBlockPos center_block = to_block_pos(desc.center);
    if (!desc.bounds.contains(center_block)) {
        desc.bounds = compute_bounds(desc.center, desc.core_size_x,
                                      desc.core_size_y, desc.core_size_z);
    }

    // 注册 Sector
    SectorDesc sector;
    sector.id = desc.sector_id;
    sector.name = desc.name;
    sector.kind = SectorKind::SpaceStation;
    sector.bounds = desc.bounds;
    sector.allow_voxel_building = true;
    sector.allow_machines = true;
    sector.allow_power_network = true;
    sector.allow_logistics_network = true;
    sector.default_simulation = SimulationLevel::Passive;
    sector.legacy_storage_shard = desc.legacy_dimension_id;

    if (!sector_manager.register_sector_checked(sector)) {
        return false;
    }

    // 计算核心 chunk 并标记为有内容
    auto core_chunks = compute_core_chunks(desc);
    for (const auto& key : core_chunks) {
        sparse_policy.mark_chunk_has_content(key.sector, key.coord);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    stations_[desc.id] = desc;
    sector_to_station_[desc.sector_id] = desc.id;
    return true;
}

bool SpaceStationSectorManager::unregister_station(
    const std::string& station_id,
    SectorManager& sector_manager,
    SparseChunkPolicy& sparse_policy) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = stations_.find(station_id);
    if (it == stations_.end()) {
        return false;
    }

    SpaceStationDesc desc = it->second;

    // 取消核心 chunk 标记
    auto core_chunks = compute_core_chunks(desc);
    for (const auto& key : core_chunks) {
        sparse_policy.unmark_chunk_has_content(key.sector, key.coord);
    }

    // 注销 Sector
    sector_manager.unregister_sector(desc.sector_id);

    sector_to_station_.erase(desc.sector_id);
    stations_.erase(it);
    return true;
}

// ============================================================
// 查询
// ============================================================

const SpaceStationDesc* SpaceStationSectorManager::find_station(
    const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stations_.find(id);
    if (it == stations_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const SpaceStationDesc*> SpaceStationSectorManager::all_stations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const SpaceStationDesc*> result;
    result.reserve(stations_.size());
    for (const auto& [id, desc] : stations_) {
        result.push_back(&desc);
    }
    return result;
}

const SpaceStationDesc* SpaceStationSectorManager::find_station_by_sector(
    SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sector_to_station_.find(sector);
    if (it == sector_to_station_.end()) {
        return nullptr;
    }
    auto station_it = stations_.find(it->second);
    if (station_it == stations_.end()) {
        return nullptr;
    }
    return &station_it->second;
}

// ============================================================
// 核心区域
// ============================================================

std::vector<SectorChunkKey> SpaceStationSectorManager::compute_core_chunks(
    const SpaceStationDesc& desc) const {

    std::vector<SectorChunkKey> result;

    // 核心区域以空间站中心为基准
    GlobalBlockPos center_block = to_block_pos(desc.center);
    ChunkCoord center_chunk = block_pos_to_chunk_coord(center_block);

    // 核心区域大小（chunk 数）
    int sx = desc.core_size_x;
    int sy = desc.core_size_y;
    int sz = desc.core_size_z;

    // 以中心 chunk 为中心，扩展到核心大小
    int half_x = sx / 2;
    int half_y = sy / 2;
    int half_z = sz / 2;

    for (int dx = -half_x; dx < sx - half_x; ++dx) {
        for (int dy = -half_y; dy < sy - half_y; ++dy) {
            for (int dz = -half_z; dz < sz - half_z; ++dz) {
                ChunkCoord cc{center_chunk.cx + dx,
                              center_chunk.cy + dy,
                              center_chunk.cz + dz};
                result.push_back(SectorChunkKey{desc.sector_id, cc});
            }
        }
    }

    return result;
}

// ============================================================
// 工具
// ============================================================

void SpaceStationSectorManager::get_core_size(StationType type,
                                               int& size_x,
                                               int& size_y,
                                               int& size_z) {
    switch (type) {
        case StationType::Outpost:
            size_x = 1; size_y = 1; size_z = 1;
            break;
        case StationType::Habitat:
            size_x = 3; size_y = 1; size_z = 3;
            break;
        case StationType::Factory:
            size_x = 5; size_y = 1; size_z = 5;
            break;
        default:
            size_x = 1; size_y = 1; size_z = 1;
            break;
    }
}

AABB64 SpaceStationSectorManager::compute_bounds(const GlobalPos& center,
                                                  int core_size_x,
                                                  int core_size_y,
                                                  int core_size_z) {
    // bounds 以中心为基准，扩展到核心 chunk 范围
    // 每个 chunk 32 格，核心区域占 core_size 个 chunk
    int64_t half_x = (static_cast<int64_t>(core_size_x) * kUniverseChunkSize) / 2;
    int64_t half_y = (static_cast<int64_t>(core_size_y) * kUniverseChunkSize) / 2;
    int64_t half_z = (static_cast<int64_t>(core_size_z) * kUniverseChunkSize) / 2;

    GlobalBlockPos center_block = to_block_pos(center);

    // 额外留一些空间用于扩展
    int64_t margin = kUniverseChunkSize * 2;

    return AABB64{
        GlobalBlockPos{center_block.x - half_x - margin,
                       center_block.y - half_y - margin,
                       center_block.z - half_z - margin},
        GlobalBlockPos{center_block.x + half_x + margin,
                       center_block.y + half_y + margin,
                       center_block.z + half_z + margin}
    };
}

// ============================================================
// 管理
// ============================================================

void SpaceStationSectorManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    stations_.clear();
    sector_to_station_.clear();
}

} // namespace science_and_theology
