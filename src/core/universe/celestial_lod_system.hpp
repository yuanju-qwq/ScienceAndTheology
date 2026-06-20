#pragma once

// ============================================================
// celestial_lod_system.hpp — 天体 LOD 系统（纯 C++ 核心）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 9 节。
//
// 即使是统一宇宙，也不能在太空中同步整颗星球真实体素。
// 玩家在远处看到星球时，应该显示：
//   LOD 0：真实体素 chunk。
//   LOD 1：低精度地形 mesh。
//   LOD 2：星球球体模型 + 云层。
//   LOD 3：远处星点 / 图标。
//
// 同步规则（设计文档 9.3）：
//   近处：同步真实 chunk。
//   中距离：同步低精度地形 mesh。
//   远距离：同步 PlanetProxy / 星球模型。
//   极远距离：只同步星点或 UI 标记。
//
// 本系统将现有 GDPlanetLod 的计算逻辑下沉到纯 C++ 核心，
// 使 InterestManager 可以在无 Godot 依赖的情况下统一计算。
//
// LOD 距离阈值基于星球半径的比率（与 GDPlanetLod 保持一致）：
//   LOD 0: 0 ~ 0.4 * radius（地表附近）
//   LOD 1: 0.4 ~ 0.8 * radius（中距离）
//   LOD 2: 0.8 ~ 3.0 * radius（远距离）
//   LOD 3: 3.0 ~ 8.0 * radius（极远）
//   LOD 4: 8.0 * radius 及以上（深空星点）

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

#include "universe_types.hpp"
#include "universe_world_core.hpp"

namespace science_and_theology {

// LOD 等级（见设计文档 9.1）。
enum class LodLevel : uint8_t {
    Real        = 0,  // LOD 0：真实体素 chunk
    Simplified  = 1,  // LOD 1：低精度地形 mesh
    PlanetProxy = 2,  // LOD 2：星球球体模型 + 云层
    LowPoly     = 3,  // LOD 3：低模球体
    Billboard   = 4,  // LOD 4：远处星点 / 图标
    COUNT       = 5,
};

constexpr const char* kLodLevelNames[] = {
    "Real", "Simplified", "PlanetProxy", "LowPoly", "Billboard",
};

inline const char* lod_level_name(LodLevel l) {
    uint8_t i = static_cast<uint8_t>(l);
    if (i >= static_cast<uint8_t>(LodLevel::COUNT)) return "Unknown";
    return kLodLevelNames[i];
}

// 天体 LOD 查询结果。
struct CelestialLodResult {
    std::string celestial_id;
    LodLevel lod;
    double distance_to_center;     // 玩家到天体中心的距离
    double distance_to_surface;    // 玩家到天体表面的距离（负值表示在天体内部）
    double planet_radius;          // 天体半径
};

// LOD 距离比率（与 GDPlanetLod 保持一致）。
struct LodDistanceRatios {
    double lod0_max = 0.4;   // LOD 0 上界比率
    double lod1_max = 0.8;   // LOD 1 上界比率
    double lod2_max = 3.0;   // LOD 2 上界比率
    double lod3_max = 8.0;   // LOD 3 上界比率
    // LOD 4: lod3_max 及以上
};

// CelestialLodSystem — 天体 LOD 选择系统。
//
// 根据玩家位置和天体位置/半径，为每个天体选择合适的 LOD 等级。
// 线程安全：内部加锁。
class CelestialLodSystem {
public:
    CelestialLodSystem() = default;
    ~CelestialLodSystem() = default;

    CelestialLodSystem(const CelestialLodSystem&) = delete;
    CelestialLodSystem& operator=(const CelestialLodSystem&) = delete;

    // --- 配置 ---

    void set_ratios(const LodDistanceRatios& ratios);
    const LodDistanceRatios& ratios() const;

    // --- LOD 计算 ---

    // 计算单个天体的 LOD 等级。
    // player_pos: 玩家全局位置
    // body: 天体描述
    LodLevel choose_lod(const GlobalPos& player_pos,
                        const CelestialBodyDesc& body) const;

    // 计算单个天体的完整 LOD 结果。
    CelestialLodResult compute_lod_result(const GlobalPos& player_pos,
                                          const CelestialBodyDesc& body) const;

    // 批量计算所有天体的 LOD（从 UniverseWorldCore 获取天体列表）。
    std::vector<CelestialLodResult> compute_all_lods(
        const GlobalPos& player_pos,
        const UniverseWorldCore& core) const;

    // --- LOD 距离阈值 ---

    // 返回指定天体半径的 LOD 距离阈值（从天体中心算起）。
    // 返回数组：[lod0_max, lod1_max, lod2_max, lod3_max]
    // LOD 4 在 lod3_max 及以上。
    std::vector<double> compute_lod_distances(double planet_radius) const;

    // --- 辅助 ---

    // 计算玩家到天体表面的距离。
    // 负值表示玩家在天体内部。
    static double compute_surface_distance(const GlobalPos& player_pos,
                                           const CelestialBodyDesc& body);

    // 计算玩家到天体中心的距离。
    static double compute_center_distance(const GlobalPos& player_pos,
                                          const CelestialBodyDesc& body);

    // 判断指定 LOD 是否需要真实体素加载。
    // 只有 LOD 0 (Real) 需要真实体素。
    static bool needs_real_voxels(LodLevel lod);

    // 判断指定 LOD 是否需要任何天体可视化。
    // LOD 4 (Billboard) 仍需要星点/图标。
    // 返回 false 表示完全不需要可视化（不存在的情况）。
    static bool needs_visual(LodLevel lod);

private:
    mutable std::mutex mutex_;
    LodDistanceRatios ratios_;
};

} // namespace science_and_theology
