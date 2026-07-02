#pragma once

// ============================================================
// multi_sector_sync_coordinator.hpp — U7 多 Sector 并发 delta 生产协调器
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U7) 节。
//
// U7 工作项：支持玩家分别位于不同星球/深空/空间站，服务端同时维护
// 多个活跃 Sector；多 Sector 并发 delta 生产。
//
// 验收条件：
//   1. 两个客户端分处不同 Sector 时互不接收无关真实 chunk。
//   2. 会合后实体与方块状态收敛。
//   3. 任一客户端高速移动都不会挤占另一客户端的同步预算。
//
// MultiSectorSyncCoordinator 的职责：
//   1. 协调 InterestManager（计算每个玩家的兴趣集合）。
//   2. 协调 SectorObserverMap（维护玩家→Sector→观察 chunk 映射）。
//   3. 协调 StateSyncServer（批量计算多观察者 delta）。
//   4. 协调 AoiBudgetManager（per-observer 分通道预算限制）。
//   5. 每 tick 产出 per-observer、per-channel 的发送批次（已应用预算）。
//
// 数据流（每 tick）：
//   1. 对每个注册玩家：
//      a. InterestManager.compute_interest → InterestSet
//      b. SectorObserverMap.set_player_sector / set_observed_chunks
//   2. 构建 observer_views（SectorChunkKey → ChunkKey 转换）
//   3. StateSyncServer.compute_deltas_batch(observer_views)
//   4. 对每个 (observer, delta) 应用 AoiBudgetManager.try_send：
//      - Chunk 通道：限制初始 chunk 加载量
//      - Entity 通道：限制实体创建/销毁同步量
//      - BlockDelta 通道：限制方块增量同步量
//      - MachineDetail 通道：限制机器详情同步量
//      - CelestialLod 通道：限制天体 LOD 同步量
//   5. AoiBudgetManager.tick()（重置 per-tick 预算）
//
// 跨 Sector 隔离：
//   - SectorObserverMap 保证玩家只观察当前 Sector 的 chunk。
//   - coordinator 在构建 observer_views 时只包含玩家当前 Sector 的 chunk。
//   - 不同 Sector 的玩家不会出现在同一 observer_views 中。
//
// 会合收敛：
//   - 当两个玩家进入同一 Sector 时，SectorObserverMap.are_in_same_sector 返回 true。
//   - 两人观察的 chunk 集合会重叠，StateSyncServer.compute_deltas_batch 会为
//     两人都生成该 Sector 的 delta（脏标志在所有观察者处理完后才清除）。
//   - 两人都会收到同一 chunk 的 delta，状态收敛。
//
// 预算不挤占：
//   - AoiBudgetManager 为每个观察者独立分配 per-channel 预算。
//   - 一个观察者的高速移动消耗自己的 Chunk 预算，不影响另一个观察者。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

#include "universe_types.hpp"
#include "aoi_budget_manager.hpp"
#include "sector_observer_map.hpp"
#include "flight_state_tracker.hpp"
#include "celestial_lod_system.hpp"
#include "../player/player_handle.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

// 前向声明（避免引入完整头文件依赖）
class InterestManager;
class StateSyncServer;
class SectorManager;
class UniverseWorldCore;
struct InterestSet;
struct StateDelta;

// per-observer、per-channel 的发送批次（已应用预算）。
struct ObserverSendBatch {
    PlayerHandle observer = 0;
    SectorId sector;

    // 各通道实际发送的项（已应用预算限制）
    std::vector<SectorChunkKey> chunks;          // Chunk 通道：初始 chunk 加载
    std::vector<EntityId> entities_created;      // Entity 通道：实体创建
    std::vector<EntityId> entities_destroyed;    // Entity 通道：实体销毁
    std::vector<SectorChunkKey> block_deltas;    // BlockDelta 通道：方块增量
    std::vector<std::pair<EntityId, uint8_t>> machine_details;  // MachineDetail 通道
    std::vector<CelestialLodResult> celestial_lods;  // CelestialLod 通道

    // 各通道本 tick 被推迟/丢弃的量（用于诊断）
    int chunks_deferred = 0;
    int entities_deferred = 0;
    int block_deltas_deferred = 0;
    int machine_details_dropped = 0;
    int celestial_lods_dropped = 0;

    // 本批次是否为空
    bool empty() const {
        return chunks.empty()
            && entities_created.empty()
            && entities_destroyed.empty()
            && block_deltas.empty()
            && machine_details.empty()
            && celestial_lods.empty();
    }

    // 本批次总发送量
    int total_sent() const {
        return static_cast<int>(chunks.size())
             + static_cast<int>(entities_created.size())
             + static_cast<int>(entities_destroyed.size())
             + static_cast<int>(block_deltas.size())
             + static_cast<int>(machine_details.size())
             + static_cast<int>(celestial_lods.size());
    }
};

// MultiSectorSyncCoordinator — 多 Sector 并发 delta 生产协调器。
//
// 不拥有子系统，持有非所有权指针。调用方需保证子系统生命周期。
class MultiSectorSyncCoordinator {
public:
    MultiSectorSyncCoordinator() = default;
    ~MultiSectorSyncCoordinator() = default;

    MultiSectorSyncCoordinator(const MultiSectorSyncCoordinator&) = delete;
    MultiSectorSyncCoordinator& operator=(const MultiSectorSyncCoordinator&) = delete;

    // --- 子系统绑定 ---

    void set_interest_manager(InterestManager* im) { interest_manager_ = im; }
    void set_state_sync_server(StateSyncServer* sss) { state_sync_server_ = sss; }
    void set_budget_manager(AoiBudgetManager* bm) { budget_manager_ = bm; }
    void set_observer_map(SectorObserverMap* om) { observer_map_ = om; }
    void set_sector_manager(const SectorManager* sm) { sector_manager_ = sm; }
    void set_universe_core(const UniverseWorldCore* uc) { universe_core_ = uc; }

    // --- 玩家生命周期 ---

    // 注册玩家（同时注册到 InterestManager、SectorObserverMap、AoiBudgetManager、StateSyncServer）。
    void register_player(PlayerHandle id,
                         const GlobalPos& pos,
                         const GlobalPos& velocity,
                         SectorId current_sector);

    // 注销玩家。
    void unregister_player(PlayerHandle id);

    // 更新玩家位置和速度（同时更新 InterestManager）。
    // 返回可选的飞行模式切换事件。
    std::optional<FlightModeChangeEvent> update_player(PlayerHandle id,
                                                        const GlobalPos& pos,
                                                        const GlobalPos& velocity);

    // 设置玩家着陆目标（委托给 InterestManager）。
    void set_landing_target(PlayerHandle id,
                            const std::string& celestial_id,
                            double distance_to_surface);

    // 清除着陆目标。
    void clear_landing_target(PlayerHandle id);

    // --- 主 tick：计算并收集所有 delta，应用预算 ---

    // 执行一个同步 tick：
    //   1. 对每个玩家计算兴趣集合
    //   2. 更新 SectorObserverMap
    //   3. 批量计算 delta
    //   4. 应用 per-observer、per-channel 预算
    //   5. 推进 AoiBudgetManager 的 tick
    // 返回每个观察者的发送批次。
    std::vector<ObserverSendBatch> tick();

    // --- 查询 ---

    // 获取上一次 tick 中某观察者的发送批次。
    // 若该观察者未参与上次 tick，返回空批次。
    ObserverSendBatch get_last_batch(PlayerHandle id) const;

    // 跨 Sector 隔离检查：chunk 是否对观察者可见。
    // 仅当 chunk.sector == 观察者当前 Sector 时可见。
    bool is_chunk_visible_to(PlayerHandle observer, const SectorChunkKey& chunk) const;

    // 会合检测：两个玩家是否在同一 Sector 且观察集有重叠。
    bool are_converged(PlayerHandle a, PlayerHandle b) const;

    // 返回所有注册玩家 id。
    std::vector<PlayerHandle> all_player_handles() const;

    // 注册玩家数量。
    size_t player_count() const;

    // --- 管理 ---

    void clear();

private:
    // 将 SectorChunkKey 转换为 StateSyncServer 使用的 ChunkKey。
    // dimension_id 使用 "sector_<id>" 格式。
    static std::string sector_to_dimension_id(SectorId sector);
    static std::string sector_to_dimension_id(uint64_t sector_value);

    // 构建单个玩家的发送批次（应用预算）。
    ObserverSendBatch build_batch_for(
        PlayerHandle observer,
        SectorId sector,
        const InterestSet& interest,
        const StateDelta& delta);

    mutable std::mutex mutex_;

    InterestManager* interest_manager_ = nullptr;
    StateSyncServer* state_sync_server_ = nullptr;
    AoiBudgetManager* budget_manager_ = nullptr;
    SectorObserverMap* observer_map_ = nullptr;
    const SectorManager* sector_manager_ = nullptr;
    const UniverseWorldCore* universe_core_ = nullptr;

    // 上一次 tick 的发送批次（per-observer）。
    std::unordered_map<PlayerHandle, ObserverSendBatch> last_batches_;

    // 注册的玩家 id 列表（按注册顺序）。
    std::vector<PlayerHandle> registered_players_;
};

} // namespace science_and_theology
