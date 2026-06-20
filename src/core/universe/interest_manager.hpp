#pragma once

// ============================================================
// interest_manager.hpp — 统一兴趣区域管理
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 12 节。
//
// 方案一的网络性能依赖 AOI。玩家不应该收到整个 Universe 的数据。
// 只应该收到：
//   - 附近 chunk。
//   - 附近实体。
//   - 附近机器简略状态。
//   - 打开 GUI 的机器详细状态。
//   - 远处星球 LOD。
//   - 远处大型结构 LOD。
//
// InterestManager 统一计算玩家的兴趣集合，整合：
//   1. ChunkStreamingSystem：附近 chunk（根据飞行模式选择 AOI 形状）。
//   2. CelestialLodSystem：远处天体 LOD。
//   3. FlightStateTracker：飞行模式决定是否加载真实体素。
//
// 关键规则（设计文档 9.3、20.1）：
//   - 远处天体不得触发真实体素加载。
//   - Cruise/Warp 模式下不加载真实 chunk，只同步天体 LOD。
//   - LocalVoxelFlight/LandingApproach 模式下加载真实 chunk。

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <mutex>

#include "universe_types.hpp"
#include "sector_manager.hpp"
#include "chunk_streaming_system.hpp"
#include "celestial_lod_system.hpp"
#include "flight_state_tracker.hpp"
#include "universe_world_core.hpp"

namespace science_and_theology {

// 统一兴趣集合（见设计文档 12.2）。
struct InterestSet {
    // 需要加载/同步的 chunk（仅 LocalVoxelFlight/LandingApproach 模式）。
    std::vector<SectorChunkKey> chunks;

    // 需要同步 LOD 的天体列表。
    std::vector<CelestialLodResult> celestial_lods;

    // 需要同步的实体 ID（由上层根据 chunk 范围填充）。
    std::vector<uint64_t> entities;

    // 需要同步简略状态的机器 ID（由上层根据 chunk 范围填充）。
    std::vector<uint64_t> machines;

    // 当前飞行模式。
    FlightMode flight_mode = FlightMode::LocalVoxelFlight;

    // 当前需要真实体素加载（仅 LocalVoxelFlight/LandingApproach）。
    bool needs_real_voxels = true;

    // 本次被推迟的 chunk 数量（预算超限）。
    int deferred_chunks = 0;
};

// InterestManager — 统一兴趣区域管理。
//
// 整合 ChunkStreamingSystem、CelestialLodSystem 和 FlightStateTracker，
// 为每个玩家计算统一的兴趣集合。
//
// 线程安全：内部加锁。但引用的 SectorManager、UniverseWorldCore 等
// 只读访问，调用方需保证它们的生命周期。
class InterestManager {
public:
    InterestManager() = default;
    ~InterestManager() = default;

    InterestManager(const InterestManager&) = delete;
    InterestManager& operator=(const InterestManager&) = delete;

    // --- 子系统访问 ---

    ChunkStreamingSystem& chunk_streaming() { return chunk_streaming_; }
    CelestialLodSystem& celestial_lod() { return celestial_lod_; }
    FlightStateTracker& flight_tracker() { return flight_tracker_; }

    // --- 核心计算 ---

    // 计算玩家的统一兴趣集合。
    //
    // 流程：
    //   1. 从 FlightStateTracker 获取当前飞行模式。
    //   2. 根据飞行模式设置 ChunkStreamingSystem 的高速预测。
    //   3. 若需要真实体素（LocalVoxelFlight/LandingApproach），
    //      调用 ChunkStreamingSystem 计算附近 chunk。
    //   4. 调用 CelestialLodSystem 计算所有天体 LOD。
    //   5. 返回统一 InterestSet。
    //
    // 参数：
    //   player_id: 玩家 ID
    //   sector_manager: Sector 管理器（用于 chunk streaming）
    //   core: 统一宇宙核心（用于天体 LOD）
    InterestSet compute_interest(uint64_t player_id,
                                 const SectorManager& sector_manager,
                                 const UniverseWorldCore& core);

    // --- 便捷方法 ---

    // 注册玩家（同时注册到 ChunkStreamingSystem 和 FlightStateTracker）。
    void register_player(uint64_t player_id,
                         const GlobalPos& pos,
                         const GlobalPos& velocity,
                         SectorId current_sector);

    // 注销玩家。
    void unregister_player(uint64_t player_id);

    // 更新玩家位置和速度（同时更新 ChunkStreamingSystem 和 FlightStateTracker）。
    // 返回可选的飞行模式切换事件。
    std::optional<FlightModeChangeEvent> update_player(
        uint64_t player_id,
        const GlobalPos& pos,
        const GlobalPos& velocity);

    // 设置玩家着陆目标。
    void set_landing_target(uint64_t player_id,
                            const std::string& celestial_id,
                            double distance_to_surface);

    // 清除着陆目标。
    void clear_landing_target(uint64_t player_id);

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;
    ChunkStreamingSystem chunk_streaming_;
    CelestialLodSystem celestial_lod_;
    FlightStateTracker flight_tracker_;
};

} // namespace science_and_theology
