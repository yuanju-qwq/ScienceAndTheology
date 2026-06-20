// ============================================================
// u1_universe_core.cpp — U1 统一坐标与 Sector 核心测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U1）：
//   1. 坐标转换往返测试覆盖正负坐标、chunk/Sector 边界和大坐标。
//   2. 同一 GlobalBlockPos 最多归属一个可建造 Sector；未归属位置返回明确结果。
//   3. 旧 dimension_id 通过 StorageShardMap 适配层访问默认 Sector。
//   4. 存档 data_version 检测：旧版存档标记为 legacy。
//
// 本测试不依赖 Godot，纯 C++ 验证 snt_core 的 universe 模块。

#include <iostream>
#include <string>
#include <vector>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/storage_shard.hpp"
#include "universe/universe_world_core.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U1 FAIL] " << message << std::endl;
    ++g_failures;
}

// ============================================================
// 1. 坐标转换：floor division 与负坐标
// ============================================================

void test_coordinate_floor_division() {
    std::cerr << "[U1] test_coordinate_floor_division" << std::endl;

    const int64_t S = kUniverseChunkSize;  // 32

    // 正坐标：原点
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{0, 0, 0});
        check(c.cx == 0 && c.cy == 0 && c.cz == 0, "origin should be chunk (0,0,0)");
    }

    // 正坐标：chunk 边界前一格
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{S - 1, S - 1, S - 1});
        check(c.cx == 0 && c.cy == 0 && c.cz == 0, "last cell of chunk 0 should stay in chunk 0");
    }

    // 正坐标：chunk 边界
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{S, 0, 0});
        check(c.cx == 1 && c.cy == 0 && c.cz == 0, "first cell of chunk 1 should be chunk (1,0,0)");
    }

    // 负坐标：-1 应属于 chunk -1（floor division）
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{-1, 0, 0});
        check(c.cx == -1, "block -1 should be chunk -1 (floor division)");
    }

    // 负坐标：-S 应属于 chunk -1
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{-S, 0, 0});
        check(c.cx == -1, "block -S should be chunk -1");
    }

    // 负坐标：-(S+1) 应属于 chunk -2
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{-(S + 1), 0, 0});
        check(c.cx == -2, "block -(S+1) should be chunk -2");
    }

    // 负坐标：-(2*S) 应属于 chunk -2
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{-2 * S, 0, 0});
        check(c.cx == -2, "block -2S should be chunk -2");
    }

    // 三维负坐标
    {
        ChunkCoord c = block_pos_to_chunk_coord(GlobalBlockPos{-1, -1, -1});
        check(c.cx == -1 && c.cy == -1 && c.cz == -1, "(-1,-1,-1) should be chunk (-1,-1,-1)");
    }
}

// ============================================================
// 2. 坐标转换：往返测试
// ============================================================

void test_coordinate_round_trip() {
    std::cerr << "[U1] test_coordinate_round_trip" << std::endl;

    // 测试一系列坐标：正负、边界、大坐标
    std::vector<GlobalBlockPos> test_positions = {
        {0, 0, 0},
        {1, 1, 1},
        {31, 31, 31},               // chunk 0 最后一格
        {32, 32, 32},               // chunk 1 第一格
        {-1, -1, -1},               // chunk -1
        {-32, -32, -32},            // chunk -1 最后一格
        {-33, -33, -33},            // chunk -2
        {100000, 0, 0},             // 大坐标（星球间距量级）
        {-100000, 0, 0},
        {500000, 500000, 500000},   // 大坐标
        {-500000, -500000, -500000},
        {120000, 0, 0},             // 设计文档示例：星球 beta 中心
    };

    for (const auto& pos : test_positions) {
        ChunkCoord c = block_pos_to_chunk_coord(pos);
        GlobalBlockPos origin = chunk_coord_to_block_pos(c);

        // 验证 pos 在 chunk c 的范围内
        check(pos.x >= origin.x && pos.x < origin.x + kUniverseChunkSize,
              "pos.x should be in chunk x range");
        check(pos.y >= origin.y && pos.y < origin.y + kUniverseChunkSize,
              "pos.y should be in chunk y range");
        check(pos.z >= origin.z && pos.z < origin.z + kUniverseChunkSize,
              "pos.z should be in chunk z range");

        // 验证 local index 往返
        uint32_t local_idx = block_pos_to_chunk_local_index(pos);
        GlobalBlockPos restored = from_chunk_local(c, local_idx);
        check(restored == pos, "from_chunk_local should restore original position");
    }
}

// ============================================================
// 3. 坐标转换：大坐标（星球间距量级）
// ============================================================

void test_large_coordinates() {
    std::cerr << "[U1] test_large_coordinates" << std::endl;

    // 设计文档 8.3：星球间距 100,000~500,000 格
    GlobalBlockPos planet_a_center{0, 0, 0};
    GlobalBlockPos planet_b_center{120000, 0, 0};

    ChunkCoord chunk_a = block_pos_to_chunk_coord(planet_a_center);
    ChunkCoord chunk_b = block_pos_to_chunk_coord(planet_b_center);

    check(chunk_a.cx == 0, "planet A center chunk x should be 0");
    check(chunk_b.cx == 120000 / kUniverseChunkSize, "planet B center chunk x mismatch");

    // 验证两个星球中心不在同一 chunk
    check(chunk_a != chunk_b, "two planets should not share the same center chunk");

    // 验证大坐标的 local index 往返
    uint32_t idx_b = block_pos_to_chunk_local_index(planet_b_center);
    GlobalBlockPos restored_b = from_chunk_local(chunk_b, idx_b);
    check(restored_b == planet_b_center, "large coordinate round-trip failed");
}

// ============================================================
// 4. Sector 注册与坐标归属
// ============================================================

void test_sector_registration_and_ownership() {
    std::cerr << "[U1] test_sector_registration_and_ownership" << std::endl;

    SectorManager sm;

    // 注册一个地表 Sector（可建造）
    SectorDesc surface;
    surface.id = SectorId{100};
    surface.name = "planet_alpha_surface";
    surface.kind = SectorKind::PlanetSurface;
    surface.bounds = AABB64{GlobalBlockPos{-4096, -64, -4096},
                            GlobalBlockPos{4095, 255, 4095}};
    surface.allow_voxel_building = true;
    surface.allow_machines = true;
    surface.allow_power_network = true;
    surface.allow_logistics_network = true;
    surface.default_simulation = SimulationLevel::Active;

    check(sm.register_sector_checked(surface), "register surface sector should succeed");

    // 注册一个轨道 Sector（不可建造，可与地表空间重叠或相邻）
    SectorDesc orbit;
    orbit.id = SectorId{101};
    orbit.name = "planet_alpha_orbit";
    orbit.kind = SectorKind::PlanetOrbit;
    orbit.bounds = AABB64{GlobalBlockPos{-8192, 256, -8192},
                          GlobalBlockPos{8191, 16384, 8191}};
    orbit.allow_voxel_building = false;
    orbit.allow_machines = true;
    orbit.default_simulation = SimulationLevel::Passive;

    check(sm.register_sector_checked(orbit), "register orbit sector should succeed");

    // 验证地表中心归属可建造 Sector
    SectorQueryResult q = sm.find_sector(GlobalBlockPos{0, 64, 0});
    check(q.found(), "surface center should find a sector");
    check(q.is_buildable(), "surface center should be buildable");
    check(q.sector == SectorId{100}, "surface center should belong to surface sector");

    // 验证轨道区域归属轨道 Sector（非可建造）
    SectorQueryResult q_orbit = sm.find_sector(GlobalBlockPos{0, 1000, 0});
    check(q_orbit.found(), "orbit position should find a sector");
    check(!q_orbit.is_buildable(), "orbit position should not be buildable");
    check(q_orbit.sector == SectorId{101}, "orbit position should belong to orbit sector");

    // 验证未归属位置返回明确结果
    SectorQueryResult q_deep = sm.find_sector(GlobalBlockPos{100000, 100000, 100000});
    check(!q_deep.found(), "deep space position should not find any sector");
    check(!q_deep.is_buildable(), "deep space position should not be buildable");

    // 验证 make_chunk_key
    auto key = sm.make_chunk_key(GlobalBlockPos{0, 64, 0});
    check(key.has_value(), "make_chunk_key should return value for surface position");
    check(key->sector == SectorId{100}, "chunk key sector mismatch");
    check(key->coord == ChunkCoord{0, 2, 0}, "chunk key coord mismatch for (0,64,0)");

    // 验证未归属位置的 make_chunk_key 返回 nullopt
    auto key_deep = sm.make_chunk_key(GlobalBlockPos{100000, 100000, 100000});
    check(!key_deep.has_value(), "make_chunk_key should return nullopt for unowned position");
}

// ============================================================
// 5. 可建造 Sector 重叠检测
// ============================================================

void test_buildable_sector_overlap_detection() {
    std::cerr << "[U1] test_buildable_sector_overlap_detection" << std::endl;

    SectorManager sm;

    SectorDesc a;
    a.id = SectorId{1};
    a.name = "sector_a";
    a.kind = SectorKind::PlanetSurface;
    a.bounds = AABB64{GlobalBlockPos{0, 0, 0}, GlobalBlockPos{99, 99, 99}};
    a.allow_voxel_building = true;

    check(sm.register_sector_checked(a), "register sector A should succeed");

    // 注册与 A 重叠的可建造 Sector B（应失败）
    SectorDesc b;
    b.id = SectorId{2};
    b.name = "sector_b";
    b.kind = SectorKind::PlanetSurface;
    b.bounds = AABB64{GlobalBlockPos{50, 50, 50}, GlobalBlockPos{150, 150, 150}};
    b.allow_voxel_building = true;

    SectorRegistryDiagnostics::Overlap overlap;
    check(!sm.register_sector_checked(b, &overlap), "overlapping buildable sector B should be rejected");
    check(overlap.a == SectorId{1}, "overlap.a should be sector A");
    check(overlap.b == SectorId{2}, "overlap.b should be sector B");
    check(overlap.overlap_bounds.min.x == 50, "overlap min x should be 50");
    check(overlap.overlap_bounds.max.x == 99, "overlap max x should be 99");

    // 注册与 A 不重叠的可建造 Sector C（应成功）
    SectorDesc c;
    c.id = SectorId{3};
    c.name = "sector_c";
    c.kind = SectorKind::PlanetSurface;
    c.bounds = AABB64{GlobalBlockPos{200, 0, 0}, GlobalBlockPos{299, 99, 99}};
    c.allow_voxel_building = true;

    check(sm.register_sector_checked(c), "non-overlapping buildable sector C should succeed");

    // 诊断：不应有可建造重叠
    SectorRegistryDiagnostics diag = sm.compute_diagnostics();
    check(!diag.has_buildable_overlap(), "diagnostics should report no buildable overlap");
    check(diag.total_sectors == 2, "diagnostics should report 2 sectors");
    check(diag.buildable_sectors == 2, "diagnostics should report 2 buildable sectors");

    // 验证边界点归属：A 和 C 之间的位置（100~199）不属于任何可建造 Sector
    SectorQueryResult q_mid = sm.find_buildable_sector(GlobalBlockPos{150, 50, 50});
    check(!q_mid.found(), "gap between A and C should not be buildable");
}

// ============================================================
// 6. 非可建造 Sector 允许空间重叠
// ============================================================

void test_non_buildable_overlap_allowed() {
    std::cerr << "[U1] test_non_buildable_overlap_allowed" << std::endl;

    SectorManager sm;

    // 深空 Sector 和轨道 Sector 可以在空间上重叠
    SectorDesc deep_space;
    deep_space.id = SectorId{1};
    deep_space.name = "deep_space";
    deep_space.kind = SectorKind::DeepSpace;
    deep_space.bounds = AABB64{GlobalBlockPos{0, 0, 0}, GlobalBlockPos{10000, 10000, 10000}};
    deep_space.allow_voxel_building = false;

    SectorDesc orbit;
    orbit.id = SectorId{2};
    orbit.name = "orbit";
    orbit.kind = SectorKind::PlanetOrbit;
    orbit.bounds = AABB64{GlobalBlockPos{5000, 5000, 5000}, GlobalBlockPos{6000, 6000, 6000}};
    orbit.allow_voxel_building = false;

    check(sm.register_sector_checked(deep_space), "register deep_space should succeed");
    check(sm.register_sector_checked(orbit), "register overlapping orbit (non-buildable) should succeed");

    // 诊断：不应有可建造重叠（因为都不是可建造）
    SectorRegistryDiagnostics diag = sm.compute_diagnostics();
    check(!diag.has_buildable_overlap(), "non-buildable overlap should not be reported");
    check(diag.total_sectors == 2, "should have 2 sectors");
    check(diag.buildable_sectors == 0, "should have 0 buildable sectors");
}

// ============================================================
// 7. StorageShardMap 迁移映射
// ============================================================

void test_storage_shard_migration() {
    std::cerr << "[U1] test_storage_shard_migration" << std::endl;

    StorageShardMap map;

    // 注册 dimension_id → SectorId 映射
    map.register_shard("overworld", SectorId{100});
    map.register_shard("space_station_alpha", SectorId{200});

    // 查找
    auto s1 = map.find_sector("overworld");
    check(s1.has_value() && s1->value == 100, "overworld should map to sector 100");

    auto s2 = map.find_sector("space_station_alpha");
    check(s2.has_value() && s2->value == 200, "space_station_alpha should map to sector 200");

    auto s3 = map.find_sector("unknown_dimension");
    check(!s3.has_value(), "unknown dimension should return nullopt");

    // 反查
    check(map.find_legacy_dimension_id(SectorId{100}) == "overworld",
          "sector 100 should reverse-map to overworld");
    check(map.find_legacy_dimension_id(SectorId{999}).empty(),
          "unknown sector should reverse-map to empty string");

    check(map.shard_count() == 2, "shard count should be 2");
}

// ============================================================
// 8. UniverseWorldCore 集成
// ============================================================

void test_universe_world_core_integration() {
    std::cerr << "[U1] test_universe_world_core_integration" << std::endl;

    UniverseWorldCore core;
    core.clear();  // 确保干净状态

    core.set_seed(20260619);
    core.set_data_version(UniverseWorldCore::kCurrentDataVersion);

    check(core.seed() == 20260619, "seed mismatch");
    check(core.data_version() == UniverseWorldCore::kCurrentDataVersion, "data version mismatch");
    check(!core.is_legacy_save(), "current version should not be legacy");

    // 模拟旧存档
    core.set_data_version(UniverseWorldCore::kLegacyDataVersion);
    check(core.is_legacy_save(), "legacy version should be detected");

    core.set_data_version(UniverseWorldCore::kCurrentDataVersion);

    // 注册带 legacy dimension_id 的地表 Sector
    SectorDesc surface;
    surface.id = SectorId{100};
    surface.name = "planet_alpha_surface";
    surface.kind = SectorKind::PlanetSurface;
    surface.bounds = AABB64{GlobalBlockPos{-4096, -64, -4096},
                            GlobalBlockPos{4095, 255, 4095}};
    surface.allow_voxel_building = true;
    surface.legacy_storage_shard = make_storage_shard("planet_alpha");

    check(core.register_sector(surface), "register surface sector with legacy shard should succeed");

    // 通过旧 dimension_id 查找 Sector
    auto found = core.find_sector_by_dimension("planet_alpha");
    check(found.has_value() && found->value == 100,
          "find_sector_by_dimension should return sector 100");

    // 反查
    check(core.find_legacy_dimension_id(SectorId{100}) == "planet_alpha",
          "reverse legacy dimension lookup failed");

    // 注册天体
    CelestialBodyDesc body;
    body.id = "planet_alpha";
    body.name = "Planet Alpha";
    body.center = GlobalPos{0.0, 0.0, 0.0};
    body.radius = 4096.0;
    body.atmosphere_radius = 4200.0;
    body.surface_sector = SectorId{100};

    check(core.register_celestial_body(body), "register celestial body should succeed");

    const CelestialBodyDesc* body_found = core.find_celestial_body("planet_alpha");
    check(body_found != nullptr, "celestial body should be found");
    check(body_found->radius == 4096.0, "celestial body radius mismatch");
    check(body_found->surface_sector == SectorId{100}, "celestial body surface sector mismatch");

    // 坐标查询
    SectorQueryResult q = core.find_sector(GlobalBlockPos{0, 64, 0});
    check(q.is_buildable(), "core.find_sector should return buildable for surface center");
    check(q.sector == SectorId{100}, "core.find_sector sector mismatch");

    auto key = core.make_chunk_key(GlobalBlockPos{0, 64, 0});
    check(key.has_value(), "core.make_chunk_key should return value");
    check(key->sector == SectorId{100}, "core.make_chunk_key sector mismatch");

    // 诊断
    SectorRegistryDiagnostics diag = core.compute_diagnostics();
    check(!diag.has_buildable_overlap(), "core diagnostics should have no buildable overlap");
    check(diag.buildable_sectors == 1, "core should have 1 buildable sector");

    core.clear();
}

// ============================================================
// 9. Sector 模拟等级
// ============================================================

void test_simulation_levels() {
    std::cerr << "[U1] test_simulation_levels" << std::endl;

    SectorManager sm;

    SectorDesc desc;
    desc.id = SectorId{1};
    desc.name = "test_sector";
    desc.kind = SectorKind::PlanetSurface;
    desc.bounds = AABB64{GlobalBlockPos{0, 0, 0}, GlobalBlockPos{99, 99, 99}};
    desc.allow_voxel_building = true;
    desc.default_simulation = SimulationLevel::Passive;

    sm.register_sector_checked(desc);

    // 默认模拟等级
    check(sm.get_simulation_level(SectorId{1}) == SimulationLevel::Passive,
          "default simulation level should be Passive");

    // 更新模拟等级
    sm.set_simulation_level(SectorId{1}, SimulationLevel::Active);
    check(sm.get_simulation_level(SectorId{1}) == SimulationLevel::Active,
          "simulation level should be Active after set");

    // active_sectors 应包含该 Sector
    std::vector<SectorId> active = sm.active_sectors();
    check(active.size() == 1 && active[0] == SectorId{1},
          "active_sectors should contain sector 1");

    // 设为 Unloaded 后不应出现在 active_sectors
    sm.set_simulation_level(SectorId{1}, SimulationLevel::Unloaded);
    active = sm.active_sectors();
    check(active.empty(), "active_sectors should be empty after Unloaded");

    // 未注册 Sector 返回 Unloaded
    check(sm.get_simulation_level(SectorId{999}) == SimulationLevel::Unloaded,
          "unregistered sector should return Unloaded");
}

// ============================================================
// 10. Sector 边界归属（闭区间）
// ============================================================

void test_sector_boundary_ownership() {
    std::cerr << "[U1] test_sector_boundary_ownership" << std::endl;

    SectorManager sm;

    SectorDesc desc;
    desc.id = SectorId{1};
    desc.name = "boundary_test";
    desc.kind = SectorKind::PlanetSurface;
    desc.bounds = AABB64{GlobalBlockPos{0, 0, 0}, GlobalBlockPos{99, 99, 99}};
    desc.allow_voxel_building = true;

    sm.register_sector_checked(desc);

    // min 边界（包含）
    SectorQueryResult q_min = sm.find_sector(GlobalBlockPos{0, 0, 0});
    check(q_min.found() && q_min.sector == SectorId{1},
          "min boundary should belong to sector");

    // max 边界（包含）
    SectorQueryResult q_max = sm.find_sector(GlobalBlockPos{99, 99, 99});
    check(q_max.found() && q_max.sector == SectorId{1},
          "max boundary should belong to sector");

    // min 边界 -1（不包含）
    SectorQueryResult q_before = sm.find_sector(GlobalBlockPos{-1, 0, 0});
    check(!q_before.found(), "position before min boundary should not belong to sector");

    // max 边界 +1（不包含）
    SectorQueryResult q_after = sm.find_sector(GlobalBlockPos{100, 0, 0});
    check(!q_after.found(), "position after max boundary should not belong to sector");
}

} // namespace

int main() {
    std::cerr << "[U1Core] starting unified universe core tests" << std::endl;

    test_coordinate_floor_division();
    test_coordinate_round_trip();
    test_large_coordinates();
    test_sector_registration_and_ownership();
    test_buildable_sector_overlap_detection();
    test_non_buildable_overlap_allowed();
    test_storage_shard_migration();
    test_universe_world_core_integration();
    test_simulation_levels();
    test_sector_boundary_ownership();

    if (g_failures > 0) {
        std::cerr << "[U1Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U1 universe core tests passed: coordinate conversion, sector ownership, "
                 "overlap detection, storage shard migration, celestial bodies." << std::endl;
    return 0;
}
