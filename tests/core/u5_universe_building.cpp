// ============================================================
// u5_universe_building.cpp — U5 跨 Sector 建造与空间站迁移测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U5）：
//   1. BlockSpace 基于 GlobalBlockPos 提供统一方块访问，邻居查询跨 Sector 边界。
//   2. 方块放置/挖掘统一走 BlockSpace，带 dirty 标记。
//   3. 太空结构使用稀疏 chunk，空 chunk 可回收，结构锚点保护核心区域。
//   4. 空间站作为 SpaceStation Sector，可在宇宙坐标中直接接近和离开。
//   5. 跨 Sector 边界的测试桥可连续放置、拆除。
//   6. 星球局部建造方向可稳定量化到固定体素六邻格。

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/universe_world_core.hpp"
#include "universe/sparse_chunk_policy.hpp"
#include "universe/block_space.hpp"
#include "universe/planet_build_frame.hpp"
#include "universe/structure_anchor_manager.hpp"
#include "universe/space_station_sector.hpp"
#include "universe/sector_transition_manager.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U5 FAIL] " << message << std::endl;
    ++g_failures;
}

void test_planet_build_frame() {
    std::cerr << "[U5] test_planet_build_frame" << std::endl;

    const PlanetBuildFrame frame(0.0, -512.0, 0.0);

    const PlanetLocalBlockPos north_surface{0, 0, 0};
    check(frame.local_up(north_surface) == Direction::PosY,
          "north surface local up should resolve to +Y");
    check(frame.local_down(north_surface) == Direction::NegY,
          "north surface local down should resolve to -Y");
    check(frame.local_horizontal(north_surface, BuildVector{1.0, 1.0, 0.0}) ==
              Direction::PosX,
          "north surface horizontal should remove radial Y component");
    check(frame.classify(north_surface, Direction::PosY) ==
              LocalBuildDirection::Up,
          "+Y should classify as local up at north surface");
    check(frame.classify(north_surface, Direction::PosZ) ==
              LocalBuildDirection::Horizontal,
          "+Z should classify as horizontal at north surface");

    const PlanetLocalBlockPos east_surface{512, -512, 0};
    check(frame.local_up(east_surface) == Direction::PosX,
          "east surface local up should resolve to +X");
    check(frame.local_down(east_surface) == Direction::NegX,
          "east surface local down should resolve to -X");
    check(frame.local_horizontal(east_surface, BuildVector{1.0, 1.0, 0.0}) ==
              Direction::PosY,
          "east surface horizontal should remove radial X component");

    const Direction degenerate_horizontal =
        frame.local_horizontal(east_surface, BuildVector{1.0, 0.0, 0.0});
    check(degenerate_horizontal != Direction::PosX &&
              degenerate_horizontal != Direction::NegX &&
              PlanetBuildFrame::is_axis_direction(degenerate_horizontal),
          "degenerate tangent request should choose a valid horizontal axis");

    check(PlanetBuildFrame::snap_global_axis(BuildVector{-0.8, 0.1, 0.2}) ==
              Direction::NegX,
          "global axis snapping should select the strongest signed component");
}

// 辅助：注册两个相邻的可建造 Sector（用于跨边界测试）
// Sector A: x in [-32, 31], Sector B: x in [32, 95]
// 边界在 x=31/x=32
void setup_adjacent_sectors(UniverseWorldCore& core) {
    core.clear();
    core.set_seed(20260620);

    // Sector A（地表，可建造）
    SectorDesc sector_a;
    sector_a.id = SectorId{100};
    sector_a.name = "sector_a";
    sector_a.kind = SectorKind::PlanetSurface;
    sector_a.bounds = AABB64{GlobalBlockPos{-32, -32, -32},
                              GlobalBlockPos{31, 31, 31}};
    sector_a.allow_voxel_building = true;
    sector_a.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_a);

    // Sector B（地表，可建造，与 A 相邻）
    SectorDesc sector_b;
    sector_b.id = SectorId{101};
    sector_b.name = "sector_b";
    sector_b.kind = SectorKind::PlanetSurface;
    sector_b.bounds = AABB64{GlobalBlockPos{32, -32, -32},
                              GlobalBlockPos{95, 31, 31}};
    sector_b.allow_voxel_building = true;
    sector_b.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_b);

    // 深空 Sector（不可建造）
    SectorDesc deep_space;
    deep_space.id = SectorId{200};
    deep_space.name = "deep_space";
    deep_space.kind = SectorKind::DeepSpace;
    deep_space.bounds = AABB64{GlobalBlockPos{-10000, -10000, -10000},
                               GlobalBlockPos{10000, 10000, 10000}};
    deep_space.allow_voxel_building = false;
    deep_space.default_simulation = SimulationLevel::Passive;
    core.register_sector(deep_space);
}

// ============================================================
// 1. BlockSpace：基本方块放置与查询
// ============================================================

void test_block_space_basic() {
    std::cerr << "[U5] test_block_space_basic" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);

    // 在 Sector A 内放置方块
    GlobalBlockPos pos_a{0, 0, 0};
    check(block_space.can_build_at(pos_a), "should be able to build in sector A");
    check(block_space.set_block(pos_a, 1), "should set block in sector A");

    // 查询方块
    BlockQueryResult q = block_space.get_block(pos_a);
    check(q.found, "should find block at pos_a");
    check(q.block == 1, "block should be 1");
    check(q.sector == SectorId{100}, "block should be in sector A");

    // 未放置的位置应为空气
    GlobalBlockPos pos_empty{10, 10, 10};
    BlockQueryResult q_empty = block_space.get_block(pos_empty);
    check(!q_empty.found || q_empty.block == kBlockAir,
          "empty position should be air");

    // 不可建造区域不能放置
    GlobalBlockPos pos_deep{5000, 0, 0};
    check(!block_space.can_build_at(pos_deep),
          "should not be able to build in deep space");
    check(!block_space.set_block(pos_deep, 1),
          "should not set block in deep space");
}

// ============================================================
// 2. BlockSpace：跨 Sector 边界邻居查询
// ============================================================

void test_cross_sector_neighbor() {
    std::cerr << "[U5] test_cross_sector_neighbor" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);

    // 在边界两侧放置方块
    // Sector A 边界: x=31, Sector B 边界: x=32
    GlobalBlockPos pos_a{31, 0, 0};   // Sector A 边界
    GlobalBlockPos pos_b{32, 0, 0};   // Sector B 边界

    check(block_space.set_block(pos_a, 1), "should set block at sector A boundary");
    check(block_space.set_block(pos_b, 2), "should set block at sector B boundary");

    // 验证方块归属正确的 Sector
    BlockQueryResult q_a = block_space.get_block(pos_a);
    check(q_a.sector == SectorId{100}, "pos_a should be in sector A");
    check(q_a.block == 1, "pos_a block should be 1");

    BlockQueryResult q_b = block_space.get_block(pos_b);
    check(q_b.sector == SectorId{101}, "pos_b should be in sector B");
    check(q_b.block == 2, "pos_b block should be 2");

    // 邻居查询：从 Sector A 的边界方块查询 +X 方向邻居
    // 应该得到 Sector B 的方块
    BlockQueryResult neighbor = block_space.get_neighbor(pos_a, Direction::PosX);
    check(neighbor.found, "should find neighbor across boundary");
    check(neighbor.block == 2, "neighbor block should be 2");
    check(neighbor.sector == SectorId{101}, "neighbor should be in sector B");

    // 反向：从 Sector B 的边界方块查询 -X 方向邻居
    BlockQueryResult neighbor_back = block_space.get_neighbor(pos_b, Direction::NegX);
    check(neighbor_back.found, "should find neighbor back across boundary");
    check(neighbor_back.block == 1, "neighbor back block should be 1");
    check(neighbor_back.sector == SectorId{100}, "neighbor back should be in sector A");

    // 邻居位置计算
    GlobalBlockPos neighbor_pos = BlockSpace::neighbor_pos(pos_a, Direction::PosX);
    check(neighbor_pos.x == 32, "neighbor pos x should be 32");
    check(neighbor_pos == pos_b, "neighbor pos should equal pos_b");
}

// ============================================================
// 3. BlockSpace：跨 Sector 边界连续建造（测试桥）
// ============================================================

void test_cross_sector_bridge() {
    std::cerr << "[U5] test_cross_sector_bridge" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);

    // 在边界两侧建造一座桥（沿 X 轴连续放置方块）
    // Sector A: x=29,30,31, Sector B: x=32,33,34
    std::vector<GlobalBlockPos> bridge_blocks;
    for (int x = 29; x <= 34; ++x) {
        GlobalBlockPos pos{x, 0, 0};
        check(block_space.set_block(pos, 10), "should place bridge block");
        bridge_blocks.push_back(pos);
    }

    // 验证所有桥方块都存在
    for (const auto& pos : bridge_blocks) {
        BlockQueryResult q = block_space.get_block(pos);
        check(q.found, "bridge block should exist");
        check(q.block == 10, "bridge block should be 10");
    }

    // 验证桥跨越两个 Sector
    BlockQueryResult q_a = block_space.get_block(GlobalBlockPos{29, 0, 0});
    BlockQueryResult q_b = block_space.get_block(GlobalBlockPos{34, 0, 0});
    check(q_a.sector == SectorId{100}, "bridge start should be in sector A");
    check(q_b.sector == SectorId{101}, "bridge end should be in sector B");

    // 拆除桥
    for (const auto& pos : bridge_blocks) {
        check(block_space.dig_block(pos), "should dig bridge block");
    }

    // 验证桥已拆除
    for (const auto& pos : bridge_blocks) {
        BlockQueryResult q = block_space.get_block(pos);
        check(!q.found || q.block == kBlockAir, "bridge block should be removed");
    }
}

// ============================================================
// 4. BlockSpace：dirty 标记
// ============================================================

void test_dirty_tracking() {
    std::cerr << "[U5] test_dirty_tracking" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);

    // 初始无 dirty
    check(block_space.dirty_count() == 0, "initial dirty count should be 0");

    // 放置方块后应标记 dirty
    GlobalBlockPos pos{0, 0, 0};
    block_space.set_block(pos, 1);
    check(block_space.dirty_count() == 1, "should have 1 dirty chunk after set");

    // 获取 dirty chunks
    auto dirty = block_space.get_dirty_chunks();
    check(dirty.size() == 1, "should return 1 dirty chunk");
    check(dirty[0].sector == SectorId{100}, "dirty chunk should be in sector A");

    // 清除 dirty
    block_space.clear_dirty(dirty[0]);
    check(block_space.dirty_count() == 0, "dirty count should be 0 after clear");

    // 挖掘也标记 dirty
    block_space.dig_block(pos);
    check(block_space.dirty_count() == 1, "should have dirty after dig");
}

// ============================================================
// 5. StructureAnchorManager：锚点注册与保护
// ============================================================

void test_structure_anchors() {
    std::cerr << "[U5] test_structure_anchors" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);
    StructureAnchorManager anchor_mgr;

    // 在 Sector A 放置一些方块（创建有内容的 chunk）
    GlobalBlockPos pos{0, 0, 0};
    block_space.set_block(pos, 1);

    // 获取该方块所在的 chunk key
    auto dirty = block_space.get_dirty_chunks();
    check(dirty.size() == 1, "should have 1 dirty chunk");
    SectorChunkKey anchored_key = dirty[0];

    // 注册锚点保护该 chunk
    uint64_t anchor_id = anchor_mgr.register_anchor(
        "test_structure", "owner_1", {anchored_key});
    check(anchor_id != 0, "should register anchor with valid id");
    check(anchor_mgr.is_chunk_anchored(anchored_key),
          "chunk should be anchored");

    // 锚定的 chunk 不可回收
    check(!anchor_mgr.can_reclaim_chunk(anchored_key, block_space),
          "anchored chunk should not be reclaimable");

    // 注销锚点
    check(anchor_mgr.unregister_anchor(anchor_id), "should unregister anchor");
    check(!anchor_mgr.is_chunk_anchored(anchored_key),
          "chunk should not be anchored after unregister");
}

// ============================================================
// 6. StructureAnchorManager：空 chunk 回收
// ============================================================

void test_empty_chunk_reclaim() {
    std::cerr << "[U5] test_empty_chunk_reclaim" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);
    StructureAnchorManager anchor_mgr;

    // 放置然后挖掘方块（chunk 变空）
    GlobalBlockPos pos{0, 0, 0};
    block_space.set_block(pos, 1);
    check(block_space.chunk_count() == 1, "should have 1 chunk after set");

    block_space.dig_block(pos);
    // chunk 仍存在但为空
    check(block_space.chunk_count() == 1, "chunk should still exist after dig");

    // 空 chunk 可以回收（无锚点）
    auto dirty = block_space.get_dirty_chunks();
    SectorChunkKey empty_key = dirty[0];
    check(anchor_mgr.can_reclaim_chunk(empty_key, block_space),
          "empty unanchored chunk should be reclaimable");

    // 执行回收
    int reclaimed = anchor_mgr.reclaim_empty_chunks(block_space);
    check(reclaimed >= 1, "should reclaim at least 1 empty chunk");
    check(block_space.chunk_count() == 0, "should have 0 chunks after reclaim");
}

// ============================================================
// 7. StructureAnchorManager：建造预算
// ============================================================

void test_build_budget() {
    std::cerr << "[U5] test_build_budget" << std::endl;

    StructureAnchorManager anchor_mgr;

    BuildBudgetConfig cfg;
    cfg.max_chunks_per_operation = 3;
    cfg.max_chunks_per_sector = 10;
    cfg.max_total_chunks = 100;
    anchor_mgr.set_config(cfg);

    // 单次操作预算
    anchor_mgr.reset_operation_budget();
    check(anchor_mgr.can_create_chunks(2), "should allow 2 chunks in budget");
    anchor_mgr.record_chunk_creation(2);
    check(anchor_mgr.can_create_chunks(1), "should allow 1 more chunk");
    anchor_mgr.record_chunk_creation(1);
    check(!anchor_mgr.can_create_chunks(1), "should exceed operation budget");

    // 重置后可以继续
    anchor_mgr.reset_operation_budget();
    check(anchor_mgr.can_create_chunks(3), "should allow 3 chunks after reset");
}

// ============================================================
// 8. SpaceStationSector：空间站注册与查询
// ============================================================

void test_space_station_registration() {
    std::cerr << "[U5] test_space_station_registration" << std::endl;

    UniverseWorldCore core;
    core.clear();
    core.set_seed(20260620);

    SparseChunkPolicy sparse_policy;
    SpaceStationSectorManager station_mgr;

    // 注册一个前哨站
    SpaceStationDesc station;
    station.id = "station_alpha";
    station.name = "Station Alpha";
    station.type = StationType::Outpost;
    station.center = GlobalPos{50000.0, 0.0, 0.0};
    station.sector_id = SectorId{500};
    station.parent_celestial_id = "";
    station.seed = 12345;
    station.gravity_multiplier = 1.0;
    station.atmosphere_type = 2;

    // 设置核心大小
    int sx, sy, sz;
    SpaceStationSectorManager::get_core_size(StationType::Outpost, sx, sy, sz);
    station.core_size_x = sx;
    station.core_size_y = sy;
    station.core_size_z = sz;

    bool ok = station_mgr.register_station(station, core.sector_manager(), sparse_policy);
    check(ok, "should register station");

    // 验证空间站已注册
    const SpaceStationDesc* found = station_mgr.find_station("station_alpha");
    check(found != nullptr, "should find registered station");
    check(found->name == "Station Alpha", "station name should match");

    // 验证 Sector 已注册
    const SectorDesc* sector = core.sector_manager().get_sector_desc(SectorId{500});
    check(sector != nullptr, "station sector should be registered");
    check(sector->kind == SectorKind::SpaceStation, "sector kind should be SpaceStation");
    check(sector->allow_voxel_building, "station sector should allow building");

    // 验证通过 SectorId 查找
    const SpaceStationDesc* found_by_sector = station_mgr.find_station_by_sector(SectorId{500});
    check(found_by_sector != nullptr, "should find station by sector id");

    // 验证核心 chunk 已标记
    auto core_chunks = station_mgr.compute_core_chunks(*found);
    for (const auto& key : core_chunks) {
        check(sparse_policy.is_chunk_marked(key.sector, key.coord),
              "core chunk should be marked");
    }
}

// ============================================================
// 9. SpaceStationSector：空间站可建造与接近
// ============================================================

void test_space_station_building() {
    std::cerr << "[U5] test_space_station_building" << std::endl;

    UniverseWorldCore core;
    core.clear();
    core.set_seed(20260620);

    SparseChunkPolicy sparse_policy;
    SpaceStationSectorManager station_mgr;

    // 注册一个居住站
    SpaceStationDesc station;
    station.id = "station_habitat";
    station.name = "Habitat Station";
    station.type = StationType::Habitat;
    station.center = GlobalPos{60000.0, 0.0, 0.0};
    station.sector_id = SectorId{600};
    station.seed = 54321;

    int sx, sy, sz;
    SpaceStationSectorManager::get_core_size(StationType::Habitat, sx, sy, sz);
    station.core_size_x = sx;
    station.core_size_y = sy;
    station.core_size_z = sz;

    station_mgr.register_station(station, core.sector_manager(), sparse_policy);

    // 在空间站内建造方块
    BlockSpace block_space(core.sector_manager(), sparse_policy);

    GlobalBlockPos pos{60000, 0, 0};  // 空间站中心
    check(block_space.can_build_at(pos), "should be able to build in station");
    check(block_space.set_block(pos, 1), "should set block in station");

    BlockQueryResult q = block_space.get_block(pos);
    check(q.found, "should find block in station");
    check(q.sector == SectorId{600}, "block should be in station sector");
    check(q.is_buildable, "station sector should be buildable");

    // 空间站外（深空）不能建造
    // 需要注册一个深空 Sector 来测试
    SectorDesc deep_space;
    deep_space.id = SectorId{700};
    deep_space.name = "deep_space";
    deep_space.kind = SectorKind::DeepSpace;
    deep_space.bounds = AABB64{GlobalBlockPos{-100000, -100000, -100000},
                               GlobalBlockPos{100000, 100000, 100000}};
    deep_space.allow_voxel_building = false;
    core.register_sector(deep_space);

    GlobalBlockPos pos_outside{55000, 0, 0};  // 空间站外
    check(!block_space.can_build_at(pos_outside),
          "should not build outside station in deep space");
}

// ============================================================
// 10. SpaceStationSector：空间站接近与离开（不切换 dimension）
// ============================================================

void test_space_station_approach_leave() {
    std::cerr << "[U5] test_space_station_approach_leave" << std::endl;

    UniverseWorldCore core;
    core.clear();
    core.set_seed(20260620);

    SparseChunkPolicy sparse_policy;
    SpaceStationSectorManager station_mgr;

    // 注册深空 Sector
    SectorDesc deep_space;
    deep_space.id = SectorId{700};
    deep_space.name = "deep_space";
    deep_space.kind = SectorKind::DeepSpace;
    deep_space.bounds = AABB64{GlobalBlockPos{-100000, -100000, -100000},
                               GlobalBlockPos{100000, 100000, 100000}};
    deep_space.allow_voxel_building = false;
    deep_space.default_simulation = SimulationLevel::Passive;
    core.register_sector(deep_space);

    // 注册空间站
    SpaceStationDesc station;
    station.id = "station_outpost";
    station.name = "Outpost Station";
    station.type = StationType::Outpost;
    station.center = GlobalPos{50000.0, 0.0, 0.0};
    station.sector_id = SectorId{500};
    station.seed = 99999;

    int sx, sy, sz;
    SpaceStationSectorManager::get_core_size(StationType::Outpost, sx, sy, sz);
    station.core_size_x = sx;
    station.core_size_y = sy;
    station.core_size_z = sz;

    station_mgr.register_station(station, core.sector_manager(), sparse_policy);

    // 使用 SectorTransitionManager 跟踪玩家位置
    SectorTransitionManager stm;
    stm.register_player(1, SectorId{700});  // 初始在深空

    // 玩家在深空
    GlobalPos pos_deep{40000.0, 0.0, 0.0};
    auto event1 = stm.update_player_position(1, pos_deep, core.sector_manager());
    check(!event1.has_value() || event1->to_sector == SectorId{700},
          "should stay in deep space");
    check(stm.get_current_sector(1) == SectorId{700},
          "player should be in deep space sector");

    // 玩家接近空间站（进入空间站 Sector）
    GlobalPos pos_station{50000.0, 0.0, 0.0};
    auto event2 = stm.update_player_position(1, pos_station, core.sector_manager());
    check(event2.has_value(), "should trigger sector transition when approaching station");
    if (event2.has_value()) {
        check(event2->to_sector == SectorId{500},
              "should enter station sector");
        check(event2->to_kind == SectorKind::SpaceStation,
              "target kind should be SpaceStation");
    }
    check(stm.get_current_sector(1) == SectorId{500},
          "player should be in station sector");

    // 玩家离开空间站（回到深空）
    GlobalPos pos_leave{40000.0, 0.0, 0.0};
    auto event3 = stm.update_player_position(1, pos_leave, core.sector_manager());
    check(event3.has_value(), "should trigger sector transition when leaving station");
    if (event3.has_value()) {
        check(event3->from_sector == SectorId{500},
              "should leave station sector");
        check(event3->from_kind == SectorKind::SpaceStation,
              "source kind should be SpaceStation");
    }

    // 验证：不需要切换 dimension，所有 Sector 仍然可用
    check(core.sector_manager().sector_count() == 2,
          "should still have 2 sectors (no dimension switch)");
    check(core.sector_manager().get_sector_desc(SectorId{500}) != nullptr,
          "station sector should still be registered");
    check(core.sector_manager().get_sector_desc(SectorId{700}) != nullptr,
          "deep space sector should still be registered");
}

// ============================================================
// 11. SpaceStationSector：不同类型空间站的核心大小
// ============================================================

void test_station_types() {
    std::cerr << "[U5] test_station_types" << std::endl;

    int sx, sy, sz;

    SpaceStationSectorManager::get_core_size(StationType::Outpost, sx, sy, sz);
    check(sx == 1 && sy == 1 && sz == 1, "Outpost should be 1x1x1");

    SpaceStationSectorManager::get_core_size(StationType::Habitat, sx, sy, sz);
    check(sx == 3 && sy == 1 && sz == 3, "Habitat should be 3x1x3");

    SpaceStationSectorManager::get_core_size(StationType::Factory, sx, sy, sz);
    check(sx == 5 && sy == 1 && sz == 5, "Factory should be 5x1x5");

    // 验证 bounds 计算
    AABB64 bounds = SpaceStationSectorManager::compute_bounds(
        GlobalPos{0.0, 0.0, 0.0}, 3, 1, 3);
    check(bounds.is_valid(), "bounds should be valid");
    check(bounds.contains(GlobalBlockPos{0, 0, 0}),
          "bounds should contain center");
}

// ============================================================
// 12. 完整场景：跨 Sector 建造桥 + 锚点保护 + 回收
// ============================================================

void test_full_building_scenario() {
    std::cerr << "[U5] test_full_building_scenario" << std::endl;

    UniverseWorldCore core;
    setup_adjacent_sectors(core);

    SparseChunkPolicy sparse_policy;
    BlockSpace block_space(core.sector_manager(), sparse_policy);
    StructureAnchorManager anchor_mgr;

    // 阶段 1：在 Sector A 建造桥墩（锚点）
    GlobalBlockPos pylon_a{29, 0, 0};
    block_space.set_block(pylon_a, 1);

    auto dirty = block_space.get_dirty_chunks();
    SectorChunkKey pylon_chunk = dirty[0];

    // 注册锚点保护桥墩
    uint64_t anchor = anchor_mgr.register_anchor(
        "bridge_pylon_a", "bridge_project", {pylon_chunk});
    check(anchor != 0, "should register pylon anchor");

    // 阶段 2：建造跨 Sector 桥（从 pylon 旁开始，不覆盖 pylon）
    for (int x = 30; x <= 34; ++x) {
        block_space.set_block(GlobalBlockPos{x, 0, 0}, 10);
    }
    check(block_space.chunk_count() >= 1, "should have chunks after building bridge");

    // 阶段 3：拆除桥（但保留桥墩）
    for (int x = 30; x <= 34; ++x) {
        block_space.dig_block(GlobalBlockPos{x, 0, 0});
    }
    // 桥墩仍在
    BlockQueryResult pylon_q = block_space.get_block(pylon_a);
    check(pylon_q.found && pylon_q.block == 1, "pylon should remain");

    // 阶段 4：尝试回收空 chunk（桥墩 chunk 不应被回收）
    int reclaimed = anchor_mgr.reclaim_empty_chunks(block_space);

    // 桥墩 chunk 仍存在（被锚定）
    check(block_space.has_chunk(pylon_chunk), "anchored pylon chunk should remain");
    BlockQueryResult pylon_q2 = block_space.get_block(pylon_a);
    check(pylon_q2.found && pylon_q2.block == 1,
          "pylon block should still exist after reclaim");

    // 阶段 5：注销锚点后可以回收
    anchor_mgr.unregister_anchor(anchor);
    // 挖掉桥墩
    block_space.dig_block(pylon_a);
    int reclaimed2 = anchor_mgr.reclaim_empty_chunks(block_space);
    check(reclaimed2 >= 1, "should reclaim pylon chunk after unanchor");
    check(!block_space.has_chunk(pylon_chunk),
          "pylon chunk should be reclaimed after unanchor and dig");
}

} // namespace

int main() {
    std::cerr << "[U5Core] starting universe building tests" << std::endl;

    test_planet_build_frame();
    test_block_space_basic();
    test_cross_sector_neighbor();
    test_cross_sector_bridge();
    test_dirty_tracking();
    test_structure_anchors();
    test_empty_chunk_reclaim();
    test_build_budget();
    test_space_station_registration();
    test_space_station_building();
    test_space_station_approach_leave();
    test_station_types();
    test_full_building_scenario();

    if (g_failures > 0) {
        std::cerr << "[U5Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U5 universe building tests passed: BlockSpace, cross-sector building, "
                 "structure anchors, space station sectors." << std::endl;
    return 0;
}
