#pragma once

// ============================================================
// sector_manager.hpp — Sector 注册与全局坐标转换
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 4、5 节。
//
// SectorManager 是统一宇宙中"全局坐标 → Sector → Sector 局部 chunk"
// 的唯一转换入口（见设计文档 5.4）。
//
// 职责：
//   1. 注册 / 查询 Sector 描述。
//   2. 全局方块坐标 → 所属 Sector（find_sector）。
//   3. 全局方块坐标 → SectorChunkKey（make_chunk_key）。
//   4. 检测 Sector 边界重叠 / 空洞（用于诊断和注册校验）。
//   5. 维护 Sector 模拟等级（运行时由上层根据玩家位置更新）。
//
// 边界归属规则（见设计文档 4.3、5.4）：
//   - Sector bounds 为闭区间 AABB64。
//   - 同一 GlobalBlockPos 最多归属一个"可建造"Sector。
//   - 允许多个非可建造 Sector（如轨道、深空）在空间上重叠，
//     但 find_sector 对可建造 Sector 返回唯一结果。
//   - 未归属任何 Sector 的位置返回 invalid SectorId。
//
// 负坐标 floor division（见 universe_types.hpp）：
//   block_pos_to_chunk_coord 已实现正确的 floor 语义。

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// Sector 查询结果。
// find_sector 返回此结构，明确区分"命中可建造 Sector"、
// "命中非可建造 Sector"和"未命中任何 Sector"。
struct SectorQueryResult {
    SectorId sector;                    // 命中的 Sector；未命中时 is_valid() == false
    const SectorDesc* desc = nullptr;   // 指向 Sector 描述；未命中时为 nullptr

    bool found() const { return sector.is_valid() && desc != nullptr; }
    bool is_buildable() const {
        return found() && desc->allow_voxel_building;
    }
};

// Sector 注册诊断结果（见设计文档 4.3：重叠/空洞检测）。
struct SectorRegistryDiagnostics {
    // 可建造 Sector 之间的重叠区域（不允许）。
    // 每项为一对 SectorId 和它们的重叠 AABB。
    struct Overlap {
        SectorId a;
        SectorId b;
        AABB64 overlap_bounds;
    };
    std::vector<Overlap> buildable_overlaps;

    // 所有已注册 Sector 的总数量。
    size_t total_sectors = 0;

    // 可建造 Sector 数量。
    size_t buildable_sectors = 0;

    bool has_buildable_overlap() const { return !buildable_overlaps.empty(); }
};

class SectorManager {
public:
    SectorManager() = default;
    ~SectorManager() = default;

    SectorManager(const SectorManager&) = delete;
    SectorManager& operator=(const SectorManager&) = delete;

    // --- Sector 注册 ---

    // 注册一个 Sector。
    // 返回 false 表示 SectorId 已存在或 bounds 无效。
    // 不会自动检测与其它可建造 Sector 的重叠；调用 register_sector_checked 进行检测。
    bool register_sector(const SectorDesc& desc);

    // 注册一个 Sector，并检测与现有可建造 Sector 的重叠。
    // 若与现有可建造 Sector 重叠，返回 false 并通过 out_overlap 输出冲突信息。
    bool register_sector_checked(const SectorDesc& desc,
                                 SectorRegistryDiagnostics::Overlap* out_overlap = nullptr);

    // 注销一个 Sector。
    // 返回 false 表示 SectorId 不存在。
    bool unregister_sector(SectorId id);

    // 查询 Sector 描述。
    const SectorDesc* get_sector_desc(SectorId id) const;

    // 返回所有已注册 Sector 的描述（只读）。
    std::vector<const SectorDesc*> all_sector_descs() const;

    // --- 坐标查询 ---

    // 查找全局方块坐标所属的 Sector。
    //
    // 规则（见设计文档 4.3、5.4）：
    //   1. 优先返回包含该坐标且 allow_voxel_building == true 的 Sector。
    //   2. 若无可建造 Sector，返回包含该坐标的任意 Sector（按注册顺序）。
    //   3. 若无 Sector 包含该坐标，返回未命中结果。
    //
    // 注意：可建造 Sector 之间不应重叠（由 register_sector_checked 保证）。
    // 若因直接 register_sector 导致可建造 Sector 重叠，本方法返回首个命中。
    SectorQueryResult find_sector(const GlobalBlockPos& pos) const;

    // 查找全局方块坐标所属的可建造 Sector。
    // 等价于 find_sector 后检查 is_buildable()。
    SectorQueryResult find_buildable_sector(const GlobalBlockPos& pos) const;

    // 将全局方块坐标转换为 SectorChunkKey。
    // 若该坐标不属于任何 Sector，返回 nullopt。
    // 若该坐标属于某 Sector，返回 {sector, chunk_coord_in_sector}。
    //
    // 注意：chunk 坐标是全局的（基于 GlobalBlockPos 的 floor division），
    // 不做 Sector 局部偏移。这样跨 Sector 边界的 chunk 查询语义一致。
    // SectorChunkKey.sector 表示该 chunk 归属哪个 Sector 进行管理。
    std::optional<SectorChunkKey> make_chunk_key(const GlobalBlockPos& pos) const;

    // 直接由 SectorId + 全局方块坐标构造 SectorChunkKey（不做归属校验）。
    // 用于已知 Sector 的内部操作。
    SectorChunkKey make_chunk_key_for_sector(SectorId sector,
                                             const GlobalBlockPos& pos) const;

    // --- 模拟等级 ---

    // 设置 Sector 的当前模拟等级（运行时由上层根据玩家位置更新）。
    void set_simulation_level(SectorId sector, SimulationLevel level);

    // 查询 Sector 的当前模拟等级。
    // 若 Sector 未注册，返回 Unloaded。
    SimulationLevel get_simulation_level(SectorId sector) const;

    // 返回所有非 Unloaded 的 Sector（即需要 tick 或保存的 Sector）。
    std::vector<SectorId> active_sectors() const;

    // --- 诊断 ---

    // 计算当前注册表的重叠诊断。
    // 用于注册后校验和测试。
    SectorRegistryDiagnostics compute_diagnostics() const;

    // --- 管理 ---

    // 清空所有注册（仅用于测试和重置）。
    void clear();

    // 返回已注册 Sector 数量。
    size_t sector_count() const;

private:
    struct SectorEntry {
        SectorDesc desc;
        SimulationLevel current_level = SimulationLevel::Unloaded;
    };

    mutable std::mutex mutex_;
    std::unordered_map<SectorId, SectorEntry> sectors_;

    // 按注册顺序保存 SectorId，用于 find_sector 的"首个命中"语义。
    std::vector<SectorId> registration_order_;
};

} // namespace science_and_theology
