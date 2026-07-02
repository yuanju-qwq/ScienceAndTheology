#pragma once

// ============================================================
// sector_transition_manager.hpp — Sector 转换管理（U4）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 4.5、8.2、21.2 (U4) 节。
//
// U4 工作项：到达新星球时只更新玩家的 Sector 与 interest，不切换全局
// active_dimension。
//
// SectorTransitionManager 跟踪玩家当前所在 Sector，检测跨 Sector 边界
// 的转换事件，并通知 InterestManager / ChunkStreamingSystem 更新状态。
//
// 关键规则（设计文档 4.5、8.2）：
//   - 玩家跨 Sector 边界时，只更新玩家的 current_sector 和 interest，
//     不切换全局 active_dimension。
//   - 旧 Sector 的 chunk 按卸载策略逐步卸载。
//   - 新 Sector 的 chunk 按加载策略逐步加载。
//   - 转换事件记录用于诊断和上层同步。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

#include "universe_types.hpp"
#include "sector_manager.hpp"

namespace science_and_theology {

// Sector 转换事件。
struct SectorTransitionEvent {
    uint64_t player_handle;
    SectorId from_sector;
    SectorId to_sector;
    GlobalPos pos;              // 转换时的玩家位置
    SectorKind from_kind;       // 旧 Sector 类型
    SectorKind to_kind;         // 新 Sector 类型
    std::string reason;         // 转换原因
};

// SectorTransitionManager — Sector 转换管理器。
//
// 跟踪玩家当前 Sector，检测跨 Sector 转换。
// 不切换全局 active_dimension，只更新玩家状态。
class SectorTransitionManager {
public:
    SectorTransitionManager() = default;
    ~SectorTransitionManager() = default;

    SectorTransitionManager(const SectorTransitionManager&) = delete;
    SectorTransitionManager& operator=(const SectorTransitionManager&) = delete;

    // --- 玩家管理 ---

    // 注册玩家，指定初始 Sector。
    void register_player(uint64_t player_handle, SectorId initial_sector);

    // 注销玩家。
    void unregister_player(uint64_t player_handle);

    // --- 转换检测 ---

    // 更新玩家位置，检测 Sector 转换。
    // 若玩家跨入新 Sector，返回转换事件。
    // 不切换全局 active_dimension，只更新玩家 current_sector。
    std::optional<SectorTransitionEvent> update_player_position(
        uint64_t player_handle,
        const GlobalPos& pos,
        const SectorManager& sector_manager);

    // 手动设置玩家当前 Sector（用于上层强制切换或初始化）。
    std::optional<SectorTransitionEvent> set_player_sector(
        uint64_t player_handle,
        SectorId new_sector,
        const SectorManager& sector_manager,
        const std::string& reason = "");

    // --- 查询 ---

    // 返回玩家当前 Sector。
    SectorId get_current_sector(uint64_t player_handle) const;

    // 返回玩家是否在指定 Sector 内。
    bool is_in_sector(uint64_t player_handle, SectorId sector) const;

    // 返回玩家历史转换记录（诊断用，最多保留最近 N 条）。
    std::vector<SectorTransitionEvent> get_transition_history(
        uint64_t player_handle, size_t max_count = 10) const;

    // --- 管理 ---

    void clear();

private:
    struct PlayerState {
        SectorId current_sector;
        std::vector<SectorTransitionEvent> history;
    };

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, PlayerState> players_;

    // 历史记录上限。
    static constexpr size_t kMaxHistoryPerPlayer = 32;
};

} // namespace science_and_theology
