#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology {

struct TickProfileConfig {
    bool enabled = false;
    double tick_budget_ms = 50.0;      // 20 TPS budget.
    double slow_scope_ms = 2.0;
    int64_t log_interval_ticks = 100;  // 5 seconds at 20 TPS.
    size_t window_size = 256;
    size_t top_n = 6;
};

struct TickProfileEntry {
    std::string name;
    double last_ms = 0.0;
    double avg_ms = 0.0;
    double max_ms = 0.0;
    double p99_ms = 0.0;
    double budget_share = 0.0;
    int64_t samples = 0;
    int64_t slow_samples = 0;
};

class TickProfiler {
public:
    TickProfiler() = default;

    void set_config(const TickProfileConfig& config);
    TickProfileConfig config() const;

    void set_enabled(bool enabled);
    bool enabled() const;

    void set_tick_budget_ms(double budget_ms);
    void set_slow_scope_ms(double threshold_ms);
    void set_log_interval_ticks(int64_t ticks);

    void begin_tick(int64_t tick);
    void record(const std::string& name, double elapsed_ms);
    void end_tick(double total_ms);

    std::vector<TickProfileEntry> snapshot_top(size_t limit = 0) const;
    std::string format_summary(size_t limit = 0) const;
    std::string consume_due_log();

    double last_tick_ms() const;
    int64_t current_tick() const;

    void clear();

private:
    struct ScopeState {
        std::deque<double> recent_ms;
        TickProfileEntry entry;
    };

    mutable std::mutex mutex_;
    TickProfileConfig config_;
    std::unordered_map<std::string, ScopeState> scopes_;
    double last_tick_ms_ = 0.0;
    int64_t current_tick_ = 0;
    bool log_due_ = false;

    void update_entry_locked(ScopeState& state);
};

class ScopedTickProfile {
public:
    ScopedTickProfile(TickProfiler* profiler, std::string name);
    ~ScopedTickProfile();

    ScopedTickProfile(const ScopedTickProfile&) = delete;
    ScopedTickProfile& operator=(const ScopedTickProfile&) = delete;

private:
    TickProfiler* profiler_ = nullptr;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
    bool active_ = false;
};

} // namespace science_and_theology
