#pragma once

// ============================================================
// planet_environment.hpp — 星球环境配置（U4）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 6、7、21.2 (U4) 节。
//
// U4 工作项：为第二颗星球注册地表和轨道 Sector，复用现有球形地形、
// 资源差异、大气和重力配置。
//
// PlanetEnvironment 描述一颗星球的物理与生成环境：
//   - 重力大小与衰减模式
//   - 大气密度与高度
//   - 地形 / 生物群系 / 资源种子
//   - 日长（昼夜循环）
//
// 该结构与 CelestialBodyDesc 解耦：
//   CelestialBodyDesc 描述空间几何（中心、半径、Sector 关联）；
//   PlanetEnvironment 描述玩法环境（重力、大气、生成参数）。
// UniverseWorldCore 通过天体 id 维护两者的关联。
//
// 线程安全：UniverseWorldCore 内部加锁保护。

#include <cstdint>
#include <string>

#include "universe_types.hpp"

namespace science_and_theology {

// 重力衰减模式（见设计文档 7.2）。
enum class GravityFalloff : uint8_t {
    InverseSquare = 0,  // 平方反比（真实物理，深空适用）
    Linear        = 1,  // 线性衰减（简化模型，地表附近稳定）
    Constant      = 2,  // 恒定重力（地表 Sector 内常用）
    COUNT         = 3,
};

constexpr const char* kGravityFalloffNames[] = {
    "InverseSquare", "Linear", "Constant",
};

inline const char* gravity_falloff_name(GravityFalloff f) {
    uint8_t i = static_cast<uint8_t>(f);
    if (i >= static_cast<uint8_t>(GravityFalloff::COUNT)) return "Unknown";
    return kGravityFalloffNames[i];
}

// 星球环境配置。
struct PlanetEnvironment {
    // 关联天体 id（与 CelestialBodyDesc.id 对应）。
    std::string celestial_id;

    // 表面重力大小（格/秒²）。
    // 地表 Sector 内玩家受到的向下加速度。
    double surface_gravity = 9.8;

    // 重力影响半径（格）。
    // 超过此距离后重力场不再影响玩家。
    double gravity_influence_radius = 0.0;

    // 重力衰减模式。
    GravityFalloff gravity_falloff = GravityFalloff::Constant;

    // 大气密度（0.0 = 无大气，1.0 = 标准大气）。
    double atmosphere_density = 0.0;

    // 大气高度（格，从表面向上）。
    double atmosphere_height = 0.0;

    // 地形生成种子。
    int64_t terrain_seed = 0;

    // 生物群系生成种子。
    int64_t biome_seed = 0;

    // 资源分布种子（矿石、流体等）。
    int64_t resource_seed = 0;

    // 日长（秒，完整的昼夜循环周期）。
    // 0 表示无昼夜循环（如潮汐锁定星球）。
    double day_length_seconds = 1200.0;

    bool is_valid() const {
        return !celestial_id.empty() && surface_gravity >= 0.0;
    }
};

} // namespace science_and_theology
