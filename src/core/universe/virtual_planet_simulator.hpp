#pragma once

// ============================================================
// virtual_planet_simulator.hpp — U6 虚拟星球模拟器
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U6) 节。
//
// U6 工作项：完成 VirtualPlanetSimulator 产物回灌，保证重复加载、
// 异常退出和重试不会重复结算。
//
// 验收条件：休眠 Sector 恢复后状态可确定重放。
//
// VirtualPlanetSimulator 的职责：
//   1. 当星球 Sector 处于 Passive/LowFrequency 时，用简化模型模拟产物。
//   2. 当 Sector 恢复到 Active 时，将模拟产物"回灌"到真实世界。
//   3. 回灌必须幂等：重复加载、异常退出后重试不会重复结算。
//
// 幂等机制：
//   - 每次模拟会话生成唯一的 session_id。
//   - 每批产物有唯一的 batch_id（基于 session_id + 序号）。
//   - 回灌时记录已回灌的 batch_id，重复回灌同一 batch_id 会被拒绝。
//   - 异常退出后，通过日志恢复已回灌的 batch_id 集合。
//
// 模型：
//   - 模拟会话（SimulationSession）：一次 Passive/LowFrequency 期间的模拟。
//   - 产物批次（ProductBatch）：一次模拟产生的产物集合。
//   - 回灌日志（ReplayJournal）：记录已回灌的批次，用于幂等检查。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 产物条目。
struct ProductEntry {
    std::string resource_id;  // 资源标识（item_id / fluid_id）
    int64_t amount = 0;        // 数量
    std::string source_machine;  // 来源机器标识（可选）
};

// 产物批次。
// 一次模拟产生的产物集合，有唯一 batch_id。
struct ProductBatch {
    std::string batch_id;      // 唯一批次标识
    std::string session_id;    // 所属模拟会话
    SectorId sector;           // 产物所属 Sector
    int64_t simulated_tick;    // 模拟的 tick 时刻
    std::vector<ProductEntry> products;  // 产物列表
    bool replayed = false;     // 是否已回灌
};

// 模拟会话。
struct SimulationSession {
    std::string session_id;    // 唯一会话标识
    SectorId sector;           // 模拟的 Sector
    int64_t start_tick;        // 开始 tick
    int64_t end_tick;          // 结束 tick
    SimulationLevel level;     // 模拟时的等级
    bool completed = false;    // 是否正常完成
    bool replayed = false;     // 产物是否已全部回灌
};

// 回灌结果。
struct ReplayResult {
    bool success = false;      // 是否成功
    std::string batch_id;      // 回灌的批次 id
    std::string reason;        // 失败原因（若失败）
    std::vector<ProductEntry> applied_products;  // 实际应用的产物
};

// VirtualPlanetSimulator — 虚拟星球模拟器。
//
// 使用方式：
//   1. Sector 进入 Passive/LowFrequency 时，调用 begin_session()。
//   2. 模拟期间，调用 simulate_batch() 生成产物批次。
//   3. Sector 恢复到 Active 时，调用 replay_batch() 逐批回灌。
//   4. 异常退出后重启，调用 load_journal() 恢复已回灌记录。
class VirtualPlanetSimulator {
public:
    VirtualPlanetSimulator() = default;
    ~VirtualPlanetSimulator() = default;

    VirtualPlanetSimulator(const VirtualPlanetSimulator&) = delete;
    VirtualPlanetSimulator& operator=(const VirtualPlanetSimulator&) = delete;

    // --- 会话管理 ---

    // 开始一次模拟会话。
    // 返回 session_id（空表示失败，如已有进行中的会话）。
    std::string begin_session(SectorId sector,
                               int64_t start_tick,
                               SimulationLevel level);

    // 结束模拟会话。
    // 标记会话为已完成，准备回灌。
    bool end_session(const std::string& session_id);

    // 取消模拟会话（异常退出时调用）。
    // 已生成的批次保留，但会话标记为未完成。
    bool cancel_session(const std::string& session_id);

    // 查询会话。
    const SimulationSession* find_session(const std::string& session_id) const;
    size_t session_count() const;

    // --- 产物批次管理 ---

    // 生成一批产物。
    // batch_id 由内部自动生成（session_id + 序号）。
    // 返回生成的批次（nullptr 表示失败，如会话不存在或已完成）。
    std::optional<ProductBatch> simulate_batch(
        const std::string& session_id,
        int64_t simulated_tick,
        const std::vector<ProductEntry>& products);

    // 查询会话的所有批次。
    std::vector<ProductBatch> batches_for_session(const std::string& session_id) const;

    // 查询所有未回灌的批次。
    std::vector<ProductBatch> pending_batches() const;

    // --- 回灌（幂等） ---

    // 回灌一批产物。
    // 幂等：若 batch_id 已回灌，返回成功但不重复应用。
    // 返回回灌结果。
    ReplayResult replay_batch(const std::string& batch_id);

    // 回灌会话的所有未回灌批次。
    // 返回回灌的批次数量。
    int replay_session(const std::string& session_id);

    // 回灌所有未回灌批次。
    // 返回回灌的批次数量。
    int replay_all_pending();

    // 检查批次是否已回灌。
    bool is_batch_replayed(const std::string& batch_id) const;

    // --- 回灌日志（持久化用） ---

    // 导出已回灌的 batch_id 集合（用于持久化）。
    std::vector<std::string> export_replayed_batch_ids() const;

    // 导入已回灌的 batch_id 集合（用于异常恢复）。
    // 会合并到现有集合中。
    void import_replayed_batch_ids(const std::vector<std::string>& ids);

    // --- 统计 ---

    size_t total_batch_count() const;
    size_t replayed_batch_count() const;
    size_t pending_batch_count() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;

    // 会话表。
    std::unordered_map<std::string, SimulationSession> sessions_;

    // 批次表（batch_id → batch）。
    std::unordered_map<std::string, ProductBatch> batches_;

    // 已回灌的 batch_id 集合（幂等检查）。
    std::unordered_set<std::string> replayed_batch_ids_;

    // 会话内的批次序号（用于生成 batch_id）。
    std::unordered_map<std::string, int> session_batch_seq_;

    // 全局会话序号（用于生成 session_id）。
    int next_session_seq_ = 1;
};

} // namespace science_and_theology
