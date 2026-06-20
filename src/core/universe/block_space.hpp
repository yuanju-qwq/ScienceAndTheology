#pragma once

// ============================================================
// block_space.hpp — 统一方块空间（U5）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 8、21.2 (U5) 节。
//
// U5 工作项：实现基于 GlobalBlockPos 的 BlockSpace，邻居查询能够跨
// Sector 边界。方块放置、挖掘、block entity 注册、碰撞刷新和存档
// dirty 标记统一走 BlockSpace。
//
// BlockSpace 是统一宇宙中的方块访问层：
//   - 通过 SectorManager 查找方块所属 Sector。
//   - 通过 SectorChunkKey 定位 chunk 存储。
//   - 邻居查询自动跨 Sector 边界（设计文档 8.1）。
//   - 太空 Sector 使用稀疏 chunk（按需创建）。
//   - 存档 dirty 标记跟踪已修改的 chunk。
//
// 关键规则（设计文档 8.1、8.2）：
//   - 方块操作只允许在 allow_voxel_building == true 的 Sector 内。
//   - 跨 Sector 边界的邻居查询返回邻居方块（可能属于不同 Sector）。
//   - 太空 Sector 中的 chunk 按需创建（稀疏策略）。
//   - 修改方块后标记 chunk 为 dirty，用于存档。
//
// 线程安全：内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <array>

#include "universe_types.hpp"
#include "sector_manager.hpp"
#include "sparse_chunk_policy.hpp"

namespace science_and_theology {

// 方块 ID。0 表示空气（空方块）。
using BlockId = uint32_t;

constexpr BlockId kBlockAir = 0;

// Chunk 内方块存储。
// 使用固定大小数组，索引由 block_pos_to_chunk_local_index 计算。
struct ChunkBlocks {
    static constexpr size_t kBlockCount =
        static_cast<size_t>(kUniverseChunkSize)
        * kUniverseChunkSize * kUniverseChunkSize;

    std::array<BlockId, kBlockCount> blocks{};

    // 默认全为空气。
    ChunkBlocks() {
        blocks.fill(kBlockAir);
    }

    bool is_empty() const {
        for (BlockId b : blocks) {
            if (b != kBlockAir) {
                return false;
            }
        }
        return true;
    }

    void clear() {
        blocks.fill(kBlockAir);
    }

    BlockId get(uint32_t local_index) const {
        if (local_index >= kBlockCount) {
            return kBlockAir;
        }
        return blocks[local_index];
    }

    void set(uint32_t local_index, BlockId block) {
        if (local_index < kBlockCount) {
            blocks[local_index] = block;
        }
    }
};

// 方块查询结果。
struct BlockQueryResult {
    bool found = false;             // 是否找到方块（chunk 存在）
    BlockId block = kBlockAir;      // 方块 ID
    SectorId sector;                // 所属 Sector
    bool is_buildable = false;      // 所属 Sector 是否可建造
};

// BlockSpace — 统一方块空间。
//
// 提供基于 GlobalBlockPos 的方块访问，自动处理跨 Sector 边界。
class BlockSpace {
public:
    BlockSpace(SectorManager& sector_manager,
               SparseChunkPolicy& sparse_policy);
    ~BlockSpace() = default;

    BlockSpace(const BlockSpace&) = delete;
    BlockSpace& operator=(const BlockSpace&) = delete;

    // --- 方块查询 ---

    // 获取指定位置的方块。
    // 若 chunk 不存在，返回空气方块。
    BlockQueryResult get_block(const GlobalBlockPos& pos) const;

    // 获取邻居方块（跨 Sector 边界）。
    // 返回邻居位置的方块查询结果。
    BlockQueryResult get_neighbor(const GlobalBlockPos& pos,
                                   Direction dir) const;

    // 计算邻居位置（跨 Sector 边界）。
    // 返回邻居的全局方块坐标。
    static GlobalBlockPos neighbor_pos(const GlobalBlockPos& pos, Direction dir);

    // --- 方块操作 ---

    // 设置指定位置的方块。
    // 若 chunk 不存在且 block != kBlockAir，按需创建 chunk（稀疏策略）。
    // 若 Sector 不允许建造，返回 false。
    // 修改后标记 chunk 为 dirty。
    bool set_block(const GlobalBlockPos& pos, BlockId block);

    // 挖掘指定位置的方块（设置为空气）。
    // 若 chunk 不存在，返回 false（无需挖掘）。
    // 若挖掘后 chunk 为空，按策略回收（见 StructureAnchorManager）。
    bool dig_block(const GlobalBlockPos& pos);

    // 检查指定位置是否可以建造。
    // 需要 Sector 允许建造且方块位置有效。
    bool can_build_at(const GlobalBlockPos& pos) const;

    // --- Chunk 管理 ---

    // 检查 chunk 是否存在。
    bool has_chunk(const SectorChunkKey& key) const;

    // 确保 chunk 存在（按需创建）。
    // 对于稀疏 Sector，创建前会标记 chunk 有内容。
    // 返回 chunk 的指针，若 Sector 不允许建造返回 nullptr。
    ChunkBlocks* ensure_chunk(const SectorChunkKey& key);

    // 获取 chunk（只读）。
    const ChunkBlocks* get_chunk(const SectorChunkKey& key) const;

    // 移除 chunk。
    void remove_chunk(const SectorChunkKey& key);

    // 返回所有已加载的 chunk 数量。
    size_t chunk_count() const;

    // 返回所有已加载的 chunk 键。
    std::vector<SectorChunkKey> all_chunk_keys() const;

    // --- Dirty 标记 ---

    // 标记 chunk 为 dirty（已修改，需要保存）。
    void mark_dirty(const SectorChunkKey& key);

    // 清除 chunk 的 dirty 标记（已保存）。
    void clear_dirty(const SectorChunkKey& key);

    // 检查 chunk 是否 dirty。
    bool is_dirty(const SectorChunkKey& key) const;

    // 返回所有 dirty 的 chunk 键。
    std::vector<SectorChunkKey> get_dirty_chunks() const;

    // 返回 dirty chunk 数量。
    size_t dirty_count() const;

    // --- 管理 ---

    // 清空所有方块存储和 dirty 标记（仅用于测试和重置）。
    void clear();

private:
    // 查找方块所属 Sector。
    // 返回 SectorQueryResult。
    SectorQueryResult find_sector_for_pos(const GlobalBlockPos& pos) const;

    SectorManager& sector_manager_;
    SparseChunkPolicy& sparse_policy_;

    mutable std::mutex mutex_;

    // chunk 方块存储，按 SectorChunkKey 索引。
    std::unordered_map<SectorChunkKey, ChunkBlocks> chunks_;

    // dirty chunk 集合。
    std::unordered_set<SectorChunkKey> dirty_chunks_;
};

} // namespace science_and_theology
