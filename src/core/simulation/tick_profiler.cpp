#include "tick_profiler.hpp"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <utility>

namespace science_and_theology {
namespace {

double elapsed_ms_since(std::chrono::steady_clock::time_point start) {
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

void TickProfiler::set_config(const TickProfileConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    if (config_.tick_budget_ms < 0.0) {
        config_.tick_budget_ms = 0.0;
    }
    if (config_.slow_scope_ms < 0.0) {
        config_.slow_scope_ms = 0.0;
    }
    if (config_.log_interval_ticks < 1) {
        config_.log_interval_ticks = 1;
    }
    if (config_.window_size == 0) {
        config_.window_size = 1;
    }
    for (auto& pair : scopes_) {
        auto& samples = pair.second.recent_ms;
        while (samples.size() > config_.window_size) {
            samples.pop_front();
        }
        update_entry_locked(pair.second);
    }
}

TickProfileConfig TickProfiler::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void TickProfiler::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.enabled = enabled;
}

bool TickProfiler::enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.enabled;
}

void TickProfiler::set_tick_budget_ms(double budget_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.tick_budget_ms = std::max(0.0, budget_ms);
    for (auto& pair : scopes_) {
        update_entry_locked(pair.second);
    }
}

void TickProfiler::set_slow_scope_ms(double threshold_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.slow_scope_ms = std::max(0.0, threshold_ms);
}

void TickProfiler::set_log_interval_ticks(int64_t ticks) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.log_interval_ticks = std::max<int64_t>(1, ticks);
}

void TickProfiler::begin_tick(int64_t tick) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_tick_ = tick;
}

void TickProfiler::record(const std::string& name, double elapsed_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.enabled || name.empty()) {
        return;
    }

    auto& state = scopes_[name];
    state.entry.name = name;
    state.entry.last_ms = elapsed_ms;
    state.entry.samples++;
    if (config_.slow_scope_ms > 0.0 && elapsed_ms > config_.slow_scope_ms) {
        state.entry.slow_samples++;
    }

    state.recent_ms.push_back(elapsed_ms);
    while (state.recent_ms.size() > config_.window_size) {
        state.recent_ms.pop_front();
    }
    update_entry_locked(state);
}

void TickProfiler::end_tick(double total_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.enabled) {
        return;
    }
    last_tick_ms_ = total_ms;
    if (config_.log_interval_ticks > 0
        && current_tick_ > 0
        && current_tick_ % config_.log_interval_ticks == 0) {
        log_due_ = true;
    }
}

std::vector<TickProfileEntry> TickProfiler::snapshot_top(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TickProfileEntry> result;
    result.reserve(scopes_.size());
    for (const auto& pair : scopes_) {
        result.push_back(pair.second.entry);
    }
    std::sort(result.begin(), result.end(),
        [](const TickProfileEntry& a, const TickProfileEntry& b) {
            if (a.avg_ms == b.avg_ms) {
                return a.max_ms > b.max_ms;
            }
            return a.avg_ms > b.avg_ms;
        });
    const size_t max_count = limit > 0 ? limit : config_.top_n;
    if (max_count > 0 && result.size() > max_count) {
        result.resize(max_count);
    }
    return result;
}

std::string TickProfiler::format_summary(size_t limit) const {
    TickProfileConfig cfg;
    double last_tick = 0.0;
    int64_t tick = 0;
    std::vector<TickProfileEntry> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg = config_;
        last_tick = last_tick_ms_;
        tick = current_tick_;
        entries.reserve(scopes_.size());
        for (const auto& pair : scopes_) {
            entries.push_back(pair.second.entry);
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const TickProfileEntry& a, const TickProfileEntry& b) {
            if (a.avg_ms == b.avg_ms) {
                return a.max_ms > b.max_ms;
            }
            return a.avg_ms > b.avg_ms;
        });

    const size_t max_count = limit > 0 ? limit : cfg.top_n;
    if (max_count > 0 && entries.size() > max_count) {
        entries.resize(max_count);
    }

    std::ostringstream oss;
    oss << "[TickProfiler] tick=" << tick
        << " total_ms=" << std::fixed << std::setprecision(2) << last_tick
        << " budget_ms=" << cfg.tick_budget_ms
        << " slow_ms=" << cfg.slow_scope_ms
        << " top=";

    if (entries.empty()) {
        oss << "none";
        return oss.str();
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (i > 0) {
            oss << " | ";
        }
        oss << e.name
            << " avg=" << e.avg_ms
            << " max=" << e.max_ms
            << " p99=" << e.p99_ms
            << " share=" << std::fixed << std::setprecision(1)
            << (e.budget_share * 100.0) << "%"
            << " samples=" << e.samples
            << " slow=" << e.slow_samples;
        oss << std::fixed << std::setprecision(2);
    }
    return oss.str();
}

std::string TickProfiler::consume_due_log() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!log_due_) {
            return std::string();
        }
        log_due_ = false;
    }
    return format_summary();
}

double TickProfiler::last_tick_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tick_ms_;
}

int64_t TickProfiler::current_tick() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_tick_;
}

void TickProfiler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    scopes_.clear();
    last_tick_ms_ = 0.0;
    current_tick_ = 0;
    log_due_ = false;
}

void TickProfiler::update_entry_locked(ScopeState& state) {
    if (state.recent_ms.empty()) {
        state.entry.avg_ms = 0.0;
        state.entry.max_ms = 0.0;
        state.entry.p99_ms = 0.0;
        state.entry.budget_share = 0.0;
        return;
    }

    const double sum = std::accumulate(
        state.recent_ms.begin(), state.recent_ms.end(), 0.0);
    state.entry.avg_ms = sum / static_cast<double>(state.recent_ms.size());
    state.entry.max_ms = *std::max_element(
        state.recent_ms.begin(), state.recent_ms.end());

    std::vector<double> sorted(state.recent_ms.begin(), state.recent_ms.end());
    std::sort(sorted.begin(), sorted.end());
    size_t p99_idx = static_cast<size_t>(
        static_cast<double>(sorted.size()) * 0.99);
    if (p99_idx >= sorted.size()) {
        p99_idx = sorted.size() - 1;
    }
    state.entry.p99_ms = sorted[p99_idx];
    state.entry.budget_share = config_.tick_budget_ms > 0.0
        ? state.entry.avg_ms / config_.tick_budget_ms
        : 0.0;
}

ScopedTickProfile::ScopedTickProfile(TickProfiler* profiler, std::string name)
    : profiler_(profiler)
    , name_(std::move(name)) {
    active_ = profiler_ != nullptr && profiler_->enabled();
    if (active_) {
        start_ = std::chrono::steady_clock::now();
    }
}

ScopedTickProfile::~ScopedTickProfile() {
    if (!active_ || profiler_ == nullptr) {
        return;
    }
    profiler_->record(name_, elapsed_ms_since(start_));
}

} // namespace science_and_theology
