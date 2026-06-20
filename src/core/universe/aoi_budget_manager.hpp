#pragma once

// ============================================================
// aoi_budget_manager.hpp — U7 per-observer 分通道发送预算
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U7) 节。
//
// U7 工作项：chunk、实体、block delta、机器详情和天体 LOD 分通道
// 设置发送预算与优先级。
//
// 验收条件：任一客户端高速移动都不会挤占另一客户端的同步预算。
//
// AoiBudgetManager 的职责：
//   1. 为每个观察者（玩家）维护独立的分通道发送预算。
//   2. 通道包括：Chunk、Entity、BlockDelta、MachineDetail、CelestialLod。
//   3. 每个 tick 重置预算；发送时扣减；超预算的请求排队或丢弃。
//   4. 通道有优先级：高优先级通道先发送（如玩家附近 block delta）。
//   5. 预算按观察者独立分配，一个观察者的消耗不影响另一个。
//
// 模型：
//   - 每个观察者有 per-channel budget（每 tick 最大发送量）。
//   - 每个通道有 priority（越大越优先）。
//   - send(channel, count) 尝试发送，返回实际允许发送量。
//   - 超预算的请求记录为 deferred（推迟到下一 tick）。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 同步通道类型。
enum class SyncChannel : uint8_t {
    Chunk          = 0,  // chunk 数据（地形、方块）
    Entity         = 1,  // 实体创建/销毁/移动
    BlockDelta     = 2,  // 方块增量变化
    MachineDetail  = 3,  // 机器详情（GUI 打开时）
    CelestialLod   = 4,  // 天体 LOD 更新
    COUNT          = 5,
};

constexpr const char* kSyncChannelNames[] = {
    "Chunk", "Entity", "BlockDelta", "MachineDetail", "CelestialLod",
};

inline const char* sync_channel_name(SyncChannel ch) {
    uint8_t i = static_cast<uint8_t>(ch);
    if (i >= static_cast<uint8_t>(SyncChannel::COUNT)) return "Unknown";
    return kSyncChannelNames[i];
}

// 通道配置。
struct ChannelBudgetConfig {
    // 每 tick 最大发送量（0 = 无限制）。
    int max_per_tick = 0;

    // 优先级（越大越优先）。
    int priority = 0;

    // 超预算时是否推迟（true=推迟到下一 tick，false=丢弃）。
    bool defer_on_overflow = true;
};

// 默认通道配置（可被覆盖）。
struct ObserverBudgetConfig {
    ChannelBudgetConfig channels[static_cast<size_t>(SyncChannel::COUNT)];

    ObserverBudgetConfig() {
        // 默认配置
        channels[static_cast<size_t>(SyncChannel::Chunk)].max_per_tick = 4;
        channels[static_cast<size_t>(SyncChannel::Chunk)].priority = 10;
        channels[static_cast<size_t>(SyncChannel::Chunk)].defer_on_overflow = true;

        channels[static_cast<size_t>(SyncChannel::Entity)].max_per_tick = 32;
        channels[static_cast<size_t>(SyncChannel::Entity)].priority = 20;
        channels[static_cast<size_t>(SyncChannel::Entity)].defer_on_overflow = true;

        channels[static_cast<size_t>(SyncChannel::BlockDelta)].max_per_tick = 64;
        channels[static_cast<size_t>(SyncChannel::BlockDelta)].priority = 30;
        channels[static_cast<size_t>(SyncChannel::BlockDelta)].defer_on_overflow = true;

        channels[static_cast<size_t>(SyncChannel::MachineDetail)].max_per_tick = 8;
        channels[static_cast<size_t>(SyncChannel::MachineDetail)].priority = 40;
        channels[static_cast<size_t>(SyncChannel::MachineDetail)].defer_on_overflow = false;

        channels[static_cast<size_t>(SyncChannel::CelestialLod)].max_per_tick = 2;
        channels[static_cast<size_t>(SyncChannel::CelestialLod)].priority = 5;
        channels[static_cast<size_t>(SyncChannel::CelestialLod)].defer_on_overflow = false;
    }
};

// 通道统计。
struct ChannelStats {
    int total_sent = 0;       // 累计发送量
    int total_deferred = 0;   // 累计推迟量
    int total_dropped = 0;    // 累计丢弃量
    int current_used = 0;     // 本 tick 已用
};

// 观察者预算状态。
struct ObserverBudgetState {
    ObserverBudgetConfig config;
    ChannelStats channels[static_cast<size_t>(SyncChannel::COUNT)];
};

// 发送请求结果。
struct SendResult {
    int allowed = 0;        // 实际允许发送量
    int deferred = 0;       // 推迟到下一 tick 的量
    int dropped = 0;        // 丢弃的量
    bool exhausted = false; // 本通道本 tick 预算已耗尽
};

// AoiBudgetManager — per-observer 分通道发送预算管理器。
class AoiBudgetManager {
public:
    AoiBudgetManager() = default;
    ~AoiBudgetManager() = default;

    AoiBudgetManager(const AoiBudgetManager&) = delete;
    AoiBudgetManager& operator=(const AoiBudgetManager&) = delete;

    // --- 观察者管理 ---

    // 注册观察者（使用默认配置）。
    void register_observer(uint64_t observer_id);

    // 注册观察者（自定义配置）。
    void register_observer(uint64_t observer_id, const ObserverBudgetConfig& config);

    // 注销观察者。
    void unregister_observer(uint64_t observer_id);

    // 查询观察者是否已注册。
    bool has_observer(uint64_t observer_id) const;

    // 观察者数量。
    size_t observer_count() const;

    // --- 预算消耗 ---

    // 尝试在指定通道发送指定数量。
    // 返回实际允许发送量、推迟量和丢弃量。
    SendResult try_send(uint64_t observer_id, SyncChannel channel, int count);

    // 查询本 tick 剩余预算。
    // 返回 -1 表示无限制。
    int remaining_budget(uint64_t observer_id, SyncChannel channel) const;

    // --- tick 管理 ---

    // 推进一个 tick，重置所有观察者的 per-tick 预算。
    // 推迟的请求不累积（由调用方在下一 tick 重新请求）。
    void tick();

    // 当前 tick 计数。
    int64_t tick_counter() const;

    // --- 配置 ---

    // 设置观察者的通道配置。
    void set_channel_config(uint64_t observer_id, SyncChannel channel,
                             const ChannelBudgetConfig& config);

    // 设置全局默认配置（影响新注册的观察者）。
    void set_default_config(const ObserverBudgetConfig& config);
    const ObserverBudgetConfig& default_config() const;

    // --- 查询 ---

    // 获取观察者的通道统计。
    ChannelStats get_channel_stats(uint64_t observer_id, SyncChannel channel) const;

    // 获取观察者所有通道的总发送量。
    int total_sent(uint64_t observer_id) const;

    // 返回所有已注册观察者 id。
    std::vector<uint64_t> all_observer_ids() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;

    ObserverBudgetConfig default_config_;

    std::unordered_map<uint64_t, ObserverBudgetState> observers_;

    int64_t tick_counter_ = 0;
};

} // namespace science_and_theology
