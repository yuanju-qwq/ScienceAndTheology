// ============================================================
// u2_universe_streaming.cpp — U2 Sector 感知流式加载测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U2）：
//   1. chunk streaming 消费玩家附近的 Sector/Chunk 集合，不依赖 active_dimension。
//   2. 轨道使用稀疏 chunk：纯空气区域不生成。
//   3. 实体跨 Sector 迁移被正确检测。
//   4. floating origin 运行时重基准阈值检测。
//   5. 边界两侧方块查询一致。

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/chunk_streaming_system.hpp"
#include "universe/sparse_chunk_policy.hpp"
#include "universe/entity_sector_tracker.hpp"
#include "universe/floating_origin_core.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U2 FAIL] " << message << std::endl;
    ++g_failures;
}

// 辅助：注册一个地表 Sector 和一个轨道 Sector（模拟单星球 U2 场景）
void setup_single_planet_sectors(SectorManager& sm) {
    SectorDesc surface;
    surface.id = SectorId{100};
    surface.name = "planet_alpha_surface";
    surface.kind = SectorKind::PlanetSurface;
    surface.bounds = AABB64{GlobalBlockPos{-4096, -64, -4096},
                            GlobalBlockPos{4095, 255, 4095}};
    surface.allow_voxel_building = true;
    surface.default_simulation = SimulationLevel::Active;
    sm.register_sector_checked(surface);

    SectorDesc orbit;
    orbit.id = SectorId{101};
    orbit.name = "planet_alpha_orbit";
    orbit.kind = SectorKind::PlanetOrbit;
    orbit.bounds = AABB64{GlobalBlockPos{-8192, 256, -8192},
                          GlobalBlockPos{8191, 16384, 8191}};
    orbit.allow_voxel_building = false;
    orbit.default_simulation = SimulationLevel::Passive;
    sm.register_sector_checked(orbit);
}

// ============================================================
// 1. ChunkStreamingSystem：球形 AOI
// ============================================================

void test_sphere_aoi() {
    std::cerr << "[U2] test_sphere_aoi" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    ChunkStreamingSystem css;
    StreamingConfig cfg;
    cfg.sphere_radius_chunks = 2;
    cfg.max_chunk_requests_per_frame = 100;  // 不限制
    css.set_config(cfg);

    // 玩家在地表中心
    PlayerStreamingState state;
    state.pos = GlobalPos{0.5, 64.5, 0.5};
    state.velocity = GlobalPos{0.0, 0.0, 0.0};
    state.current_sector = SectorId{100};
    css.register_player(1, state);

    StreamingUpdateResult result = css.update_player(1, sm);

    // 应该有 chunk 需要加载（球形半径 2）
    check(!result.chunks_to_load.empty(), "sphere AOI should produce chunks to load");
    check(result.chunks_to_unload.empty(), "first update should have no chunks to unload");

    // 所有加载的 chunk 应属于地表 Sector
    for (const auto& key : result.chunks_to_load) {
        check(key.sector == SectorId{100},
              "sphere AOI chunks should belong to surface sector");
    }

    // 验证已加载数量
    size_t loaded_count = css.get_loaded_chunk_count(1);
    check(loaded_count == result.chunks_to_load.size(),
          "loaded count should match chunks_to_load size");

    // 再次更新：不应有新的加载（已全部加载）
    StreamingUpdateResult result2 = css.update_player(1, sm);
    check(result2.chunks_to_load.empty(), "second update should have no new chunks to load");
}

// ============================================================
// 2. ChunkStreamingSystem：chunk 预算
// ============================================================

void test_chunk_budget() {
    std::cerr << "[U2] test_chunk_budget" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    ChunkStreamingSystem css;
    StreamingConfig cfg;
    cfg.sphere_radius_chunks = 3;
    cfg.max_chunk_requests_per_frame = 2;  // 每帧只加载 2 个
    css.set_config(cfg);

    PlayerStreamingState state;
    state.pos = GlobalPos{0.5, 64.5, 0.5};
    state.current_sector = SectorId{100};
    css.register_player(1, state);

    StreamingUpdateResult result = css.update_player(1, sm);

    check(static_cast<int>(result.chunks_to_load.size()) <= 2,
          "chunks_to_load should be limited by budget");
    check(result.deferred_count > 0, "should have deferred chunks");

    // 第二次更新：继续加载剩余的
    StreamingUpdateResult result2 = css.update_player(1, sm);
    check(static_cast<int>(result2.chunks_to_load.size()) <= 2,
          "second update should also respect budget");
}

// ============================================================
// 3. ChunkStreamingSystem：卸载远距离 chunk
// ============================================================

void test_chunk_unload() {
    std::cerr << "[U2] test_chunk_unload" << std::endl;

    SectorManager sm;
    // 注册一个大的地表 Sector
    SectorDesc surface;
    surface.id = SectorId{1};
    surface.name = "large_surface";
    surface.kind = SectorKind::PlanetSurface;
    surface.bounds = AABB64{GlobalBlockPos{-100000, -64, -100000},
                            GlobalBlockPos{100000, 255, 100000}};
    surface.allow_voxel_building = true;
    sm.register_sector_checked(surface);

    ChunkStreamingSystem css;
    StreamingConfig cfg;
    cfg.sphere_radius_chunks = 2;
    cfg.unload_distance_multiplier = 1.5;
    cfg.max_chunk_requests_per_frame = 1000;
    css.set_config(cfg);

    // 玩家在原点
    PlayerStreamingState state;
    state.pos = GlobalPos{0.5, 64.5, 0.5};
    state.current_sector = SectorId{1};
    css.register_player(1, state);

    css.update_player(1, sm);
    size_t initial_count = css.get_loaded_chunk_count(1);
    check(initial_count > 0, "should have loaded chunks at origin");

    // 玩家移动到很远的位置
    css.update_player_position(1, GlobalPos{50000.5, 64.5, 50000.5}, GlobalPos{0, 0, 0});
    StreamingUpdateResult result = css.update_player(1, sm);

    // 应该有卸载（旧 chunk 超出卸载距离）
    check(!result.chunks_to_unload.empty(), "should unload far chunks");
    // 应该有新加载（新位置的 chunk）
    check(!result.chunks_to_load.empty(), "should load new chunks at new position");
}

// ============================================================
// 4. ChunkStreamingSystem：跨 Sector 边界
// ============================================================

void test_cross_sector_streaming() {
    std::cerr << "[U2] test_cross_sector_streaming" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    ChunkStreamingSystem css;
    StreamingConfig cfg;
    cfg.sphere_radius_chunks = 2;
    cfg.max_chunk_requests_per_frame = 1000;
    css.set_config(cfg);

    // 玩家在地表与轨道边界附近（y=255 是地表最高，y=256 是轨道最低）
    PlayerStreamingState state;
    state.pos = GlobalPos{0.5, 255.5, 0.5};  // 地表边界
    state.current_sector = SectorId{100};
    css.register_player(1, state);

    StreamingUpdateResult result = css.update_player(1, sm);

    // 应该涉及地表 Sector
    bool has_surface = false;
    for (const auto& s : result.touched_sectors) {
        if (s == SectorId{100}) has_surface = true;
    }
    check(has_surface, "should touch surface sector at boundary");

    // 玩家移动到轨道
    css.update_player_position(1, GlobalPos{0.5, 500.5, 0.5}, GlobalPos{0, 0, 0});
    StreamingUpdateResult result2 = css.update_player(1, sm);

    // 应该涉及轨道 Sector
    bool has_orbit = false;
    for (const auto& s : result2.touched_sectors) {
        if (s == SectorId{101}) has_orbit = true;
    }
    check(has_orbit, "should touch orbit sector after moving up");

    // 新加载的 chunk 应属于轨道 Sector
    bool has_orbit_chunk = false;
    for (const auto& key : result2.chunks_to_load) {
        if (key.sector == SectorId{101}) has_orbit_chunk = true;
    }
    check(has_orbit_chunk, "should load orbit sector chunks");
}

// ============================================================
// 5. SparseChunkPolicy：地表总是生成，轨道稀疏
// ============================================================

void test_sparse_chunk_policy() {
    std::cerr << "[U2] test_sparse_chunk_policy" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    SparseChunkPolicy policy;

    const SectorDesc* surface_desc = sm.get_sector_desc(SectorId{100});
    const SectorDesc* orbit_desc = sm.get_sector_desc(SectorId{101});
    check(surface_desc != nullptr, "surface desc should exist");
    check(orbit_desc != nullptr, "orbit desc should exist");

    ChunkCoord cc{0, 0, 0};

    // 地表：总是生成
    check(policy.should_generate_chunk(*surface_desc, cc),
          "surface chunk should always generate");

    // 轨道：默认不生成
    check(!policy.should_generate_chunk(*orbit_desc, cc),
          "orbit chunk should not generate by default (sparse)");

    // 标记轨道 chunk 有内容后应生成
    policy.mark_chunk_has_content(SectorId{101}, cc);
    check(policy.should_generate_chunk(*orbit_desc, cc),
          "orbit chunk should generate after marking has content");

    // 取消标记后不应生成
    policy.unmark_chunk_has_content(SectorId{101}, cc);
    check(!policy.should_generate_chunk(*orbit_desc, cc),
          "orbit chunk should not generate after unmarking");

    check(policy.marked_count() == 0, "marked count should be 0 after unmark");
}

// ============================================================
// 6. EntitySectorTracker：跨 Sector 迁移检测
// ============================================================

void test_entity_sector_migration() {
    std::cerr << "[U2] test_entity_sector_migration" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    EntitySectorTracker tracker;

    // 注册实体在地表
    EntityTrackingId entity{42};
    GlobalPos surface_pos{0.5, 64.5, 0.5};
    tracker.register_entity(entity, surface_pos, sm);

    check(tracker.get_entity_sector(entity) == SectorId{100},
          "entity should be in surface sector");

    // 更新位置但仍在地表：不应迁移
    auto event1 = tracker.update_entity_position(
        entity, GlobalPos{10.5, 100.5, 10.5}, sm);
    check(!event1.has_value(), "should not migrate when staying in surface");
    check(tracker.get_entity_sector(entity) == SectorId{100},
          "entity should still be in surface sector");

    // 移动到轨道：应触发迁移
    GlobalPos orbit_pos{0.5, 500.5, 0.5};
    auto event2 = tracker.update_entity_position(entity, orbit_pos, sm);
    check(event2.has_value(), "should migrate when moving to orbit");
    check(event2->from_sector == SectorId{100}, "migration from should be surface");
    check(event2->to_sector == SectorId{101}, "migration to should be orbit");
    check(tracker.get_entity_sector(entity) == SectorId{101},
          "entity should now be in orbit sector");

    // 移回地表：应触发迁移
    auto event3 = tracker.update_entity_position(entity, surface_pos, sm);
    check(event3.has_value(), "should migrate when moving back to surface");
    check(event3->from_sector == SectorId{101}, "migration from should be orbit");
    check(event3->to_sector == SectorId{100}, "migration to should be surface");

    // 查询 Sector 内实体
    auto surface_entities = tracker.get_entities_in_sector(SectorId{100});
    check(!surface_entities.empty(), "surface sector should have entities");
    check(surface_entities[0].value == 42, "entity id should be 42");

    auto orbit_entities = tracker.get_entities_in_sector(SectorId{101});
    check(orbit_entities.empty(), "orbit sector should have no entities after moving back");
}

// ============================================================
// 7. EntitySectorTracker：批量更新
// ============================================================

void test_entity_batch_update() {
    std::cerr << "[U2] test_entity_batch_update" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    EntitySectorTracker tracker;

    // 注册多个实体
    EntityTrackingId e1{1}, e2{2}, e3{3};
    tracker.register_entity(e1, GlobalPos{0.5, 64.5, 0.5}, sm);
    tracker.register_entity(e2, GlobalPos{0.5, 64.5, 0.5}, sm);
    tracker.register_entity(e3, GlobalPos{0.5, 64.5, 0.5}, sm);

    // 批量更新：e2 移动到轨道
    std::vector<std::pair<EntityTrackingId, GlobalPos>> updates = {
        {e1, GlobalPos{0.5, 64.5, 0.5}},   // 不变
        {e2, GlobalPos{0.5, 500.5, 0.5}},  // 迁移到轨道
        {e3, GlobalPos{0.5, 64.5, 0.5}},   // 不变
    };

    auto events = tracker.update_positions(updates, sm);
    check(events.size() == 1, "should have 1 migration event");
    check(events[0].entity.value == 2, "migrated entity should be e2");

    check(tracker.entity_count() == 3, "should have 3 entities");
}

// ============================================================
// 8. FloatingOriginCore：重基准阈值检测
// ============================================================

void test_floating_origin_rebase() {
    std::cerr << "[U2] test_floating_origin_rebase" << std::endl;

    FloatingOriginCore fo;
    FloatingOriginConfig cfg;
    cfg.rebase_threshold = 100.0;  // 小阈值便于测试
    fo.set_config(cfg);

    // 初始原点在 (0,0,0)
    check(fo.origin() == GlobalPos{0.0, 0.0, 0.0}, "initial origin should be zero");

    // 玩家在原点附近：不触发重基准
    auto event1 = fo.set_player_position(GlobalPos{50.0, 0.0, 0.0});
    check(!event1.has_value(), "should not rebase within threshold");
    check(fo.player_render_distance() == 50.0, "render distance should be 50");

    // 玩家移动到阈值外：触发重基准
    auto event2 = fo.set_player_position(GlobalPos{200.0, 0.0, 0.0});
    check(event2.has_value(), "should rebase when exceeding threshold");
    check(event2->old_origin == GlobalPos{0.0, 0.0, 0.0}, "old origin should be zero");
    check(event2->new_origin == GlobalPos{200.0, 0.0, 0.0}, "new origin should be player pos");

    // 重基准后玩家渲染坐标应接近 0
    check(fo.player_render_distance() == 0.0, "render distance should be 0 after rebase");
    check(fo.origin() == GlobalPos{200.0, 0.0, 0.0}, "origin should be updated");

    // 坐标转换
    GlobalPos universe_pos{250.0, 0.0, 0.0};
    GlobalPos render_pos = fo.universe_to_render(universe_pos);
    check(render_pos.x == 50.0, "universe_to_render should subtract origin");

    GlobalPos back = fo.render_to_universe(render_pos);
    check(back == universe_pos, "render_to_universe should be inverse");
}

// ============================================================
// 9. FloatingOriginCore：方块坐标转换
// ============================================================

void test_floating_origin_block_coords() {
    std::cerr << "[U2] test_floating_origin_block_coords" << std::endl;

    FloatingOriginCore fo;
    fo.set_origin(GlobalPos{1000.5, 2000.5, 3000.5});

    // 全局方块坐标 → 渲染方块坐标
    GlobalBlockPos global_block{1020, 2020, 3020};
    GlobalBlockPos render_block = fo.universe_block_to_render(global_block);

    // 渲染原点取整：floor(1000.5)=1000, floor(2000.5)=2000, floor(3000.5)=3000
    check(render_block.x == 20, "render block x should be 1020-1000=20");
    check(render_block.y == 20, "render block y should be 2020-2000=20");
    check(render_block.z == 20, "render block z should be 3020-3000=20");
}

// ============================================================
// 10. 边界两侧方块查询一致
// ============================================================

void test_boundary_consistency() {
    std::cerr << "[U2] test_boundary_consistency" << std::endl;

    SectorManager sm;
    setup_single_planet_sectors(sm);

    // 地表最高 y=255，轨道最低 y=256
    // 边界两侧的方块查询应归属不同 Sector，但都应返回明确结果
    SectorQueryResult q_surface = sm.find_sector(GlobalBlockPos{0, 255, 0});
    SectorQueryResult q_orbit = sm.find_sector(GlobalBlockPos{0, 256, 0});

    check(q_surface.found() && q_surface.sector == SectorId{100},
          "y=255 should belong to surface sector");
    check(q_orbit.found() && q_orbit.sector == SectorId{101},
          "y=256 should belong to orbit sector");

    // chunk 坐标转换在边界两侧应一致（同一 chunk 可能跨边界）
    auto key_surface = sm.make_chunk_key(GlobalBlockPos{0, 255, 0});
    auto key_orbit = sm.make_chunk_key(GlobalBlockPos{0, 256, 0});

    // 注意：y=255 和 y=256 可能在同一 chunk（chunk size=32，y=255 在 chunk 7，y=256 在 chunk 8）
    // 但它们归属不同 Sector，所以 SectorChunkKey 不同
    check(key_surface.has_value(), "surface boundary should have chunk key");
    check(key_orbit.has_value(), "orbit boundary should have chunk key");
    check(key_surface->sector != key_orbit->sector,
          "boundary chunks should belong to different sectors");

    // 实体在边界附近移动应正确检测迁移
    EntitySectorTracker tracker;
    EntityTrackingId entity{1};
    tracker.register_entity(entity, GlobalPos{0.5, 255.5, 0.5}, sm);
    check(tracker.get_entity_sector(entity) == SectorId{100},
          "entity at y=255 should be in surface");

    auto event = tracker.update_entity_position(
        entity, GlobalPos{0.5, 256.5, 0.5}, sm);
    check(event.has_value(), "crossing boundary should trigger migration");
    check(event->from_sector == SectorId{100}, "migration from surface");
    check(event->to_sector == SectorId{101}, "migration to orbit");
}

} // namespace

int main() {
    std::cerr << "[U2Core] starting universe streaming tests" << std::endl;

    test_sphere_aoi();
    test_chunk_budget();
    test_chunk_unload();
    test_cross_sector_streaming();
    test_sparse_chunk_policy();
    test_entity_sector_migration();
    test_entity_batch_update();
    test_floating_origin_rebase();
    test_floating_origin_block_coords();
    test_boundary_consistency();

    if (g_failures > 0) {
        std::cerr << "[U2Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U2 universe streaming tests passed: chunk streaming, sparse policy, "
                 "entity migration, floating origin, boundary consistency." << std::endl;
    return 0;
}
