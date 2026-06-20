#include "sector_simulation_scheduler.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 子系统注册
// ============================================================

void SectorSimulationScheduler::register_subsystem(const SectorSubsystem& subsystem) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 同名替换
    for (auto& existing : subsystems_) {
        if (existing.name == subsystem.name) {
            existing = subsystem;
            return;
        }
    }
    subsystems_.push_back(subsystem);
}

bool SectorSimulationScheduler::unregister_subsystem(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(subsystems_.begin(), subsystems_.end(),
                           [&](const SectorSubsystem& s) { return s.name == name; });
    if (it == subsystems_.end()) {
        return false;
    }
    subsystems_.erase(it);
    return true;
}

size_t SectorSimulationScheduler::subsystem_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subsystems_.size();
}

// ============================================================
// Sector 模拟等级管理
// ============================================================

void SectorSimulationScheduler::set_sector_level(SectorId sector, SimulationLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    sectors_[sector].level = level;
}

void SectorSimulationScheduler::sync_from_sector_manager(const SectorManager& sector_manager) {
    auto descs = sector_manager.all_sector_descs();

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto* desc : descs) {
        if (desc == nullptr) continue;
        sectors_[desc->id].level = desc->default_simulation;
    }
}

SimulationLevel SectorSimulationScheduler::get_sector_level(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sectors_.find(sector);
    if (it == sectors_.end()) {
        return SimulationLevel::Unloaded;
    }
    return it->second.level;
}

std::vector<SectorId> SectorSimulationScheduler::scheduled_sectors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SectorId> result;
    result.reserve(sectors_.size());
    for (const auto& [id, entry] : sectors_) {
        if (entry.level != SimulationLevel::Unloaded) {
            result.push_back(id);
        }
    }
    return result;
}

// ============================================================
// 调度配置
// ============================================================

void SectorSimulationScheduler::set_low_frequency_interval(int interval) {
    std::lock_guard<std::mutex> lock(mutex_);
    low_frequency_interval_ = std::max(1, interval);
}

int SectorSimulationScheduler::low_frequency_interval() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return low_frequency_interval_;
}

// ============================================================
// 调度决策
// ============================================================

ScheduleDecision SectorSimulationScheduler::decide(SimulationLevel level) const {
    switch (level) {
        case SimulationLevel::Unloaded:
        case SimulationLevel::Passive:
            return ScheduleDecision::Skip;
        case SimulationLevel::LowFrequency: {
            // 按 low_frequency_interval_ 间隔调度
            std::lock_guard<std::mutex> lock(mutex_);
            if (tick_counter_ % static_cast<int64_t>(low_frequency_interval_) == 0) {
                return ScheduleDecision::LowFrequency;
            }
            return ScheduleDecision::Skip;
        }
        case SimulationLevel::Active:
            return ScheduleDecision::Active;
        case SimulationLevel::HighPriority:
            return ScheduleDecision::HighPriority;
        default:
            return ScheduleDecision::Skip;
    }
}

// ============================================================
// 调度执行
// ============================================================

void SectorSimulationScheduler::tick(float delta) {
    // 快照当前状态（减少持锁时间）
    std::vector<SectorSubsystem> subsystems_snapshot;
    std::vector<std::pair<SectorId, SimulationLevel>> sector_snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        subsystems_snapshot = subsystems_;

        // 按 priority 排序（稳定排序，保持同优先级的注册顺序）
        std::stable_sort(subsystems_snapshot.begin(), subsystems_snapshot.end(),
                         [](const SectorSubsystem& a, const SectorSubsystem& b) {
                             return a.priority < b.priority;
                         });

        sector_snapshot.reserve(sectors_.size());
        for (const auto& [id, entry] : sectors_) {
            if (entry.level != SimulationLevel::Unloaded) {
                sector_snapshot.emplace_back(id, entry.level);
            }
        }

        ++tick_counter_;
    }

    size_t ticked_sectors = 0;
    size_t invocations = 0;

    // 按优先级顺序调度子系统
    for (const auto& subsystem : subsystems_snapshot) {
        for (const auto& [sector_id, level] : sector_snapshot) {
            ScheduleDecision decision = decide(level);

            if (decision == ScheduleDecision::Skip) {
                continue;
            }

            // active_only 的子系统只在 Active 及以上等级 tick
            if (subsystem.active_only &&
                decision != ScheduleDecision::Active &&
                decision != ScheduleDecision::HighPriority) {
                continue;
            }

            SectorTickContext ctx;
            ctx.sector = sector_id;
            ctx.level = level;
            ctx.decision = decision;
            ctx.tick_counter = tick_counter_;
            ctx.delta = delta;

            subsystem.tick_callback(ctx);
            ++invocations;
            ++ticked_sectors;  // 至少 tick 了一个子系统则计入
        }
    }

    // 更新统计
    std::lock_guard<std::mutex> lock(mutex_);
    last_ticked_sector_count_ = ticked_sectors;
    last_tick_invocations_ = invocations;
}

// ============================================================
// 查询
// ============================================================

int64_t SectorSimulationScheduler::tick_counter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_counter_;
}

size_t SectorSimulationScheduler::last_ticked_sector_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_ticked_sector_count_;
}

size_t SectorSimulationScheduler::last_tick_invocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_tick_invocations_;
}

// ============================================================
// 管理
// ============================================================

void SectorSimulationScheduler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subsystems_.clear();
    sectors_.clear();
    tick_counter_ = 0;
    last_ticked_sector_count_ = 0;
    last_tick_invocations_ = 0;
}

} // namespace science_and_theology
