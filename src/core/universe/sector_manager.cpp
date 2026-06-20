#include "sector_manager.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// Sector 注册
// ============================================================

bool SectorManager::register_sector(const SectorDesc& desc) {
    if (!desc.is_valid()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (sectors_.count(desc.id) > 0) {
        return false;
    }
    SectorEntry entry;
    entry.desc = desc;
    entry.current_level = desc.default_simulation;
    sectors_.emplace(desc.id, std::move(entry));
    registration_order_.push_back(desc.id);
    return true;
}

bool SectorManager::register_sector_checked(
    const SectorDesc& desc,
    SectorRegistryDiagnostics::Overlap* out_overlap) {
    if (!desc.is_valid()) {
        return false;
    }

    // 先检查与现有可建造 Sector 的重叠（仅对可建造 Sector 检测）。
    if (desc.allow_voxel_building) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sectors_.count(desc.id) > 0) {
            return false;
        }
        for (const auto& [id, entry] : sectors_) {
            if (!entry.desc.allow_voxel_building) {
                continue;
            }
            if (entry.desc.bounds.intersects(desc.bounds)) {
                // 计算重叠 AABB
                AABB64 overlap;
                overlap.min.x = std::max(entry.desc.bounds.min.x, desc.bounds.min.x);
                overlap.min.y = std::max(entry.desc.bounds.min.y, desc.bounds.min.y);
                overlap.min.z = std::max(entry.desc.bounds.min.z, desc.bounds.min.z);
                overlap.max.x = std::min(entry.desc.bounds.max.x, desc.bounds.max.x);
                overlap.max.y = std::min(entry.desc.bounds.max.y, desc.bounds.max.y);
                overlap.max.z = std::min(entry.desc.bounds.max.z, desc.bounds.max.z);
                if (out_overlap) {
                    out_overlap->a = entry.desc.id;
                    out_overlap->b = desc.id;
                    out_overlap->overlap_bounds = overlap;
                }
                return false;
            }
        }
        // 校验通过，注册
        SectorEntry entry;
        entry.desc = desc;
        entry.current_level = desc.default_simulation;
        sectors_.emplace(desc.id, std::move(entry));
        registration_order_.push_back(desc.id);
        return true;
    }

    // 非可建造 Sector 直接注册（允许与其它 Sector 空间重叠）
    return register_sector(desc);
}

bool SectorManager::unregister_sector(SectorId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sectors_.find(id);
    if (it == sectors_.end()) {
        return false;
    }
    sectors_.erase(it);
    auto order_it = std::find(registration_order_.begin(),
                              registration_order_.end(), id);
    if (order_it != registration_order_.end()) {
        registration_order_.erase(order_it);
    }
    return true;
}

const SectorDesc* SectorManager::get_sector_desc(SectorId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sectors_.find(id);
    if (it == sectors_.end()) {
        return nullptr;
    }
    return &it->second.desc;
}

std::vector<const SectorDesc*> SectorManager::all_sector_descs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const SectorDesc*> result;
    result.reserve(sectors_.size());
    for (const auto& [id, entry] : sectors_) {
        result.push_back(&entry.desc);
    }
    return result;
}

// ============================================================
// 坐标查询
// ============================================================

SectorQueryResult SectorManager::find_sector(const GlobalBlockPos& pos) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 第一遍：优先返回可建造 Sector。
    for (SectorId id : registration_order_) {
        auto it = sectors_.find(id);
        if (it == sectors_.end()) continue;
        const SectorDesc& desc = it->second.desc;
        if (desc.allow_voxel_building && desc.bounds.contains(pos)) {
            return SectorQueryResult{desc.id, &desc};
        }
    }

    // 第二遍：返回任意包含该坐标的 Sector。
    for (SectorId id : registration_order_) {
        auto it = sectors_.find(id);
        if (it == sectors_.end()) continue;
        const SectorDesc& desc = it->second.desc;
        if (desc.bounds.contains(pos)) {
            return SectorQueryResult{desc.id, &desc};
        }
    }

    return SectorQueryResult{SectorId{0}, nullptr};
}

SectorQueryResult SectorManager::find_buildable_sector(const GlobalBlockPos& pos) const {
    SectorQueryResult result = find_sector(pos);
    if (result.is_buildable()) {
        return result;
    }
    return SectorQueryResult{SectorId{0}, nullptr};
}

std::optional<SectorChunkKey> SectorManager::make_chunk_key(const GlobalBlockPos& pos) const {
    SectorQueryResult q = find_sector(pos);
    if (!q.found()) {
        return std::nullopt;
    }
    SectorChunkKey key;
    key.sector = q.sector;
    key.coord = block_pos_to_chunk_coord(pos);
    return key;
}

SectorChunkKey SectorManager::make_chunk_key_for_sector(
    SectorId sector, const GlobalBlockPos& pos) const {
    SectorChunkKey key;
    key.sector = sector;
    key.coord = block_pos_to_chunk_coord(pos);
    return key;
}

// ============================================================
// 模拟等级
// ============================================================

void SectorManager::set_simulation_level(SectorId sector, SimulationLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sectors_.find(sector);
    if (it == sectors_.end()) {
        return;
    }
    it->second.current_level = level;
}

SimulationLevel SectorManager::get_simulation_level(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sectors_.find(sector);
    if (it == sectors_.end()) {
        return SimulationLevel::Unloaded;
    }
    return it->second.current_level;
}

std::vector<SectorId> SectorManager::active_sectors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SectorId> result;
    for (SectorId id : registration_order_) {
        auto it = sectors_.find(id);
        if (it == sectors_.end()) continue;
        if (it->second.current_level != SimulationLevel::Unloaded) {
            result.push_back(id);
        }
    }
    return result;
}

// ============================================================
// 诊断
// ============================================================

SectorRegistryDiagnostics SectorManager::compute_diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SectorRegistryDiagnostics diag;

    std::vector<const SectorDesc*> buildable;
    for (const auto& [id, entry] : sectors_) {
        diag.total_sectors++;
        if (entry.desc.allow_voxel_building) {
            diag.buildable_sectors++;
            buildable.push_back(&entry.desc);
        }
    }

    // 检测可建造 Sector 两两重叠。
    for (size_t i = 0; i < buildable.size(); ++i) {
        for (size_t j = i + 1; j < buildable.size(); ++j) {
            const SectorDesc& a = *buildable[i];
            const SectorDesc& b = *buildable[j];
            if (a.bounds.intersects(b.bounds)) {
                SectorRegistryDiagnostics::Overlap overlap;
                overlap.a = a.id;
                overlap.b = b.id;
                overlap.overlap_bounds.min.x = std::max(a.bounds.min.x, b.bounds.min.x);
                overlap.overlap_bounds.min.y = std::max(a.bounds.min.y, b.bounds.min.y);
                overlap.overlap_bounds.min.z = std::max(a.bounds.min.z, b.bounds.min.z);
                overlap.overlap_bounds.max.x = std::min(a.bounds.max.x, b.bounds.max.x);
                overlap.overlap_bounds.max.y = std::min(a.bounds.max.y, b.bounds.max.y);
                overlap.overlap_bounds.max.z = std::min(a.bounds.max.z, b.bounds.max.z);
                diag.buildable_overlaps.push_back(std::move(overlap));
            }
        }
    }

    return diag;
}

// ============================================================
// 管理
// ============================================================

void SectorManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    sectors_.clear();
    registration_order_.clear();
}

size_t SectorManager::sector_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sectors_.size();
}

} // namespace science_and_theology
