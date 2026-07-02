#pragma once

// ============================================================
// celestial_lod_pipeline.hpp — 天体 LOD 加载链路状态机（U4）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 9、18、21.2 (U4) 节。
//
// U4 工作项：实现 Billboard/Proxy -> LandingApproach -> SimplifiedMesh
// -> RealChunks 的加载链路。
//
// 加载链路（设计文档 9.3、18.3）：
//   Distant (Billboard/LowPoly)
//     -> Proxy (PlanetProxy)
//       -> Approach (LandingApproach，开始预加载真实 chunk)
//         -> Simplified (SimplifiedMesh，低精度地形 mesh)
//           -> Real (RealChunks，真实体素 chunk)
//
// 当玩家远离天体时反向转换：Real -> Simplified -> Approach -> Proxy -> Distant
//
// 本系统跟踪每个玩家对每个天体的 LOD 链路状态，记录转换事件。
// 与 CelestialLodSystem 的区别：
//   - CelestialLodSystem 根据距离计算"应该"使用哪个 LOD。
//   - CelestialLodPipeline 跟踪"实际"加载链路状态，记录转换事件，
//     用于驱动上层异步加载 / 卸载流程。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

#include "universe_types.hpp"
#include "celestial_lod_system.hpp"
#include "flight_state_tracker.hpp"

namespace science_and_theology {

// LOD 加载链路状态。
// 表示玩家对某天体的当前加载阶段。
enum class LodPipelineState : uint8_t {
    Distant     = 0,  // 远处：Billboard/LowPoly，只显示星点
    Proxy       = 1,  // 代理：PlanetProxy，显示星球球体模型
    Approach    = 2,  // 接近：LandingApproach，开始预加载真实 chunk
    Simplified  = 3,  // 简化：SimplifiedMesh，低精度地形 mesh
    Real        = 4,  // 真实：RealChunks，真实体素 chunk
    COUNT       = 5,
};

constexpr const char* kLodPipelineStateNames[] = {
    "Distant", "Proxy", "Approach", "Simplified", "Real",
};

inline const char* lod_pipeline_state_name(LodPipelineState s) {
    uint8_t i = static_cast<uint8_t>(s);
    if (i >= static_cast<uint8_t>(LodPipelineState::COUNT)) return "Unknown";
    return kLodPipelineStateNames[i];
}

// LOD 链路转换事件。
struct LodPipelineTransition {
    uint64_t player_handle;
    std::string celestial_id;
    LodPipelineState old_state;
    LodPipelineState new_state;
    GlobalPos pos;                  // 转换时的玩家位置
    double distance_to_surface;     // 到天体表面的距离
    std::string reason;             // 转换原因
};

// 玩家对单个天体的链路状态。
struct PlayerCelestialState {
    LodPipelineState state = LodPipelineState::Distant;
    double last_distance_to_surface = 0.0;
};

// CelestialLodPipeline — 天体 LOD 加载链路状态机。
//
// 根据 CelestialLodSystem 计算的 LOD 等级，更新每个玩家对每个天体的
// 加载链路状态，并记录转换事件。
//
// 转换规则：
//   - LOD 等级提升（玩家接近）时，链路状态逐步前进。
//   - LOD 等级下降（玩家远离）时，链路状态逐步后退。
//   - 每次只前进 / 后退一步，避免跳级（实际加载是异步的）。
//   - Approach 状态对应 LandingApproach 飞行模式，触发真实 chunk 预加载。
class CelestialLodPipeline {
public:
    CelestialLodPipeline() = default;
    ~CelestialLodPipeline() = default;

    CelestialLodPipeline(const CelestialLodPipeline&) = delete;
    CelestialLodPipeline& operator=(const CelestialLodPipeline&) = delete;

    // --- 玩家管理 ---

    // 注册玩家。
    void register_player(uint64_t player_handle);

    // 注销玩家。
    void unregister_player(uint64_t player_handle);

    // --- 链路更新 ---

    // 更新玩家对所有天体的链路状态。
    // player_pos: 玩家当前位置
    // lod_results: CelestialLodSystem 计算的所有天体 LOD 结果
    // flight_mode: 当前飞行模式（用于触发 Approach 状态）
    // landing_target_id: 着陆目标天体 id（LandingApproach 模式时有效）
    // 返回本次更新产生的转换事件列表。
    std::vector<LodPipelineTransition> update(
        uint64_t player_handle,
        const GlobalPos& player_pos,
        const std::vector<CelestialLodResult>& lod_results,
        FlightMode flight_mode,
        const std::string& landing_target_id);

    // --- 查询 ---

    // 查询玩家对某天体的当前链路状态。
    LodPipelineState get_state(uint64_t player_handle,
                               const std::string& celestial_id) const;

    // 查询玩家对所有天体的链路状态。
    std::vector<std::pair<std::string, LodPipelineState>> get_all_states(
        uint64_t player_handle) const;

    // 判断玩家对某天体是否处于需要真实 chunk 的状态。
    // Approach、Simplified、Real 状态需要真实 chunk（程度不同）。
    static bool needs_real_chunks(LodPipelineState state);

    // 判断玩家对某天体是否处于预加载阶段。
    // Approach 及以上状态表示正在预加载或已加载真实 chunk。
    static bool is_preloading_or_higher(LodPipelineState state);

    // --- 管理 ---

    void clear();

private:
    // 根据 LOD 等级计算目标链路状态。
    static LodPipelineState lod_to_pipeline_state(LodLevel lod);

    // 计算从当前状态到目标状态的单步转换。
    // 返回下一步状态（可能等于 current，表示无需转换）。
    static LodPipelineState compute_next_step(LodPipelineState current,
                                              LodPipelineState target);

    mutable std::mutex mutex_;

    // 玩家对每个天体的链路状态。
    // key: player_handle, value: {celestial_id -> state}
    std::unordered_map<uint64_t,
                       std::unordered_map<std::string, PlayerCelestialState>> states_;
};

} // namespace science_and_theology
