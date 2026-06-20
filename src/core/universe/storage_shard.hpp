#pragma once

// ============================================================
// storage_shard.hpp — 迁移期 dimension_id 适配层
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 21.1 节。
//
// U1 阶段不删除现有 dimension_id，而是将其包装为 StorageShardId，
// 作为存档兼容键。新代码不得直接用 dimension_id 表达空间关系；
// 必须通过 StorageShardId 访问旧存档，或通过 SectorId 访问新空间。
//
// 迁移路径：
//   旧存档 dimension_id → StorageShardId → 首次加载时映射到默认 Sector
//   新存档 SectorId → 直接使用，legacy_storage_shard 可为空

#include <string>
#include <unordered_map>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 迁移期 StorageShard 与 Sector 的双向映射表。
// 线程安全：内部加锁，可被多个系统并发查询。
//
// 用途：
//   - 旧 API 通过 dimension_id 访问默认 Sector。
//   - 存档加载时根据 dimension_id 找到对应 Sector。
//   - 新代码通过 SectorId 反查 legacy_storage_shard（如有）。
class StorageShardMap {
public:
    StorageShardMap() = default;
    ~StorageShardMap() = default;

    StorageShardMap(const StorageShardMap&) = delete;
    StorageShardMap& operator=(const StorageShardMap&) = delete;

    // 注册一个 dimension_id → SectorId 的映射。
    // 同一 dimension_id 重复注册会更新映射（用于迁移期修正）。
    void register_shard(const std::string& dimension_id, SectorId sector);

    // 通过 dimension_id 查找 SectorId。
    // 返回 nullopt 表示未注册（调用方应按旧路径处理或拒绝）。
    std::optional<SectorId> find_sector(const std::string& dimension_id) const;

    // 通过 SectorId 反查 legacy dimension_id。
    // 返回空字符串表示该 Sector 没有旧存档映射。
    std::string find_legacy_dimension_id(SectorId sector) const;

    // 返回已注册的 shard 数量（诊断用）。
    size_t shard_count() const;

    // 清空映射（仅用于测试和重置）。
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SectorId> shard_to_sector_;
    std::unordered_map<SectorId, std::string> sector_to_shard_;
};

// 全局默认 StorageShardMap 实例。
// U1 阶段单进程单宇宙，使用全局实例简化迁移期适配。
// 后续阶段若需多宇宙隔离，可改为 UniverseWorldCore 持有。
StorageShardMap& global_storage_shard_map();

// 工具函数：将 dimension_id 包装为 StorageShardId。
inline StorageShardId make_storage_shard(const std::string& dimension_id) {
    StorageShardId shard;
    shard.value = dimension_id;
    return shard;
}

} // namespace science_and_theology
