#include "sector_transition_manager.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 玩家管理
// ============================================================

void SectorTransitionManager::register_player(uint64_t player_id,
                                               SectorId initial_sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayerState ps;
    ps.current_sector = initial_sector;
    players_[player_id] = std::move(ps);
}

void SectorTransitionManager::unregister_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.erase(player_id);
}

// ============================================================
// 转换检测
// ============================================================

std::optional<SectorTransitionEvent> SectorTransitionManager::update_player_position(
    uint64_t player_id,
    const GlobalPos& pos,
    const SectorManager& sector_manager) {

    // 查询玩家位置所属 Sector
    GlobalBlockPos block_pos = to_block_pos(pos);
    SectorQueryResult q = sector_manager.find_sector(block_pos);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return std::nullopt;
    }

    PlayerState& ps = it->second;

    if (!q.found()) {
        // 玩家不在任何 Sector 内（如深空未注册区域）
        // 不触发转换，保持当前 Sector
        return std::nullopt;
    }

    SectorId new_sector = q.sector;
    if (new_sector == ps.current_sector) {
        // 仍在同一 Sector 内
        return std::nullopt;
    }

    // 发生 Sector 转换
    SectorTransitionEvent event;
    event.player_id = player_id;
    event.from_sector = ps.current_sector;
    event.to_sector = new_sector;
    event.pos = pos;

    const SectorDesc* from_desc = sector_manager.get_sector_desc(ps.current_sector);
    const SectorDesc* to_desc = q.desc;
    event.from_kind = from_desc ? from_desc->kind : SectorKind::DeepSpace;
    event.to_kind = to_desc ? to_desc->kind : SectorKind::DeepSpace;
    event.reason = "player crossed sector boundary";

    // 更新玩家状态
    ps.current_sector = new_sector;

    // 记录历史
    ps.history.push_back(event);
    if (ps.history.size() > kMaxHistoryPerPlayer) {
        ps.history.erase(ps.history.begin());
    }

    return event;
}

std::optional<SectorTransitionEvent> SectorTransitionManager::set_player_sector(
    uint64_t player_id,
    SectorId new_sector,
    const SectorManager& sector_manager,
    const std::string& reason) {

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return std::nullopt;
    }

    PlayerState& ps = it->second;

    if (new_sector == ps.current_sector) {
        return std::nullopt;
    }

    SectorTransitionEvent event;
    event.player_id = player_id;
    event.from_sector = ps.current_sector;
    event.to_sector = new_sector;
    event.pos = GlobalPos{};

    const SectorDesc* from_desc = sector_manager.get_sector_desc(ps.current_sector);
    const SectorDesc* to_desc = sector_manager.get_sector_desc(new_sector);
    event.from_kind = from_desc ? from_desc->kind : SectorKind::DeepSpace;
    event.to_kind = to_desc ? to_desc->kind : SectorKind::DeepSpace;
    event.reason = reason.empty() ? "manual sector change" : reason;

    ps.current_sector = new_sector;

    ps.history.push_back(event);
    if (ps.history.size() > kMaxHistoryPerPlayer) {
        ps.history.erase(ps.history.begin());
    }

    return event;
}

// ============================================================
// 查询
// ============================================================

SectorId SectorTransitionManager::get_current_sector(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return SectorId{0};
    }
    return it->second.current_sector;
}

bool SectorTransitionManager::is_in_sector(uint64_t player_id, SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return false;
    }
    return it->second.current_sector == sector;
}

std::vector<SectorTransitionEvent> SectorTransitionManager::get_transition_history(
    uint64_t player_id, size_t max_count) const {

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return {};
    }

    const auto& history = it->second.history;
    size_t count = std::min(max_count, history.size());

    // 返回最近 count 条（倒序）
    std::vector<SectorTransitionEvent> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(history[history.size() - count + i]);
    }
    return result;
}

// ============================================================
// 管理
// ============================================================

void SectorTransitionManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.clear();
}

} // namespace science_and_theology
