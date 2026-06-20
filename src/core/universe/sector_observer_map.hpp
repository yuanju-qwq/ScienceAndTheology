#pragma once

// ============================================================
// sector_observer_map.hpp — U7 Sector 感知观察者映射
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U7) 节。
//
// U7 验收条件：两个客户端分处不同 Sector 时互不接收无关真实 chunk；
// 会合后实体与方块状态收敛。
//
// SectorObserverMap 的职责：
//   1. 维护每个玩家当前所在的 Sector。
//   2. 维护每个玩家观察的 chunk 集合（按 Sector 分组）。
//   3. 确保跨 Sector 隔离：玩家只接收自己 Sector 的 chunk delta。
//   4. 会合收敛：当两个玩家进入同一 Sector 时，他们的观察集会重叠，
//      StateSyncServer 会为两人都生成该 Sector 的 delta。
//   5. 提供查询接口：哪些玩家在某个 Sector、某个 chunk 被哪些玩家观察。
//
// 模型：
//   - 每个玩家有一个 current_sector 和 observed_chunks（SectorChunkKey 集合）。
//   - observed_chunks 只包含当前 Sector 的 chunk（跨 Sector 时不保留旧 Sector 的 chunk）。
//   - 反向索引：Sector → 玩家列表，SectorChunkKey → 玩家列表。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// SectorObserverMap — Sector 感知观察者映射。
class SectorObserverMap {
public:
    SectorObserverMap() = default;
    ~SectorObserverMap() = default;

    SectorObserverMap(const SectorObserverMap&) = delete;
    SectorObserverMap& operator=(const SectorObserverMap&) = delete;

    // --- 玩家管理 ---

    // 注册玩家。
    // 初始 current_sector 可为 SectorId{0}（未进入任何 Sector）。
    void register_player(uint64_t player_id);

    // 注销玩家。
    void unregister_player(uint64_t player_id);

    // 查询玩家是否已注册。
    bool has_player(uint64_t player_id) const;

    // 玩家数量。
    size_t player_count() const;

    // --- Sector 管理 ---

    // 设置玩家当前 Sector。
    // 切换 Sector 时会清空旧的 observed_chunks。
    void set_player_sector(uint64_t player_id, SectorId sector);

    // 获取玩家当前 Sector。
    SectorId get_player_sector(uint64_t player_id) const;

    // 查询某 Sector 内的所有玩家。
    std::vector<uint64_t> players_in_sector(SectorId sector) const;

    // 查询某 Sector 内的玩家数量。
    size_t player_count_in_sector(SectorId sector) const;

    // --- 观察 chunk 管理 ---

    // 设置玩家观察的 chunk 集合。
    // 这些 chunk 必须属于玩家当前 Sector（不属于的会被忽略）。
    // 会替换之前的观察集。
    void set_observed_chunks(uint64_t player_id,
                              const std::vector<SectorChunkKey>& chunks);

    // 添加观察的单个 chunk。
    void add_observed_chunk(uint64_t player_id, const SectorChunkKey& chunk);

    // 移除观察的单个 chunk。
    void remove_observed_chunk(uint64_t player_id, const SectorChunkKey& chunk);

    // 获取玩家观察的所有 chunk。
    std::vector<SectorChunkKey> get_observed_chunks(uint64_t player_id) const;

    // 获取玩家观察的 chunk 数量。
    size_t observed_chunk_count(uint64_t player_id) const;

    // 查询哪些玩家在观察某个 chunk。
    std::vector<uint64_t> observers_of_chunk(const SectorChunkKey& chunk) const;

    // 查询某个 chunk 是否被任何玩家观察。
    bool is_chunk_observed(const SectorChunkKey& chunk) const;

    // --- 会合检测 ---

    // 检查两个玩家是否在同一 Sector。
    bool are_in_same_sector(uint64_t player_a, uint64_t player_b) const;

    // 获取与指定玩家在同一 Sector 的所有其他玩家。
    std::vector<uint64_t> peers_in_same_sector(uint64_t player_id) const;

    // --- 查询 ---

    // 返回所有已注册玩家 id。
    std::vector<uint64_t> all_player_ids() const;

    // 返回所有有玩家的 Sector。
    std::vector<SectorId> occupied_sectors() const;

    // --- 管理 ---

    void clear();

    // 清除指定 Sector 的所有玩家观察（用于 Sector 卸载）。
    void clear_sector(SectorId sector);

private:
    struct PlayerEntry {
        SectorId current_sector{0};
        std::unordered_set<SectorChunkKey> observed_chunks;
    };

    mutable std::mutex mutex_;

    // 玩家表。
    std::unordered_map<uint64_t, PlayerEntry> players_;

    // Sector → 玩家列表的反向索引。
    std::unordered_map<SectorId, std::vector<uint64_t>> sector_players_;
};

} // namespace science_and_theology
