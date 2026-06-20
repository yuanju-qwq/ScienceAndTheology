#include "aoi_budget_manager.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 观察者管理
// ============================================================

void AoiBudgetManager::register_observer(uint64_t observer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (observers_.count(observer_id) > 0) {
        return;
    }
    ObserverBudgetState state;
    state.config = default_config_;
    observers_.emplace(observer_id, std::move(state));
}

void AoiBudgetManager::register_observer(uint64_t observer_id,
                                          const ObserverBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    ObserverBudgetState state;
    state.config = config;
    observers_[observer_id] = std::move(state);
}

void AoiBudgetManager::unregister_observer(uint64_t observer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.erase(observer_id);
}

bool AoiBudgetManager::has_observer(uint64_t observer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return observers_.count(observer_id) > 0;
}

size_t AoiBudgetManager::observer_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return observers_.size();
}

// ============================================================
// 预算消耗
// ============================================================

SendResult AoiBudgetManager::try_send(uint64_t observer_id,
                                       SyncChannel channel,
                                       int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    SendResult result;

    auto it = observers_.find(observer_id);
    if (it == observers_.end()) {
        // 未注册的观察者：允许全部发送（不限制）
        result.allowed = count;
        return result;
    }

    size_t ch_idx = static_cast<size_t>(channel);
    auto& state = it->second;
    const auto& cfg = state.config.channels[ch_idx];
    auto& stats = state.channels[ch_idx];

    // 无限制
    if (cfg.max_per_tick <= 0) {
        result.allowed = count;
        stats.total_sent += count;
        stats.current_used += count;
        return result;
    }

    int remaining = cfg.max_per_tick - stats.current_used;
    if (remaining <= 0) {
        // 预算已耗尽
        result.exhausted = true;
        if (cfg.defer_on_overflow) {
            result.deferred = count;
            stats.total_deferred += count;
        } else {
            result.dropped = count;
            stats.total_dropped += count;
        }
        return result;
    }

    if (count <= remaining) {
        // 全部允许
        result.allowed = count;
        stats.total_sent += count;
        stats.current_used += count;
    } else {
        // 部分允许
        result.allowed = remaining;
        stats.total_sent += remaining;
        stats.current_used = cfg.max_per_tick;

        int overflow = count - remaining;
        result.exhausted = true;
        if (cfg.defer_on_overflow) {
            result.deferred = overflow;
            stats.total_deferred += overflow;
        } else {
            result.dropped = overflow;
            stats.total_dropped += overflow;
        }
    }

    return result;
}

int AoiBudgetManager::remaining_budget(uint64_t observer_id,
                                        SyncChannel channel) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = observers_.find(observer_id);
    if (it == observers_.end()) {
        return -1;  // 未注册，无限制
    }

    size_t ch_idx = static_cast<size_t>(channel);
    const auto& cfg = it->second.config.channels[ch_idx];
    const auto& stats = it->second.channels[ch_idx];

    if (cfg.max_per_tick <= 0) {
        return -1;  // 无限制
    }

    return cfg.max_per_tick - stats.current_used;
}

// ============================================================
// tick 管理
// ============================================================

void AoiBudgetManager::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, state] : observers_) {
        for (size_t i = 0; i < static_cast<size_t>(SyncChannel::COUNT); ++i) {
            state.channels[i].current_used = 0;
        }
    }

    ++tick_counter_;
}

int64_t AoiBudgetManager::tick_counter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_counter_;
}

// ============================================================
// 配置
// ============================================================

void AoiBudgetManager::set_channel_config(uint64_t observer_id,
                                           SyncChannel channel,
                                           const ChannelBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = observers_.find(observer_id);
    if (it == observers_.end()) {
        return;
    }

    it->second.config.channels[static_cast<size_t>(channel)] = config;
}

void AoiBudgetManager::set_default_config(const ObserverBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_config_ = config;
}

const ObserverBudgetConfig& AoiBudgetManager::default_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return default_config_;
}

// ============================================================
// 查询
// ============================================================

ChannelStats AoiBudgetManager::get_channel_stats(uint64_t observer_id,
                                                  SyncChannel channel) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = observers_.find(observer_id);
    if (it == observers_.end()) {
        return ChannelStats{};
    }

    return it->second.channels[static_cast<size_t>(channel)];
}

int AoiBudgetManager::total_sent(uint64_t observer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = observers_.find(observer_id);
    if (it == observers_.end()) {
        return 0;
    }

    int total = 0;
    for (size_t i = 0; i < static_cast<size_t>(SyncChannel::COUNT); ++i) {
        total += it->second.channels[i].total_sent;
    }
    return total;
}

std::vector<uint64_t> AoiBudgetManager::all_observer_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> result;
    result.reserve(observers_.size());
    for (const auto& [id, _] : observers_) {
        result.push_back(id);
    }
    return result;
}

// ============================================================
// 管理
// ============================================================

void AoiBudgetManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.clear();
    tick_counter_ = 0;
}

} // namespace science_and_theology
