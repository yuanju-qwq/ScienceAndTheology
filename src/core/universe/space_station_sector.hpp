#pragma once

// ============================================================
// space_station_sector.hpp — 空间站 Sector 管理（U5）
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 6.2、8.2、21.2 (U5) 节。
//
// U5 工作项：将 StationDescriptor 的正式实现迁为统一宇宙中的 SpaceStation
// Sector；旧 MapConnector 仅用于旧存档兼容。
//
// 空间站作为统一宇宙中的一个 Sector（SectorKind::SpaceStation）：
//   - 在宇宙坐标中有明确的位置和边界。
//   - 玩家可以直接接近和离开，不需要切换 dimension。
//   - 使用稀疏 chunk（只加载有方块的 chunk）。
//   - 空间站核心区域通过结构锚点保护，不被回收。
//   - 空间站可以有自己的重力场（小型、恒定向下）。
//
// 旧 StationDescriptor（GDScript Resource）仅用于旧存档兼容，
// 加载时迁移为 SpaceStationSector。
//
// 线程安全：UniverseWorldCore 内部加锁。

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>

#include "universe_types.hpp"
#include "planet_environment.hpp"
#include "sector_manager.hpp"
#include "sparse_chunk_policy.hpp"

namespace science_and_theology {

// 空间站类型（与旧 StationDescriptor.StationType 对应）。
enum class StationType : uint8_t {
    Outpost  = 0,  // 前哨站（小型，1x1x1 核心）
    Habitat  = 1,  // 居住站（中型，3x1x3 核心）
    Factory  = 2,  // 工厂站（大型，5x1x5 核心）
    COUNT    = 3,
};

constexpr const char* kStationTypeNames[] = {
    "Outpost", "Habitat", "Factory",
};

inline const char* station_type_name(StationType t) {
    uint8_t i = static_cast<uint8_t>(t);
    if (i >= static_cast<uint8_t>(StationType::COUNT)) return "Unknown";
    return kStationTypeNames[i];
}

// 空间站描述。
struct SpaceStationDesc {
    // 空间站 id（唯一标识）。
    std::string id;

    // 显示名。
    std::string name;

    // 空间站类型。
    StationType type = StationType::Outpost;

    // 空间站中心位置（宇宙坐标）。
    GlobalPos center;

    // 空间站 Sector id（注册到 SectorManager 后获得）。
    SectorId sector_id;

    // 空间站 bounds（Sector 边界）。
    AABB64 bounds;

    // 所属天体 id（如 "planet_alpha"），为空表示独立空间站。
    std::string parent_celestial_id;

    // 空间站种子（用于生成初始结构）。
    int64_t seed = 0;

    // 重力倍率（相对于标准重力 9.8）。
    double gravity_multiplier = 1.0;

    // 大气类型（0=无大气，1=稀薄，2=可呼吸）。
    int atmosphere_type = 2;

    // 核心区域大小（chunk 数，由类型决定）。
    // Outpost: 1x1x1, Habitat: 3x1x3, Factory: 5x1x5
    int core_size_x = 1;
    int core_size_y = 1;
    int core_size_z = 1;

    // 旧 dimension_id（存档迁移用，为空表示新空间站）。
    StorageShardId legacy_dimension_id;

    bool is_valid() const {
        return !id.empty() && sector_id.is_valid() && bounds.is_valid();
    }
};

// SpaceStationSectorManager — 空间站 Sector 管理器。
//
// 管理所有空间站的注册、查询和核心区域锚点。
// 空间站注册时会：
//   1. 在 SectorManager 中注册 SpaceStation Sector。
//   2. 计算核心区域 chunk 并标记为有内容（稀疏策略）。
//   3. 返回 SpaceStationDesc 供上层使用。
class SpaceStationSectorManager {
public:
    SpaceStationSectorManager() = default;
    ~SpaceStationSectorManager() = default;

    SpaceStationSectorManager(const SpaceStationSectorManager&) = delete;
    SpaceStationSectorManager& operator=(const SpaceStationSectorManager&) = delete;

    // --- 空间站注册 ---

    // 注册空间站。
    // 会在 SectorManager 中注册 SpaceStation Sector。
    // core_chunks 会被标记为有内容（稀疏策略）。
    // 返回 false 表示注册失败（SectorId 冲突或 bounds 无效）。
    bool register_station(SpaceStationDesc& desc,
                          SectorManager& sector_manager,
                          SparseChunkPolicy& sparse_policy);

    // 注销空间站。
    bool unregister_station(const std::string& station_id,
                             SectorManager& sector_manager,
                             SparseChunkPolicy& sparse_policy);

    // --- 查询 ---

    // 通过 id 查找空间站。
    const SpaceStationDesc* find_station(const std::string& id) const;

    // 返回所有空间站。
    std::vector<const SpaceStationDesc*> all_stations() const;

    // 通过 SectorId 查找空间站。
    const SpaceStationDesc* find_station_by_sector(SectorId sector) const;

    // --- 核心区域 ---

    // 计算空间站核心区域的 chunk 键。
    // 核心区域是空间站的初始结构区域，由类型决定大小。
    std::vector<SectorChunkKey> compute_core_chunks(const SpaceStationDesc& desc) const;

    // --- 工具 ---

    // 根据空间站类型计算核心区域大小。
    static void get_core_size(StationType type, int& size_x, int& size_y, int& size_z);

    // 计算空间站 Sector bounds（基于中心位置和核心大小）。
    static AABB64 compute_bounds(const GlobalPos& center, int core_size_x,
                                  int core_size_y, int core_size_z);

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;

    // 空间站按 id 索引。
    std::unordered_map<std::string, SpaceStationDesc> stations_;

    // SectorId 到空间站 id 的反向索引。
    std::unordered_map<SectorId, std::string> sector_to_station_;
};

} // namespace science_and_theology
