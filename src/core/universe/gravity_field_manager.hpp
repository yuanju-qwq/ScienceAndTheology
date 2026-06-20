#pragma once

// ============================================================
// gravity_field_manager.hpp — 重力场管理（U4）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 7、21.2 (U4) 节。
//
// U4 工作项：定义多个重力场接近或重叠时的选择规则，避免方向瞬跳。
//
// 重力场规则（设计文档 7.1、7.2）：
//   - 每颗星球关联一个重力场，从天体中心向外辐射。
//   - 重力方向指向天体中心（球形重力）。
//   - 重力大小按 falloff 模式衰减。
//   - 多个重力场重叠时，选择引力最大的作为主导。
//   - 为避免方向瞬跳，引入滞后机制（hysteresis）：
//     当前主导场引力需降到次优场的 hysteresis_ratio 倍以下才切换。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>

#include "universe_types.hpp"
#include "planet_environment.hpp"

namespace science_and_theology {

// 重力场描述。
struct GravityField {
    // 关联天体 id。
    std::string celestial_id;

    // 重力场中心（天体中心）。
    GlobalPos center;

    // 天体半径（格）。
    double radius = 0.0;

    // 表面重力大小（格/秒²）。
    double surface_gravity = 9.8;

    // 重力影响半径（格）。
    double influence_radius = 0.0;

    // 衰减模式。
    GravityFalloff falloff = GravityFalloff::Constant;
};

// 重力查询结果。
struct GravityQueryResult {
    // 是否受到重力影响（false 表示在所有重力场影响范围外）。
    bool has_gravity = false;

    // 主导重力场的天体 id（无重力时为空）。
    std::string dominant_celestial_id;

    // 重力方向（单位向量，指向天体中心）。
    // 无重力时为 (0, -1, 0) 作为默认向下。
    GlobalPos direction{0.0, -1.0, 0.0};

    // 重力大小（格/秒²）。
    double magnitude = 0.0;

    // 玩家到主导天体表面的距离（格）。
    double distance_to_surface = 0.0;
};

// GravityFieldManager — 重力场管理器。
//
// 管理所有星球的重力场，计算玩家当前位置受到的重力。
// 多重力场重叠时通过滞后机制选择主导场，避免方向瞬跳。
class GravityFieldManager {
public:
    GravityFieldManager() = default;
    ~GravityFieldManager() = default;

    GravityFieldManager(const GravityFieldManager&) = delete;
    GravityFieldManager& operator=(const GravityFieldManager&) = delete;

    // --- 配置 ---

    // 滞后比率：当前主导场引力需降到次优场的此倍数以下才切换。
    // 默认 0.7，即次优场引力需超过主导场的 1/0.7 ≈ 1.43 倍才切换。
    void set_hysteresis_ratio(double ratio);
    double hysteresis_ratio() const;

    // --- 重力场管理 ---

    // 注册 / 更新重力场。
    void register_field(const GravityField& field);

    // 注销重力场。
    void unregister_field(const std::string& celestial_id);

    // 从 PlanetEnvironment 和天体描述构造并注册重力场。
    // 便捷方法，用于 U4 星球注册流程。
    void register_from_environment(const std::string& celestial_id,
                                   const GlobalPos& center,
                                   double radius,
                                   const PlanetEnvironment& env);

    // 清空所有重力场。
    void clear_fields();

    // --- 查询 ---

    // 计算玩家当前位置的重力。
    // 多重力场重叠时通过滞后机制选择主导场。
    // 玩家 id 用于跟踪每个玩家的主导场历史（滞后机制）。
    GravityQueryResult compute_gravity(uint64_t player_id,
                                        const GlobalPos& pos) const;

    // 计算单个重力场在指定位置的引力大小。
    // 不考虑滞后机制，纯物理计算。
    static double compute_field_magnitude(const GravityField& field,
                                          const GlobalPos& pos);

    // 计算单个重力场在指定位置的重力方向（单位向量，指向天体中心）。
    // 若玩家在天体中心，返回 (0, -1, 0) 作为默认。
    static GlobalPos compute_field_direction(const GravityField& field,
                                             const GlobalPos& pos);

    // 判断位置是否在重力场影响范围内。
    static bool is_in_field(const GravityField& field, const GlobalPos& pos);

    // --- 管理 ---

    // 清空所有状态（包括玩家主导场历史）。
    void clear();

    // 返回已注册的重力场数量。
    size_t field_count() const;

private:
    // 计算不考虑滞后时的最优重力场（引力最大的）。
    // 返回 celestial_id 和引力大小。无重力场影响时 celestial_id 为空。
    struct BestFieldResult {
        std::string celestial_id;
        double magnitude = 0.0;
        const GravityField* field = nullptr;
    };
    BestFieldResult compute_best_field(const GlobalPos& pos) const;

    mutable std::mutex mutex_;
    double hysteresis_ratio_ = 0.7;

    // 重力场按 celestial_id 索引。
    std::unordered_map<std::string, GravityField> fields_;

    // 每个玩家当前的主导重力场（滞后机制用）。
    // key: player_id, value: celestial_id
    mutable std::unordered_map<uint64_t, std::string> player_dominant_;
};

} // namespace science_and_theology
