#include "performance_budget_monitor.hpp"

#include <sstream>
#include <iomanip>

namespace science_and_theology {

// ============================================================
// 配置
// ============================================================

void PerformanceBudgetMonitor::set_channel_config(
    PerfChannel channel, const PerfChannelBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_config_.channels[static_cast<size_t>(channel)] = config;
}

void PerformanceBudgetMonitor::set_window_size(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size == 0) size = 1;
    window_size_ = size;

    // 截断现有窗口
    for (auto& state : channels_) {
        while (state.recent_ms.size() > window_size_) {
            state.recent_ms.pop_front();
        }
    }
}

// ============================================================
// 记录
// ============================================================

bool PerformanceBudgetMonitor::record(PerfChannel channel, double elapsed_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t idx = static_cast<size_t>(channel);
    auto& state = channels_[idx];
    const auto& cfg = default_config_.channels[idx];

    state.stats.last_ms = elapsed_ms;
    state.stats.total_records++;
    state.recorded_this_tick = true;

    // 滚动窗口
    state.recent_ms.push_back(elapsed_ms);
    while (state.recent_ms.size() > window_size_) {
        state.recent_ms.pop_front();
    }

    // 尖峰检测
    bool is_spike = false;
    if (cfg.max_budget_ms > 0.0 && elapsed_ms > cfg.max_budget_ms) {
        is_spike = true;
        state.stats.spike_count++;
        state.stats.current_sustained_spikes++;
    } else {
        state.stats.current_sustained_spikes = 0;
    }
    state.spike_this_tick = is_spike;

    // 持续超限告警（仅在首次达到阈值时计数，低频策略）
    if (state.stats.current_sustained_spikes == cfg.sustained_threshold) {
        state.stats.sustained_alerts++;
    }

    update_stats_locked(channel);

    return is_spike;
}

void PerformanceBudgetMonitor::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t i = 0; i < static_cast<size_t>(PerfChannel::COUNT); ++i) {
        auto& state = channels_[i];
        if (!state.recorded_this_tick) {
            // 本 tick 未记录，重置连续尖峰计数
            state.stats.current_sustained_spikes = 0;
            state.spike_this_tick = false;
        }
        state.recorded_this_tick = false;
    }

    ++tick_counter_;
}

// ============================================================
// 查询
// ============================================================

PerfChannelStats PerformanceBudgetMonitor::get_channel_stats(PerfChannel channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_[static_cast<size_t>(channel)].stats;
}

bool PerformanceBudgetMonitor::is_sustained_exceeded(PerfChannel channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cfg = default_config_.channels[static_cast<size_t>(channel)];
    const auto& state = channels_[static_cast<size_t>(channel)];
    return state.stats.current_sustained_spikes >= cfg.sustained_threshold;
}

bool PerformanceBudgetMonitor::is_spike_this_tick(PerfChannel channel) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_[static_cast<size_t>(channel)].spike_this_tick;
}

int64_t PerformanceBudgetMonitor::tick_counter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_counter_;
}

// ============================================================
// 诊断
// ============================================================

std::vector<PerfChannel> PerformanceBudgetMonitor::sustained_exceeded_channels() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<PerfChannel> result;
    for (size_t i = 0; i < static_cast<size_t>(PerfChannel::COUNT); ++i) {
        const auto& cfg = default_config_.channels[i];
        const auto& state = channels_[i];
        if (state.stats.current_sustained_spikes >= cfg.sustained_threshold) {
            result.push_back(static_cast<PerfChannel>(i));
        }
    }
    return result;
}

std::string PerformanceBudgetMonitor::format_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << "[Perf] tick=" << tick_counter_;

    for (size_t i = 0; i < static_cast<size_t>(PerfChannel::COUNT); ++i) {
        const auto& state = channels_[i];
        const auto& cfg = default_config_.channels[i];

        oss << " " << kPerfChannelNames[i] << "="
            << std::fixed << std::setprecision(2)
            << state.stats.avg_ms << "/"
            << state.stats.max_ms << "/"
            << state.stats.p99_ms;

        if (cfg.max_budget_ms > 0.0) {
            oss << "(b" << cfg.max_budget_ms << ")";
        }
    }

    return oss.str();
}

// ============================================================
// 管理
// ============================================================

void PerformanceBudgetMonitor::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& state : channels_) {
        state.recent_ms.clear();
        state.stats = PerfChannelStats{};
        state.spike_this_tick = false;
        state.recorded_this_tick = false;
    }
    tick_counter_ = 0;
}

// ============================================================
// 内部：更新通道统计
// ============================================================

void PerformanceBudgetMonitor::update_stats_locked(PerfChannel channel) {
    size_t idx = static_cast<size_t>(channel);
    auto& state = channels_[idx];
    auto& stats = state.stats;

    if (state.recent_ms.empty()) {
        stats.avg_ms = 0.0;
        stats.max_ms = 0.0;
        stats.p99_ms = 0.0;
        return;
    }

    // 平均值
    double sum = std::accumulate(state.recent_ms.begin(), state.recent_ms.end(), 0.0);
    stats.avg_ms = sum / static_cast<double>(state.recent_ms.size());

    // 最大值
    stats.max_ms = *std::max_element(state.recent_ms.begin(), state.recent_ms.end());

    // p99 近似：排序后取 99% 位置
    std::vector<double> sorted(state.recent_ms.begin(), state.recent_ms.end());
    std::sort(sorted.begin(), sorted.end());
    size_t p99_idx = static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99);
    if (p99_idx >= sorted.size()) p99_idx = sorted.size() - 1;
    stats.p99_ms = sorted[p99_idx];
}

} // namespace science_and_theology
