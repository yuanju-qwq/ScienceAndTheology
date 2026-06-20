#pragma once

// ============================================================
// structure_anchor_manager.hpp — 结构锚定与空 chunk 回收（U5）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 8.2、21.2 (U5) 节。
//
// U5 工作项：对太空结构使用稀疏 chunk，并定义空 chunk 回收、结构锚定
// 和最大连续建造预算。
//
// 太空结构（空间站、太空桥、飞船停靠点）需要锚定，防止 chunk 被回收。
// 当玩家离开后，未被锚定的空 chunk 可以被回收以释放内存。
//
// 规则（设计文档 8.2）：
//   - 结构锚点：标记某个 chunk 为"有结构"，不可回收。
//   - 空 chunk 回收：chunk 变空且无锚点时，可由回收策略移除。
//   - 建造预算：限制单次连续建造的 chunk 数量，防止恶意或错误导致
//     过多 chunk 创建。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>

#include "universe_types.hpp"
#include "block_space.hpp"

namespace science_and_theology {

// 结构锚点描述。
struct StructureAnchor {
    // 锚点 id（唯一标识）。
    uint64_t id = 0;

    // 锚点名称（诊断用，如 "space_station_core"、"bridge_pylon"）。
    std::string name;

    // 锚定的 chunk 集合。
    std::vector<SectorChunkKey> anchored_chunks;

    // 锚点所属天体或结构 id（如 "station_alpha"）。
    std::string owner_id;
};

// 建造预算配置。
struct BuildBudgetConfig {
    // 单次连续建造操作的最大 chunk 创建数。
    // 防止恶意或错误导致过多 chunk 创建。
    int max_chunks_per_operation = 64;

    // 单个 Sector 的最大 chunk 数（防止 Sector 过载）。
    int max_chunks_per_sector = 4096;

    // 全局最大 chunk 数（防止内存爆炸）。
    int max_total_chunks = 65536;
};

// StructureAnchorManager — 结构锚定与空 chunk 回收管理。
//
// 管理太空结构的锚点，控制空 chunk 回收和建造预算。
class StructureAnchorManager {
public:
    StructureAnchorManager() = default;
    ~StructureAnchorManager() = default;

    StructureAnchorManager(const StructureAnchorManager&) = delete;
    StructureAnchorManager& operator=(const StructureAnchorManager&) = delete;

    // --- 配置 ---

    void set_config(const BuildBudgetConfig& config);
    const BuildBudgetConfig& config() const;

    // --- 锚点管理 ---

    // 注册结构锚点。
    // 返回锚点 id（0 表示失败）。
    uint64_t register_anchor(const std::string& name,
                              const std::string& owner_id,
                              const std::vector<SectorChunkKey>& chunks);

    // 注销锚点。
    // 注销后，锚定的 chunk 不再受保护，可被回收。
    bool unregister_anchor(uint64_t anchor_id);

    // 查询锚点信息。
    const StructureAnchor* get_anchor(uint64_t anchor_id) const;

    // 返回所有锚点。
    std::vector<const StructureAnchor*> all_anchors() const;

    // 检查 chunk 是否被锚定。
    bool is_chunk_anchored(const SectorChunkKey& key) const;

    // --- 空 chunk 回收 ---

    // 检查 chunk 是否可以回收。
    // 可回收条件：chunk 为空且未被锚定。
    bool can_reclaim_chunk(const SectorChunkKey& key,
                            const BlockSpace& block_space) const;

    // 回收所有可回收的空 chunk。
    // 返回被回收的 chunk 数量。
    int reclaim_empty_chunks(BlockSpace& block_space);

    // --- 建造预算 ---

    // 检查是否可以在指定 Sector 内创建新 chunk。
    // 考虑单 Sector 预算和全局预算。
    bool can_create_chunk(SectorId sector,
                           const BlockSpace& block_space) const;

    // 检查是否可以在单次操作中创建指定数量的 chunk。
    bool can_create_chunks(int count) const;

    // 记录一次建造操作创建的 chunk 数（用于预算跟踪）。
    void record_chunk_creation(int count);

    // 重置当前操作的 chunk 创建计数（每次操作开始时调用）。
    void reset_operation_budget();

    // --- 管理 ---

    void clear();

    // 返回锚点数量。
    size_t anchor_count() const;

private:
    mutable std::mutex mutex_;
    BuildBudgetConfig config_;

    // 锚点按 id 索引。
    std::unordered_map<uint64_t, StructureAnchor> anchors_;

    // chunk 到锚点 id 的反向索引（一个 chunk 可被多个锚点引用）。
    std::unordered_map<SectorChunkKey, std::vector<uint64_t>> chunk_anchors_;

    // 下一个锚点 id。
    uint64_t next_anchor_id_ = 1;

    // 当前操作已创建的 chunk 数（用于单次操作预算）。
    int current_operation_chunks_ = 0;
};

} // namespace science_and_theology
