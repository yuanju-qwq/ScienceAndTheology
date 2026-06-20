#include "sector_observer_map.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 玩家管理
// ============================================================

void SectorObserverMap::register_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (players_.count(player_id) > 0) {
        return;
    }
    players_.emplace(player_id, PlayerEntry{});
}

void SectorObserverMap::unregister_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }

    SectorId old_sector = it->second.current_sector;

    // 从 Sector 反向索引中移除
    if (old_sector.is_valid()) {
        auto sit = sector_players_.find(old_sector);
        if (sit != sector_players_.end()) {
            auto& vec = sit->second;
            vec.erase(std::remove(vec.begin(), vec.end(), player_id), vec.end());
            if (vec.empty()) {
                sector_players_.erase(sit);
            }
        }
    }

    players_.erase(it);
}

bool SectorObserverMap::has_player(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return players_.count(player_id) > 0;
}

size_t SectorObserverMap::player_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return players_.size();
}

// ============================================================
// Sector 管理
// ============================================================

void SectorObserverMap::set_player_sector(uint64_t player_id, SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }

    SectorId old_sector = it->second.current_sector;

    // 如果 Sector 没变，不做处理
    if (old_sector == sector) {
        return;
    }

    // 从旧 Sector 反向索引中移除
    if (old_sector.is_valid()) {
        auto sit = sector_players_.find(old_sector);
        if (sit != sector_players_.end()) {
            auto& vec = sit->second;
            vec.erase(std::remove(vec.begin(), vec.end(), player_id), vec.end());
            if (vec.empty()) {
                sector_players_.erase(sit);
            }
        }
    }

    // 设置新 Sector
    it->second.current_sector = sector;
    // 切换 Sector 时清空旧 chunk 观察集
    it->second.observed_chunks.clear();

    // 添加到新 Sector 反向索引
    if (sector.is_valid()) {
        sector_players_[sector].push_back(player_id);
    }
}

SectorId SectorObserverMap::get_player_sector(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return SectorId{0};
    }
    return it->second.current_sector;
}

std::vector<uint64_t> SectorObserverMap::players_in_sector(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sector_players_.find(sector);
    if (it == sector_players_.end()) {
        return {};
    }
    return it->second;
}

size_t SectorObserverMap::player_count_in_sector(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sector_players_.find(sector);
    if (it == sector_players_.end()) {
        return 0;
    }
    return it->second.size();
}

// ============================================================
// 观察 chunk 管理
// ============================================================

void SectorObserverMap::set_observed_chunks(
    uint64_t player_id,
    const std::vector<SectorChunkKey>& chunks) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }

    SectorId current_sector = it->second.current_sector;
    it->second.observed_chunks.clear();

    // 只添加属于当前 Sector 的 chunk
    for (const auto& chunk : chunks) {
        if (chunk.sector == current_sector) {
            it->second.observed_chunks.insert(chunk);
        }
    }
}

void SectorObserverMap::add_observed_chunk(uint64_t player_id,
                                            const SectorChunkKey& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }

    // 只添加属于当前 Sector 的 chunk
    if (chunk.sector == it->second.current_sector) {
        it->second.observed_chunks.insert(chunk);
    }
}

void SectorObserverMap::remove_observed_chunk(uint64_t player_id,
                                               const SectorChunkKey& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }

    it->second.observed_chunks.erase(chunk);
}

std::vector<SectorChunkKey> SectorObserverMap::get_observed_chunks(
    uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return {};
    }

    return std::vector<SectorChunkKey>(it->second.observed_chunks.begin(),
                                        it->second.observed_chunks.end());
}

size_t SectorObserverMap::observed_chunk_count(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return 0;
    }
    return it->second.observed_chunks.size();
}

std::vector<uint64_t> SectorObserverMap::observers_of_chunk(
    const SectorChunkKey& chunk) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> result;
    for (const auto& [id, entry] : players_) {
        if (entry.observed_chunks.count(chunk) > 0) {
            result.push_back(id);
        }
    }
    return result;
}

bool SectorObserverMap::is_chunk_observed(const SectorChunkKey& chunk) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : players_) {
        if (entry.observed_chunks.count(chunk) > 0) {
            return true;
        }
    }
    return false;
}

// ============================================================
// 会合检测
// ============================================================

bool SectorObserverMap::are_in_same_sector(uint64_t player_a,
                                            uint64_t player_b) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto a_it = players_.find(player_a);
    auto b_it = players_.find(player_b);
    if (a_it == players_.end() || b_it == players_.end()) {
        return false;
    }

    return a_it->second.current_sector == b_it->second.current_sector &&
           a_it->second.current_sector.is_valid();
}

std::vector<uint64_t> SectorObserverMap::peers_in_same_sector(
    uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return {};
    }

    SectorId sector = it->second.current_sector;
    if (!sector.is_valid()) {
        return {};
    }

    std::vector<uint64_t> result;
    for (const auto& [id, entry] : players_) {
        if (id != player_id && entry.current_sector == sector) {
            result.push_back(id);
        }
    }
    return result;
}

// ============================================================
// 查询
// ============================================================

std::vector<uint64_t> SectorObserverMap::all_player_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> result;
    result.reserve(players_.size());
    for (const auto& [id, _] : players_) {
        result.push_back(id);
    }
    return result;
}

std::vector<SectorId> SectorObserverMap::occupied_sectors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SectorId> result;
    result.reserve(sector_players_.size());
    for (const auto& [sector, _] : sector_players_) {
        result.push_back(sector);
    }
    return result;
}

// ============================================================
// 管理
// ============================================================

void SectorObserverMap::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.clear();
    sector_players_.clear();
}

void SectorObserverMap::clear_sector(SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto sit = sector_players_.find(sector);
    if (sit == sector_players_.end()) {
        return;
    }

    // 清除该 Sector 内所有玩家的 Sector 归属和观察集
    for (uint64_t pid : sit->second) {
        auto pit = players_.find(pid);
        if (pit != players_.end()) {
            pit->second.current_sector = SectorId{0};
            pit->second.observed_chunks.clear();
        }
    }
    sector_players_.erase(sit);
}

} // namespace science_and_theology
