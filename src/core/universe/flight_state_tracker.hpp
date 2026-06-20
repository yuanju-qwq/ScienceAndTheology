#pragma once

// ============================================================
// flight_state_tracker.hpp — 飞行模式与航行状态管理
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 18 节。
//
// 高速飞船不应依赖完整体素加载。引入飞行模式：
//   LocalVoxelFlight：真实体素飞行，适合地表、空间站附近、太空桥附近。
//   Cruise：宏观巡航，只同步飞船、星球 LOD、航线、大型结构 LOD。
//   Warp：更高速的跃迁模式，基本不加载真实体素。
//   LandingApproach：接近星球或空间站，开始预加载真实 chunk。
//
// 模式转换流程（设计文档 18.3）：
//   高速巡航 → 接近目标 → 退出巡航 → 预加载真实 chunk → 进入 LocalVoxelFlight
//
// U3 阶段：
//   - 引入 LocalVoxelFlight、Cruise、LandingApproach；Warp 接口稳定但可后置实现。
//   - LocalVoxelFlight 加载真实 voxel；Cruise 只保留飞船、航线、天体 LOD 和大型结构代理。
//   - 根据速度与航向计算前向预加载区域。
//   - 连续保存玩家/飞船的 GlobalPos、当前 Sector 和航行模式。

#include <cstdint>
#include <optional>
#include <mutex>
#include <unordered_map>

#include "universe_types.hpp"

namespace science_and_theology {

// 飞行模式（见设计文档 18.1）。
enum class FlightMode : uint8_t {
    LocalVoxelFlight = 0,  // 真实体素飞行（地表、空间站附近、太空桥附近）
    Cruise           = 1,  // 宏观巡航（只同步飞船、星球 LOD、航线）
    LandingApproach  = 2,  // 着陆接近（预加载真实 chunk）
    Warp             = 3,  // 跃迁（基本不加载真实体素）
    COUNT            = 4,
};

constexpr const char* kFlightModeNames[] = {
    "LocalVoxelFlight", "Cruise", "LandingApproach", "Warp",
};

inline const char* flight_mode_name(FlightMode m) {
    uint8_t i = static_cast<uint8_t>(m);
    if (i >= static_cast<uint8_t>(FlightMode::COUNT)) return "Unknown";
    return kFlightModeNames[i];
}

// 飞行模式配置阈值。
struct FlightModeConfig {
    // 进入 Cruise 模式的速度阈值（格/秒）。
    // 超过此速度时从 LocalVoxelFlight 切换到 Cruise。
    double cruise_speed_threshold = 200.0;

    // 进入 LandingApproach 模式的距离阈值（格）。
    // 当玩家接近星球或空间站表面在此距离内时切换到 LandingApproach。
    double landing_approach_distance = 2000.0;

    // LandingApproach 完成后切换到 LocalVoxelFlight 的距离阈值（格）。
    double local_flight_distance = 500.0;

    // 进入 Warp 模式的速度阈值（格/秒）。
    // U3 阶段 Warp 可后置，但阈值定义在此。
    double warp_speed_threshold = 10000.0;
};

// 飞行状态。
struct FlightState {
    FlightMode mode = FlightMode::LocalVoxelFlight;
    GlobalPos pos;              // 当前全局位置
    GlobalPos velocity;         // 当前速度（格/秒）
    SectorId current_sector;    // 当前所在 Sector

    // 目标天体（用于 LandingApproach 判断）。
    // 为空表示没有着陆目标。
    std::string target_celestial_id;

    // 到目标天体表面的距离（格）。
    // 由上层根据天体位置计算后设置。
    double distance_to_target_surface = 0.0;
};

// 飞行模式切换事件。
struct FlightModeChangeEvent {
    FlightMode old_mode;
    FlightMode new_mode;
    GlobalPos pos;              // 切换时的位置
    std::string reason;         // 切换原因（诊断用）
};

// FlightStateTracker — 跟踪玩家/飞船的飞行模式并处理模式转换。
//
// 线程安全：内部加锁。
class FlightStateTracker {
public:
    FlightStateTracker() = default;
    ~FlightStateTracker() = default;

    FlightStateTracker(const FlightStateTracker&) = delete;
    FlightStateTracker& operator=(const FlightStateTracker&) = delete;

    // --- 配置 ---

    void set_config(const FlightModeConfig& config);
    const FlightModeConfig& config() const;

    // --- 玩家/飞船状态 ---

    // 注册一个飞行主体（玩家或飞船）。
    void register_flyer(uint64_t flyer_id, const FlightState& state);

    // 注销飞行主体。
    void unregister_flyer(uint64_t flyer_id);

    // 更新飞行主体位置和速度，并自动检测模式转换。
    // 返回可选的模式切换事件。
    // 自动转换规则（设计文档 18.2、18.3）：
    //   LocalVoxelFlight → Cruise：速度超过 cruise_speed_threshold。
    //   Cruise → LandingApproach：接近目标天体表面在 landing_approach_distance 内。
    //   LandingApproach → LocalVoxelFlight：距离目标表面在 local_flight_distance 内。
    //   Cruise → Warp：速度超过 warp_speed_threshold（U3 可后置）。
    std::optional<FlightModeChangeEvent> update_flyer(
        uint64_t flyer_id,
        const GlobalPos& pos,
        const GlobalPos& velocity);

    // 手动设置飞行模式（用于玩家主动切换或上层逻辑强制切换）。
    std::optional<FlightModeChangeEvent> set_flight_mode(
        uint64_t flyer_id,
        FlightMode mode,
        const std::string& reason = "");

    // 设置着陆目标。
    void set_landing_target(uint64_t flyer_id,
                            const std::string& celestial_id,
                            double distance_to_surface);

    // 清除着陆目标。
    void clear_landing_target(uint64_t flyer_id);

    // --- 查询 ---

    FlightMode get_flight_mode(uint64_t flyer_id) const;
    FlightState get_flight_state(uint64_t flyer_id) const;
    SectorId get_flyer_sector(uint64_t flyer_id) const;

    // 返回当前是否处于高速模式（Cruise 或 Warp）。
    // 高速模式下 chunk streaming 应使用前向锥形 AOI。
    bool is_high_speed_mode(uint64_t flyer_id) const;

    // 返回当前是否需要加载真实体素。
    // LocalVoxelFlight 和 LandingApproach 需要真实体素；
    // Cruise 和 Warp 不需要。
    bool needs_real_voxels(uint64_t flyer_id) const;

    // --- 管理 ---

    void clear();

private:
    // 根据当前状态和配置自动决定目标飞行模式。
    FlightMode compute_target_mode(const FlightState& state) const;

    mutable std::mutex mutex_;
    FlightModeConfig config_;
    std::unordered_map<uint64_t, FlightState> flyers_;
};

} // namespace science_and_theology
