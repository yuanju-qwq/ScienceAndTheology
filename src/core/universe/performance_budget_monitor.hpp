#pragma once

// ============================================================
// performance_budget_monitor.hpp — U8 分通道性能预算与尖峰检测
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U8) 节。
//
// U8 工作项：对 chunk 生成、mesh、网络、模拟和存档分别建立可观测预算，
// 不用一个总耗时掩盖局部尖峰。
//
// 验收条件：长时间航行与多人分散运行不存在无界内存增长。
//
// PerformanceBudgetMonitor 的职责：
//   1. 为 5 个性能通道（ChunkGen、MeshBuild、Network、Simulation、Save）
//      分别维护独立的耗时预算（每 tick 最大耗时 ms）。
//   2. 记录每 tick 各通道实际耗时，检测尖峰（超预算）。
//   3. 维护滚动窗口统计（平均值、最大值、p99 近似、尖峰次数）。
//   4. 低频日志：只在状态变化或预算持续超限时输出（AGENTS.md 规则 2）。
//   5. 提供 budget_exceeded 查询，用于发布构建默认关闭详细诊断。
//
// 模型：
//   - 每个通道有 max_budget_ms（每 tick 最大允许耗时）。
//   - record(channel, elapsed_ms) 记录本 tick 该通道耗时。
//   - 滚动窗口大小可配置（默认 256 tick）。
//   - 尖峰定义：elapsed_ms > max_budget_ms。
//   - 持续超限定义：连续 N tick 尖峰（默认 8）。
//   - 内存上界：滚动窗口固定大小，无界增长由 cap 保护。

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <algorithm>
#include <numeric>

namespace science_and_theology {

// 性能通道类型。
enum class PerfChannel : uint8_t {
    ChunkGen    = 0,  // chunk 生成（地形、生物群系）
    MeshBuild   = 1,  // mesh 构建（区块渲染数据）
    Network     = 2,  // 网络同步（delta 生产、发送）
    Simulation  = 3,  // 模拟 tick（机器、生态系统、物理）
    Save        = 4,  // 存档写入/读取
    COUNT       = 5,
};

constexpr const char* kPerfChannelNames[] = {
    "ChunkGen", "MeshBuild", "Network", "Simulation", "Save",
};

inline const char* perf_channel_name(PerfChannel ch) {
    uint8_t i = static_cast<uint8_t>(ch);
    if (i >= static_cast<uint8_t>(PerfChannel::COUNT)) return "Unknown";
    return kPerfChannelNames[i];
}

// 通道配置。
struct PerfChannelBudgetConfig {
    // 每 tick 最大允许耗时（ms）。0 = 无限制。
    double max_budget_ms = 0.0;

    // 持续超限阈值：连续 N tick 尖峰才触发告警。
    int sustained_threshold = 8;
};

// 通道统计（滚动窗口）。
struct PerfChannelStats {
    double last_ms = 0.0;           // 最近一次耗时
    double max_ms = 0.0;            // 窗口内最大耗时
    double avg_ms = 0.0;            // 窗口内平均耗时
    double p99_ms = 0.0;            // 窗口内 p99 近似
    int64_t total_records = 0;      // 累计记录数
    int64_t spike_count = 0;        // 累计尖峰次数
    int64_t sustained_alerts = 0;   // 累计持续超限告警次数
    int current_sustained_spikes = 0;  // 当前连续尖峰计数
};

// 默认通道配置。
struct DefaultBudgetConfig {
    std::array<PerfChannelBudgetConfig, static_cast<size_t>(PerfChannel::COUNT)> channels;

    DefaultBudgetConfig() {
        // 默认预算（ms/tick），基于 60 FPS（16.67ms/frame）的合理分配
        channels[static_cast<size_t>(PerfChannel::ChunkGen)].max_budget_ms = 4.0;
        channels[static_cast<size_t>(PerfChannel::MeshBuild)].max_budget_ms = 3.0;
        channels[static_cast<size_t>(PerfChannel::Network)].max_budget_ms = 2.0;
        channels[static_cast<size_t>(PerfChannel::Simulation)].max_budget_ms = 5.0;
        channels[static_cast<size_t>(PerfChannel::Save)].max_budget_ms = 2.0;
    }
};

// PerformanceBudgetMonitor — 分通道性能预算与尖峰检测。
class PerformanceBudgetMonitor {
public:
    PerformanceBudgetMonitor() = default;
    ~PerformanceBudgetMonitor() = default;

    PerformanceBudgetMonitor(const PerformanceBudgetMonitor&) = delete;
    PerformanceBudgetMonitor& operator=(const PerformanceBudgetMonitor&) = delete;

    // --- 配置 ---

    // 设置通道预算配置。
    void set_channel_config(PerfChannel channel, const PerfChannelBudgetConfig& config);

    // 设置滚动窗口大小（tick 数）。默认 256。
    void set_window_size(size_t size);

    // --- 记录 ---

    // 记录某通道本 tick 的耗时（ms）。
    // 返回 true 表示触发尖峰（超预算）。
    bool record(PerfChannel channel, double elapsed_ms);

    // 推进一个 tick（用于持续超限检测的状态机推进）。
    // 若某通道本 tick 未 record，调用此方法会重置其连续尖峰计数。
    void tick();

    // --- 查询 ---

    // 获取通道统计（基于当前滚动窗口）。
    PerfChannelStats get_channel_stats(PerfChannel channel) const;

    // 查询某通道是否处于持续超限状态。
    bool is_sustained_exceeded(PerfChannel channel) const;

    // 查询某通道本 tick 是否尖峰。
    bool is_spike_this_tick(PerfChannel channel) const;

    // 当前 tick 计数。
    int64_t tick_counter() const;

    // --- 诊断 ---

    // 返回所有持续超限的通道列表（用于低频日志）。
    std::vector<PerfChannel> sustained_exceeded_channels() const;

    // 生成低频日志摘要（只在状态变化时调用）。
    // 格式："[Perf] tick=N ChunkGen=avg/max/p99 Network=avg/max/p99 ..."
    std::string format_summary() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;

    DefaultBudgetConfig default_config_;
    size_t window_size_ = 256;

    struct ChannelState {
        std::deque<double> recent_ms;  // 滚动窗口
        PerfChannelStats stats;
        bool spike_this_tick = false;
        bool recorded_this_tick = false;
    };

    std::array<ChannelState, static_cast<size_t>(PerfChannel::COUNT)> channels_;

    int64_t tick_counter_ = 0;

    // 更新通道统计（在锁内调用）。
    void update_stats_locked(PerfChannel channel);
};

} // namespace science_and_theology
