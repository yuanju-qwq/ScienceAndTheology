#pragma once

// ============================================================
// sparse_chunk_policy.hpp — 稀疏 chunk 策略
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 8.2 节。
//
// 太空不能生成满世界 chunk。规则：
//   没有方块、没有实体、没有生成内容的太空 chunk 不创建。
//   有玩家搭桥、空间站、小行星、飞船停靠点时才创建 chunk。
//
// SparseChunkPolicy 根据 SectorKind 决定是否应该生成某个 chunk：
//   - PlanetSurface：总是生成（地形存在）。
//   - PlanetOrbit / DeepSpace：默认不生成，只在有内容时生成。
//   - SpaceBridge：沿桥方向生成，其它位置不生成。
//   - SpaceStation / AsteroidField：按需生成。
//
// 本策略只回答"是否应该生成"，不执行生成。
// 上层在请求 chunk 生成前调用本策略过滤。

#include <unordered_set>
#include <mutex>

#include "universe_types.hpp"
#include "sector_manager.hpp"

namespace science_and_theology {

// 稀疏 chunk 策略。
class SparseChunkPolicy {
public:
    SparseChunkPolicy() = default;
    ~SparseChunkPolicy() = default;

    // 判断给定 Sector 内的 chunk 是否应该生成。
    //
    // 规则：
    //   - PlanetSurface：总是返回 true（地形总是存在）。
    //   - PlanetOrbit / DeepSpace：默认返回 false（稀疏）。
    //     上层在检测到该 chunk 有方块、实体或生成内容时应调用
    //     mark_chunk_has_content 后再查询。
    //   - SpaceBridge：沿桥方向返回 true（由 sector_manager 配置决定）。
    //   - SpaceStation / AsteroidField：默认返回 false（按需）。
    //
    // 参数 sector_desc：Sector 描述（由 SectorManager 提供）。
    // 参数 coord：chunk 坐标。
    bool should_generate_chunk(const SectorDesc& sector_desc,
                               const ChunkCoord& coord) const;

    // 标记某个 chunk 有内容（方块、实体、生成内容）。
    // 标记后 should_generate_chunk 对该 chunk 返回 true。
    // 用于稀疏 Sector 中按需创建 chunk。
    void mark_chunk_has_content(SectorId sector, const ChunkCoord& coord);

    // 取消标记某个 chunk 有内容。
    // 当 chunk 内容被完全移除时调用。
    void unmark_chunk_has_content(SectorId sector, const ChunkCoord& coord);

    // 查询某个 chunk 是否被标记为有内容。
    bool is_chunk_marked(SectorId sector, const ChunkCoord& coord) const;

    // 清空所有标记（仅用于测试和重置）。
    void clear();

    // 返回已标记的 chunk 数量（诊断用）。
    size_t marked_count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_set<SectorChunkKey> marked_chunks_;
};

} // namespace science_and_theology
