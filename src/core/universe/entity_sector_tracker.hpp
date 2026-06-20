#pragma once

// ============================================================
// entity_sector_tracker.hpp — 实体跨 Sector 迁移跟踪
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 17 节。
//
// 方案一没有 World 切换，但仍然有 Sector 迁移。
// 当实体移动到新 Sector 时，需要处理：
//   - 从旧 Sector 的实体分区移除。
//   - 加入新 Sector。
//   - 更新网络订阅。
//   - 更新物理区域。
//   - 更新重力规则。
//   - 更新碰撞和 chunk interest。
//
// 这是轻量级切换，不应该像切 World 那样重。
//
// EntitySectorTracker 负责检测实体何时跨越 Sector 边界，
// 并产出迁移事件供上层处理。

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <functional>

#include "universe_types.hpp"
#include "sector_manager.hpp"

namespace science_and_theology {

// 实体标识（复用 EntityId）。
// 使用 uint64_t 以保持与现有 EntityId 兼容。
struct EntityTrackingId {
    uint64_t value = 0;

    bool operator==(const EntityTrackingId& other) const { return value == other.value; }
    bool operator!=(const EntityTrackingId& other) const { return value != other.value; }
    bool is_valid() const { return value != 0; }
};

// 实体跨 Sector 迁移事件。
struct SectorMigrationEvent {
    EntityTrackingId entity;
    SectorId from_sector;   // 旧 Sector（可能 invalid 表示首次进入）
    SectorId to_sector;     // 新 Sector
    GlobalPos pos;          // 迁移时的位置
};

// 实体跟踪状态。
struct EntityTrackingState {
    EntityTrackingId entity;
    GlobalPos pos;
    SectorId current_sector;
};

// EntitySectorTracker — 跟踪实体位置并检测 Sector 迁移。
//
// 线程安全：内部加锁。
class EntitySectorTracker {
public:
    EntitySectorTracker() = default;
    ~EntitySectorTracker() = default;

    EntitySectorTracker(const EntitySectorTracker&) = delete;
    EntitySectorTracker& operator=(const EntitySectorTracker&) = delete;

    // --- 实体注册 ---

    // 注册一个实体。若实体已存在则更新位置。
    // 首次注册时根据位置查找所属 Sector。
    void register_entity(EntityTrackingId entity, const GlobalPos& pos,
                         const SectorManager& sector_manager);

    // 注销实体。
    void unregister_entity(EntityTrackingId entity);

    // --- 位置更新与迁移检测 ---

    // 更新实体位置，并检测是否跨越 Sector 边界。
    // 若发生迁移，返回迁移事件；否则返回 nullopt。
    // 迁移事件也会被加入到待处理事件队列。
    std::optional<SectorMigrationEvent> update_entity_position(
        EntityTrackingId entity,
        const GlobalPos& pos,
        const SectorManager& sector_manager);

    // 批量更新多个实体位置，返回所有迁移事件。
    std::vector<SectorMigrationEvent> update_positions(
        const std::vector<std::pair<EntityTrackingId, GlobalPos>>& updates,
        const SectorManager& sector_manager);

    // --- 查询 ---

    // 返回实体当前所在 Sector。
    SectorId get_entity_sector(EntityTrackingId entity) const;

    // 返回实体当前位置。
    std::optional<GlobalPos> get_entity_position(EntityTrackingId entity) const;

    // 返回某 Sector 内的所有实体 ID。
    std::vector<EntityTrackingId> get_entities_in_sector(SectorId sector) const;

    // 返回已注册实体数量。
    size_t entity_count() const;

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, EntityTrackingState> entities_;
};

} // namespace science_and_theology

// std::hash 特化
template <>
struct std::hash<science_and_theology::EntityTrackingId> {
    size_t operator()(const science_and_theology::EntityTrackingId& e) const {
        return std::hash<uint64_t>()(e.value);
    }
};
