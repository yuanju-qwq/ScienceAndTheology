#pragma once

// ============================================================
// floating_origin_core.hpp — 浮动原点 C++ 核心
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 3.2 节。
//
// 客户端不要直接用超大全局坐标渲染，否则会出现浮点精度抖动。
// 客户端应该使用相机相对坐标：
//   RenderPos = entity.global_pos - client.camera_origin
//
// 服务端保存真实全局位置，客户端只使用局部渲染坐标。
//
// U2 阶段将 floating origin 扩展为运行时场景重基准：
//   超过阈值时整体平移渲染原点，同时保持逻辑坐标、速度、相机和
//   射线结果连续。
//
// FloatingOriginCore 提供：
//   1. 渲染原点管理（double 精度）。
//   2. 全局坐标 → 渲染坐标转换。
//   3. 重基准阈值检测。
//   4. 重基准事件通知（上层据此平移渲染根节点）。

#include <cstdint>
#include <optional>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 浮动原点配置。
struct FloatingOriginConfig {
    // 重基准阈值（格）。当玩家渲染坐标超过此值时触发重基准。
    // 设计文档建议：保持玩家附近在原点附近，避免 float 精度抖动。
    // float 在 4096 格以上开始有明显精度损失，所以阈值设为 2048。
    double rebase_threshold = 2048.0;

    // 重基准后玩家渲染坐标应接近 0。
    // 实际上将渲染原点设置为当前玩家全局坐标。
};

// 重基准事件。
struct RebasingEvent {
    GlobalPos old_origin;       // 旧渲染原点
    GlobalPos new_origin;       // 新渲染原点
    GlobalPos player_pos;       // 触发重基准的玩家位置
};

// FloatingOriginCore — 浮动原点 C++ 核心。
//
// 线程安全：内部加锁。
class FloatingOriginCore {
public:
    FloatingOriginCore() = default;
    ~FloatingOriginCore() = default;

    FloatingOriginCore(const FloatingOriginCore&) = delete;
    FloatingOriginCore& operator=(const FloatingOriginCore&) = delete;

    // --- 配置 ---

    void set_config(const FloatingOriginConfig& config);
    const FloatingOriginConfig& config() const;

    // --- 原点管理 ---

    // 设置渲染原点（直接设置，不触发重基准事件）。
    void set_origin(const GlobalPos& origin);

    // 获取当前渲染原点。
    GlobalPos origin() const;

    // --- 玩家位置 ---

    // 设置玩家全局位置。
    // 返回可选的重基准事件：若玩家渲染坐标超过阈值，返回重基准事件。
    // 调用方应据此平移渲染根节点。
    std::optional<RebasingEvent> set_player_position(const GlobalPos& pos);

    // 获取玩家全局位置。
    GlobalPos player_position() const;

    // --- 坐标转换 ---

    // 全局坐标 → 渲染坐标（float 精度，接近原点）。
    // 用于把远景星球、空间站等放置到正确的相对位置。
    // 返回 double 精度的渲染坐标；上层转换为 float 用于场景渲染。
    GlobalPos universe_to_render(const GlobalPos& universe_pos) const;

    // 全局方块坐标 → 渲染方块坐标（int64 精度）。
    // 用于方块渲染：渲染坐标 = 全局坐标 - 渲染原点（取整）。
    GlobalBlockPos universe_block_to_render(const GlobalBlockPos& block_pos) const;

    // 渲染坐标 → 全局坐标。
    GlobalPos render_to_universe(const GlobalPos& render_pos) const;

    // --- 查询 ---

    // 返回玩家当前渲染坐标（应始终接近原点）。
    GlobalPos player_render_position() const;

    // 返回玩家渲染坐标到原点的距离（用于诊断）。
    double player_render_distance() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;
    FloatingOriginConfig config_;
    GlobalPos origin_{0.0, 0.0, 0.0};
    GlobalPos player_pos_{0.0, 0.0, 0.0};
};

} // namespace science_and_theology
