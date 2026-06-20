#pragma once

// ============================================================
// universe_world_core.hpp — 统一宇宙核心
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 2、6 节。
//
// UniverseWorldCore 是统一宇宙的纯 C++ 权威入口。
// 它持有 SectorManager 和 StorageShardMap，负责：
//   1. Universe 标识与种子。
//   2. Sector 注册与查询（委托 SectorManager）。
//   3. 旧 dimension_id → Sector 的迁移映射（委托 StorageShardMap）。
//   4. 天体（星球、空间站）与 Sector 的关联。
//   5. 存档元数据版本（data_version）。
//
// U1 阶段：
//   - UniverseManager.gd 退为 Godot 场景和 UI 适配器。
//   - 真正的 Universe 描述、Sector 注册与坐标查询下沉到此 C++ 核心。
//   - 旧 API 通过 StorageShardMap 访问默认 Sector，新代码不再直接用 dimension_id。
//
// 线程安全：内部通过 SectorManager 和 StorageShardMap 的锁保护。
// UniverseWorldCore 自身的字段（universe_id、seed_、data_version_、
// celestial_bodies_）需要通过 core_mutex_ 保护。

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>

#include "universe_types.hpp"
#include "sector_manager.hpp"
#include "storage_shard.hpp"
#include "planet_environment.hpp"

namespace science_and_theology {

// 天体描述（见设计文档 6.1）。
// 一个天体关联地表 Sector 和轨道 Sector。
struct CelestialBodyDesc {
    std::string id;             // 天体字符串标识（如 "planet_alpha"）
    std::string name;           // 显示名
    GlobalPos center;           // 天体中心（连续坐标）
    double radius = 0.0;        // 天体半径（格）
    double atmosphere_radius = 0.0;  // 大气半径（格）

    SectorId surface_sector;    // 地表 Sector（可建造）
    SectorId orbit_sector;      // 轨道 Sector（稀疏 chunk）

    bool is_valid() const {
        return !id.empty() && radius > 0.0;
    }
};

// UniverseWorldCore — 统一宇宙核心
class UniverseWorldCore {
public:
    // 当前存档数据版本。
    // U1 阶段提升为 2，用于检测旧 dimension 存档并触发迁移路径。
    static constexpr uint32_t kCurrentDataVersion = 2;

    // 旧版（U0 及之前）存档数据版本。
    static constexpr uint32_t kLegacyDataVersion = 1;

    UniverseWorldCore() = default;
    ~UniverseWorldCore() = default;

    UniverseWorldCore(const UniverseWorldCore&) = delete;
    UniverseWorldCore& operator=(const UniverseWorldCore&) = delete;

    // --- Universe 元信息 ---

    void set_universe_id(UniverseId id);
    UniverseId universe_id() const;

    void set_seed(int64_t seed);
    int64_t seed() const;

    // 存档数据版本。加载时由 SaveManager 设置；新存档默认为 kCurrentDataVersion。
    void set_data_version(uint32_t version);
    uint32_t data_version() const;

    // 返回 true 表示当前存档是旧版（dimension_id 键），需要迁移路径。
    bool is_legacy_save() const;

    // --- Sector 管理（委托 SectorManager） ---

    SectorManager& sector_manager();
    const SectorManager& sector_manager() const;

    // 注册 Sector 并同时建立 legacy dimension_id 映射（如有）。
    // 若 desc.legacy_storage_shard 非空，会注册到 StorageShardMap。
    // 使用 register_sector_checked 语义（检测可建造重叠）。
    bool register_sector(const SectorDesc& desc,
                         SectorRegistryDiagnostics::Overlap* out_overlap = nullptr);

    // --- 旧 dimension_id 适配 ---

    // 通过旧 dimension_id 查找对应 SectorId。
    std::optional<SectorId> find_sector_by_dimension(const std::string& dimension_id) const;

    // 通过 SectorId 反查 legacy dimension_id。
    std::string find_legacy_dimension_id(SectorId sector) const;

    // StorageShardMap 直接访问（用于批量迁移）。
    StorageShardMap& storage_shard_map();
    const StorageShardMap& storage_shard_map() const;

    // --- 天体管理 ---

    // 注册天体。天体的 surface_sector 和 orbit_sector 应已注册到 SectorManager。
    bool register_celestial_body(const CelestialBodyDesc& body);

    // 通过字符串 id 查找天体。
    const CelestialBodyDesc* find_celestial_body(const std::string& id) const;

    // 返回所有天体（只读）。
    std::vector<const CelestialBodyDesc*> all_celestial_bodies() const;

    // --- 星球环境管理（U4） ---

    // 注册星球环境配置。celestial_id 应与已注册的 CelestialBodyDesc.id 对应。
    bool register_planet_environment(const PlanetEnvironment& env);

    // 通过天体 id 查找星球环境。
    const PlanetEnvironment* find_planet_environment(const std::string& celestial_id) const;

    // 返回所有星球环境（只读）。
    std::vector<const PlanetEnvironment*> all_planet_environments() const;

    // --- 坐标查询（便捷方法） ---

    // 查找全局方块坐标所属 Sector（委托 SectorManager）。
    SectorQueryResult find_sector(const GlobalBlockPos& pos) const;

    // 将全局方块坐标转换为 SectorChunkKey（委托 SectorManager）。
    std::optional<SectorChunkKey> make_chunk_key(const GlobalBlockPos& pos) const;

    // --- 诊断 ---

    // 计算 Sector 注册诊断（委托 SectorManager）。
    SectorRegistryDiagnostics compute_diagnostics() const;

    // --- 管理 ---

    // 清空所有状态（仅用于测试和重置）。
    void clear();

private:
    mutable std::mutex core_mutex_;

    UniverseId universe_id_{1};     // 默认单宇宙
    int64_t seed_ = 0;
    uint32_t data_version_ = kCurrentDataVersion;

    SectorManager sector_manager_;
    StorageShardMap storage_shard_map_;

    // 天体按字符串 id 索引。
    std::unordered_map<std::string, CelestialBodyDesc> celestial_bodies_;
    // 按注册顺序保存天体 id。
    std::vector<std::string> celestial_order_;

    // 星球环境按 celestial_id 索引（U4）。
    std::unordered_map<std::string, PlanetEnvironment> planet_environments_;
};

// 全局默认 UniverseWorldCore 实例。
// U1 阶段单进程单宇宙，使用全局实例简化迁移期适配。
// 后续阶段若需多实例隔离，可改为由 GameSession 持有。
UniverseWorldCore& global_universe_core();

} // namespace science_and_theology
