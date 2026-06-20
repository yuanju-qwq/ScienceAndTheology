#pragma once

// ============================================================
// sector_simulation_scheduler.hpp — U6 分段模拟调度器
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 4.4、21.2 (U6) 节。
//
// U6 工作项：让 TickSystem 按 Sector/Chunk 的 Unloaded、Passive、
// LowFrequency、Active、HighPriority 等级调度。
//
// SectorSimulationScheduler 是统一宇宙中的模拟调度入口：
//   1. 按 Sector 的 SimulationLevel 决定是否 tick、tick 频率。
//   2. 子系统（机器、方块物理、生态、作物、电网、流体、物品管道、AE2）
//      只在所属 Sector 内 tick，不跨 Sector 遍历。
//   3. Unloaded 的 Sector 不 tick；Passive 只保存不 tick；
//      LowFrequency 低频 tick；Active 正常 tick；HighPriority 高优先级 tick。
//   4. 跨 Sector 交互只能经过 CrossSectorRelay（见 cross_sector_relay.hpp）。
//
// 与旧 TickSystem 的关系：
//   旧 TickSystem 基于 dimension + ChunkKey，继续服务于旧 dimension 代码路径。
//   新代码路径（统一宇宙）使用 SectorSimulationScheduler。
//   两者不互相调用，避免语义混淆。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>

#include "universe_types.hpp"
#include "sector_manager.hpp"

namespace science_and_theology {

// 模拟调度等级（与 SimulationLevel 对应，但明确表达调度语义）。
// 用于 SectorSimulationScheduler 决定 tick 行为。
enum class ScheduleDecision : uint8_t {
    Skip          = 0,  // Unloaded/Passive：不 tick
    LowFrequency  = 1,  // LowFrequency：每 N tick 一次
    Active        = 2,  // Active：每 tick 一次
    HighPriority  = 3,  // HighPriority：每 tick 一次，优先调度
};

// Sector 子系统 tick 上下文。
// 传递给子系统 tick 回调，包含当前 Sector 和调度信息。
struct SectorTickContext {
    SectorId sector;
    SimulationLevel level;
    ScheduleDecision decision;
    int64_t tick_counter;
    float delta;
};

// Sector 子系统 tick 回调签名。
// 子系统通过此回调被调度，只在指定 Sector 内执行。
using SectorSubsystemCallback = std::function<void(const SectorTickContext&)>;

// 子系统描述。
struct SectorSubsystem {
    // 子系统名称（如 "machine"、"power_network"、"fluid_network"）。
    std::string name;

    // 优先级（越小越先执行）。
    int priority = 0;

    // tick 回调。
    SectorSubsystemCallback tick_callback;

    // 是否只对 Active 及以上等级 tick（false 表示 LowFrequency 也 tick）。
    // 默认 false（LowFrequency 也参与）。
    bool active_only = false;
};

// SectorSimulationScheduler — 分段模拟调度器。
//
// 职责：
//   1. 维护 Sector 模拟等级（从 SectorManager 同步或手动设置）。
//   2. 按 SimulationLevel 调度子系统 tick。
//   3. 确保子系统只在所属 Sector 内执行，不跨 Sector 遍历。
//   4. 提供 LowFrequency 的 tick 间隔配置。
class SectorSimulationScheduler {
public:
    SectorSimulationScheduler() = default;
    ~SectorSimulationScheduler() = default;

    SectorSimulationScheduler(const SectorSimulationScheduler&) = delete;
    SectorSimulationScheduler& operator=(const SectorSimulationScheduler&) = delete;

    // --- 子系统注册 ---

    // 注册子系统。
    // 同名子系统会被替换。
    void register_subsystem(const SectorSubsystem& subsystem);

    // 注销子系统。
    bool unregister_subsystem(const std::string& name);

    // 查询已注册子系统数量。
    size_t subsystem_count() const;

    // --- Sector 模拟等级管理 ---

    // 设置 Sector 的模拟等级。
    // Unloaded 的 Sector 不会被调度。
    void set_sector_level(SectorId sector, SimulationLevel level);

    // 批量从 SectorManager 同步模拟等级。
    // 会读取 SectorManager 中所有 Sector 的 default_simulation 并设置。
    void sync_from_sector_manager(const SectorManager& sector_manager);

    // 获取 Sector 的模拟等级。
    SimulationLevel get_sector_level(SectorId sector) const;

    // 返回所有非 Unloaded 的 Sector（即需要调度的 Sector）。
    std::vector<SectorId> scheduled_sectors() const;

    // --- 调度配置 ---

    // LowFrequency 的 tick 间隔（每 N tick 一次）。
    void set_low_frequency_interval(int interval);
    int low_frequency_interval() const;

    // --- 调度执行 ---

    // 推进一帧。
    // delta = 实际时间（秒）。
    // 会按优先级顺序调度所有非 Unloaded/Passive 的 Sector 的子系统。
    void tick(float delta);

    // --- 查询 ---

    // 当前 tick 计数。
    int64_t tick_counter() const;

    // 上一帧实际调度的 Sector 数量。
    size_t last_ticked_sector_count() const;

    // 上一帧实际调度的子系统次数（Sector × 子系统）。
    size_t last_tick_invocations() const;

    // --- 调度决策（公开用于测试） ---

    // 根据 SimulationLevel 和当前 tick 计数决定调度行为。
    ScheduleDecision decide(SimulationLevel level) const;

    // --- 管理 ---

    void clear();

private:
    struct SectorEntry {
        SimulationLevel level = SimulationLevel::Unloaded;
    };

    mutable std::mutex mutex_;

    // 已注册子系统（按注册顺序，tick 时按 priority 排序）。
    std::vector<SectorSubsystem> subsystems_;

    // Sector 模拟等级。
    std::unordered_map<SectorId, SectorEntry> sectors_;

    // LowFrequency tick 间隔。
    int low_frequency_interval_ = 4;  // 默认每 4 tick 一次

    int64_t tick_counter_ = 0;

    // 上一帧统计。
    size_t last_ticked_sector_count_ = 0;
    size_t last_tick_invocations_ = 0;
};

} // namespace science_and_theology
