// ============================================================
// u3_universe_navigation.cpp — U3 深空与连续航行测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U3）：
//   1. 飞行模式切换：LocalVoxelFlight ↔ Cruise ↔ LandingApproach。
//   2. 天体 LOD 选择：根据距离选择 LOD 0~4。
//   3. InterestManager 统一兴趣：Cruise 模式不加载真实 chunk，只同步天体 LOD。
//   4. 远处天体不得触发真实体素加载。
//   5. LandingApproach 模式预加载真实 chunk。

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/universe_world_core.hpp"
#include "universe/flight_state_tracker.hpp"
#include "universe/celestial_lod_system.hpp"
#include "universe/interest_manager.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U3 FAIL] " << message << std::endl;
    ++g_failures;
}

// 辅助：注册两颗星球的 Sector 和天体（模拟 U3 双星球场景）
void setup_two_planet_universe(UniverseWorldCore& core) {
    core.clear();
    core.set_seed(20260619);

    // 星球 A 地表
    SectorDesc surface_a;
    surface_a.id = SectorId{100};
    surface_a.name = "planet_alpha_surface";
    surface_a.kind = SectorKind::PlanetSurface;
    surface_a.bounds = AABB64{GlobalBlockPos{-4096, -64, -4096},
                              GlobalBlockPos{4095, 255, 4095}};
    surface_a.allow_voxel_building = true;
    surface_a.default_simulation = SimulationLevel::Active;
    core.register_sector(surface_a);

    // 星球 A 轨道
    SectorDesc orbit_a;
    orbit_a.id = SectorId{101};
    orbit_a.name = "planet_alpha_orbit";
    orbit_a.kind = SectorKind::PlanetOrbit;
    orbit_a.bounds = AABB64{GlobalBlockPos{-8192, 256, -8192},
                            GlobalBlockPos{8191, 16384, 8191}};
    orbit_a.allow_voxel_building = false;
    orbit_a.default_simulation = SimulationLevel::Passive;
    core.register_sector(orbit_a);

    // 深空区域（星球 A 到星球 B 之间）
    SectorDesc deep_space;
    deep_space.id = SectorId{200};
    deep_space.name = "deep_space_alpha_beta";
    deep_space.kind = SectorKind::DeepSpace;
    deep_space.bounds = AABB64{GlobalBlockPos{-100000, -100000, -100000},
                               GlobalBlockPos{100000, 100000, 100000}};
    deep_space.allow_voxel_building = false;
    deep_space.default_simulation = SimulationLevel::Passive;
    core.register_sector(deep_space);

    // 星球 B 地表
    SectorDesc surface_b;
    surface_b.id = SectorId{300};
    surface_b.name = "planet_beta_surface";
    surface_b.kind = SectorKind::PlanetSurface;
    surface_b.bounds = AABB64{GlobalBlockPos{120000 - 4096, -64, -4096},
                              GlobalBlockPos{120000 + 4095, 255, 4095}};
    surface_b.allow_voxel_building = true;
    surface_b.default_simulation = SimulationLevel::Active;
    core.register_sector(surface_b);

    // 星球 B 轨道（与星球 A 轨道结构对称，包围地表上方）
    SectorDesc orbit_b;
    orbit_b.id = SectorId{301};
    orbit_b.name = "planet_beta_orbit";
    orbit_b.kind = SectorKind::PlanetOrbit;
    orbit_b.bounds = AABB64{GlobalBlockPos{120000 - 8192, 256, -8192},
                            GlobalBlockPos{120000 + 8191, 16384, 8191}};
    orbit_b.allow_voxel_building = false;
    orbit_b.default_simulation = SimulationLevel::Passive;
    core.register_sector(orbit_b);

    // 星球 A 天体
    CelestialBodyDesc body_a;
    body_a.id = "planet_alpha";
    body_a.name = "Planet Alpha";
    body_a.center = GlobalPos{0.0, 0.0, 0.0};
    body_a.radius = 4096.0;
    body_a.atmosphere_radius = 4200.0;
    body_a.surface_sector = SectorId{100};
    body_a.orbit_sector = SectorId{101};
    core.register_celestial_body(body_a);

    // 星球 B 天体
    CelestialBodyDesc body_b;
    body_b.id = "planet_beta";
    body_b.name = "Planet Beta";
    body_b.center = GlobalPos{120000.0, 0.0, 0.0};
    body_b.radius = 4096.0;
    body_b.atmosphere_radius = 4200.0;
    body_b.surface_sector = SectorId{300};
    body_b.orbit_sector = SectorId{301};
    core.register_celestial_body(body_b);
}

// ============================================================
// 1. FlightStateTracker：模式切换
// ============================================================

void test_flight_mode_transitions() {
    std::cerr << "[U3] test_flight_mode_transitions" << std::endl;

    FlightStateTracker tracker;
    FlightModeConfig cfg;
    cfg.cruise_speed_threshold = 200.0;
    cfg.landing_approach_distance = 2000.0;
    cfg.local_flight_distance = 500.0;
    tracker.set_config(cfg);

    // 注册飞行主体，初始在地表低速
    FlightState state;
    state.mode = FlightMode::LocalVoxelFlight;
    state.pos = GlobalPos{0.5, 64.5, 0.5};
    state.velocity = GlobalPos{0.0, 0.0, 0.0};
    tracker.register_flyer(1, state);

    // 低速：保持 LocalVoxelFlight
    auto event1 = tracker.update_flyer(1, GlobalPos{0.5, 64.5, 0.5},
                                       GlobalPos{10.0, 0.0, 0.0});
    check(!event1.has_value(), "low speed should stay LocalVoxelFlight");
    check(tracker.get_flight_mode(1) == FlightMode::LocalVoxelFlight,
          "should be LocalVoxelFlight at low speed");

    // 高速：切换到 Cruise
    auto event2 = tracker.update_flyer(1, GlobalPos{0.5, 1000.5, 0.5},
                                       GlobalPos{300.0, 0.0, 0.0});
    check(event2.has_value(), "high speed should trigger mode change");
    check(event2->new_mode == FlightMode::Cruise, "should switch to Cruise");
    check(tracker.get_flight_mode(1) == FlightMode::Cruise,
          "should be Cruise at high speed");
    check(tracker.is_high_speed_mode(1), "Cruise should be high speed mode");
    check(!tracker.needs_real_voxels(1), "Cruise should not need real voxels");

    // 降速：回到 LocalVoxelFlight
    auto event3 = tracker.update_flyer(1, GlobalPos{0.5, 1000.5, 0.5},
                                       GlobalPos{50.0, 0.0, 0.0});
    check(event3.has_value(), "slowing down should trigger mode change");
    check(event3->new_mode == FlightMode::LocalVoxelFlight,
          "should switch back to LocalVoxelFlight");
    check(tracker.needs_real_voxels(1), "LocalVoxelFlight should need real voxels");
}

// ============================================================
// 2. FlightStateTracker：着陆接近
// ============================================================

void test_landing_approach() {
    std::cerr << "[U3] test_landing_approach" << std::endl;

    FlightStateTracker tracker;
    FlightModeConfig cfg;
    cfg.cruise_speed_threshold = 200.0;
    cfg.landing_approach_distance = 2000.0;
    cfg.local_flight_distance = 500.0;
    tracker.set_config(cfg);

    // 注册飞行主体，在 Cruise 模式
    FlightState state;
    state.mode = FlightMode::Cruise;
    state.pos = GlobalPos{50000.0, 0.0, 0.0};
    state.velocity = GlobalPos{500.0, 0.0, 0.0};
    tracker.register_flyer(1, state);

    // 设置着陆目标，距离表面 1500 格（在 landing_approach_distance 内）
    tracker.set_landing_target(1, "planet_beta", 1500.0);

    // 更新：应切换到 LandingApproach
    auto event = tracker.update_flyer(1, GlobalPos{50000.0, 0.0, 0.0},
                                      GlobalPos{500.0, 0.0, 0.0});
    check(event.has_value(), "approaching target should trigger mode change");
    check(event->new_mode == FlightMode::LandingApproach,
          "should switch to LandingApproach");
    check(tracker.needs_real_voxels(1),
          "LandingApproach should need real voxels for preload");

    // 继续接近，距离表面 300 格（在 local_flight_distance 内）
    tracker.set_landing_target(1, "planet_beta", 300.0);
    auto event2 = tracker.update_flyer(1, GlobalPos{60000.0, 0.0, 0.0},
                                       GlobalPos{100.0, 0.0, 0.0});
    check(event2.has_value(), "close enough should trigger mode change");
    check(event2->new_mode == FlightMode::LocalVoxelFlight,
          "should switch to LocalVoxelFlight when close enough");
}

// ============================================================
// 3. CelestialLodSystem：LOD 等级选择
// ============================================================

void test_celestial_lod_selection() {
    std::cerr << "[U3] test_celestial_lod_selection" << std::endl;

    CelestialLodSystem lod_sys;

    CelestialBodyDesc body;
    body.id = "planet_alpha";
    body.center = GlobalPos{0.0, 0.0, 0.0};
    body.radius = 4096.0;

    // LOD 0：地表附近（距离中心 < 0.4 * 4096 = 1638.4）
    LodLevel lod0 = lod_sys.choose_lod(GlobalPos{1000.0, 0.0, 0.0}, body);
    check(lod0 == LodLevel::Real, "close to surface should be LOD 0 (Real)");

    // LOD 1：中距离（0.4~0.8 * 4096 = 1638.4~3276.8）
    LodLevel lod1 = lod_sys.choose_lod(GlobalPos{2500.0, 0.0, 0.0}, body);
    check(lod1 == LodLevel::Simplified, "mid range should be LOD 1 (Simplified)");

    // LOD 2：远距离（0.8~3.0 * 4096 = 3276.8~12288）
    LodLevel lod2 = lod_sys.choose_lod(GlobalPos{8000.0, 0.0, 0.0}, body);
    check(lod2 == LodLevel::PlanetProxy, "far range should be LOD 2 (PlanetProxy)");

    // LOD 3：极远（3.0~8.0 * 4096 = 12288~32768）
    LodLevel lod3 = lod_sys.choose_lod(GlobalPos{20000.0, 0.0, 0.0}, body);
    check(lod3 == LodLevel::LowPoly, "very far should be LOD 3 (LowPoly)");

    // LOD 4：深空（8.0 * 4096 = 32768 以上）
    LodLevel lod4 = lod_sys.choose_lod(GlobalPos{100000.0, 0.0, 0.0}, body);
    check(lod4 == LodLevel::Billboard, "deep space should be LOD 4 (Billboard)");
}

// ============================================================
// 4. CelestialLodSystem：距离计算
// ============================================================

void test_celestial_distance() {
    std::cerr << "[U3] test_celestial_distance" << std::endl;

    CelestialBodyDesc body;
    body.id = "planet_alpha";
    body.center = GlobalPos{0.0, 0.0, 0.0};
    body.radius = 4096.0;

    // 玩家在天体表面
    GlobalPos surface_pos{4096.0, 0.0, 0.0};
    double center_dist = CelestialLodSystem::compute_center_distance(surface_pos, body);
    check(center_dist == 4096.0, "center distance should be 4096");

    double surface_dist = CelestialLodSystem::compute_surface_distance(surface_pos, body);
    check(surface_dist == 0.0, "surface distance should be 0 at surface");

    // 玩家在天体上方 1000 格
    GlobalPos above_pos{5096.0, 0.0, 0.0};
    double above_dist = CelestialLodSystem::compute_surface_distance(above_pos, body);
    check(above_dist == 1000.0, "surface distance should be 1000 above surface");

    // 玩家在天体内部
    GlobalPos inside_pos{1000.0, 0.0, 0.0};
    double inside_dist = CelestialLodSystem::compute_surface_distance(inside_pos, body);
    check(inside_dist < 0, "surface distance should be negative inside planet");
}

// ============================================================
// 5. CelestialLodSystem：LOD 距离阈值
// ============================================================

void test_lod_distances() {
    std::cerr << "[U3] test_lod_distances" << std::endl;

    CelestialLodSystem lod_sys;
    double radius = 4096.0;
    auto distances = lod_sys.compute_lod_distances(radius);

    check(distances.size() == 4, "should have 4 LOD distance thresholds");
    check(distances[0] == 0.4 * 4096.0, "LOD 0 max should be 0.4 * radius");
    check(distances[1] == 0.8 * 4096.0, "LOD 1 max should be 0.8 * radius");
    check(distances[2] == 3.0 * 4096.0, "LOD 2 max should be 3.0 * radius");
    check(distances[3] == 8.0 * 4096.0, "LOD 3 max should be 8.0 * radius");
}

// ============================================================
// 6. InterestManager：Cruise 模式不加载真实 chunk
// ============================================================

void test_interest_cruise_no_chunks() {
    std::cerr << "[U3] test_interest_cruise_no_chunks" << std::endl;

    UniverseWorldCore core;
    setup_two_planet_universe(core);

    InterestManager im;

    // 配置 chunk streaming
    StreamingConfig stream_cfg;
    stream_cfg.sphere_radius_chunks = 2;
    stream_cfg.max_chunk_requests_per_frame = 100;
    im.chunk_streaming().set_config(stream_cfg);

    // 配置飞行模式
    FlightModeConfig flight_cfg;
    flight_cfg.cruise_speed_threshold = 200.0;
    flight_cfg.landing_approach_distance = 2000.0;
    flight_cfg.local_flight_distance = 500.0;
    im.flight_tracker().set_config(flight_cfg);

    // 注册玩家在深空，高速飞行（Cruise 模式）
    im.register_player(1, GlobalPos{50000.0, 0.0, 0.0},
                       GlobalPos{500.0, 0.0, 0.0}, SectorId{200});

    // 更新位置触发 Cruise 模式
    im.update_player(1, GlobalPos{50000.0, 0.0, 0.0},
                     GlobalPos{500.0, 0.0, 0.0});

    InterestSet interest = im.compute_interest(1, core.sector_manager(), core);

    // Cruise 模式不应加载真实 chunk
    check(interest.flight_mode == FlightMode::Cruise,
          "should be in Cruise mode at high speed");
    check(!interest.needs_real_voxels, "Cruise should not need real voxels");
    check(interest.chunks.empty(), "Cruise should not load real chunks");

    // 但应有天体 LOD
    check(!interest.celestial_lods.empty(), "should have celestial LODs");
    check(interest.celestial_lods.size() == 2, "should have 2 celestial LODs");

    // 远处星球应是低 LOD
    bool has_billboard = false;
    for (const auto& lod : interest.celestial_lods) {
        if (lod.lod == LodLevel::Billboard || lod.lod == LodLevel::LowPoly) {
            has_billboard = true;
        }
    }
    check(has_billboard, "distant planets should have low LOD");
}

// ============================================================
// 7. InterestManager：LocalVoxelFlight 加载真实 chunk
// ============================================================

void test_interest_local_voxel_loads_chunks() {
    std::cerr << "[U3] test_interest_local_voxel_loads_chunks" << std::endl;

    UniverseWorldCore core;
    setup_two_planet_universe(core);

    InterestManager im;

    StreamingConfig stream_cfg;
    stream_cfg.sphere_radius_chunks = 2;
    stream_cfg.max_chunk_requests_per_frame = 100;
    im.chunk_streaming().set_config(stream_cfg);

    FlightModeConfig flight_cfg;
    flight_cfg.cruise_speed_threshold = 200.0;
    im.flight_tracker().set_config(flight_cfg);

    // 注册玩家在地表中心，低速
    im.register_player(1, GlobalPos{0.5, 64.5, 0.5},
                       GlobalPos{0.0, 0.0, 0.0}, SectorId{100});

    InterestSet interest = im.compute_interest(1, core.sector_manager(), core);

    // LocalVoxelFlight 应加载真实 chunk
    check(interest.flight_mode == FlightMode::LocalVoxelFlight,
          "should be in LocalVoxelFlight at low speed");
    check(interest.needs_real_voxels, "LocalVoxelFlight should need real voxels");
    check(!interest.chunks.empty(), "LocalVoxelFlight should load real chunks");

    // 所有 chunk 应属于地表 Sector
    for (const auto& key : interest.chunks) {
        check(key.sector == SectorId{100},
              "chunks should belong to surface sector");
    }
}

// ============================================================
// 8. InterestManager：LandingApproach 预加载
// ============================================================

void test_interest_landing_approach_preload() {
    std::cerr << "[U3] test_interest_landing_approach_preload" << std::endl;

    UniverseWorldCore core;
    setup_two_planet_universe(core);

    InterestManager im;

    StreamingConfig stream_cfg;
    stream_cfg.sphere_radius_chunks = 3;
    stream_cfg.max_chunk_requests_per_frame = 100;
    im.chunk_streaming().set_config(stream_cfg);

    FlightModeConfig flight_cfg;
    flight_cfg.cruise_speed_threshold = 200.0;
    flight_cfg.landing_approach_distance = 2000.0;
    flight_cfg.local_flight_distance = 500.0;
    im.flight_tracker().set_config(flight_cfg);

    // 注册玩家在星球 B 附近，高速但接近着陆目标
    im.register_player(1, GlobalPos{120000.0, 1500.0, 0.0},
                       GlobalPos{300.0, 0.0, 0.0}, SectorId{300});

    // 设置着陆目标，距离表面 1500 格
    im.set_landing_target(1, "planet_beta", 1500.0);

    // 更新位置触发 LandingApproach
    im.update_player(1, GlobalPos{120000.0, 1500.0, 0.0},
                     GlobalPos{300.0, 0.0, 0.0});

    InterestSet interest = im.compute_interest(1, core.sector_manager(), core);

    // LandingApproach 应加载真实 chunk（预加载）
    check(interest.flight_mode == FlightMode::LandingApproach,
          "should be in LandingApproach near target");
    check(interest.needs_real_voxels, "LandingApproach should need real voxels");
    check(!interest.chunks.empty(), "LandingApproach should preload real chunks");
}

// ============================================================
// 9. InterestManager：远处天体不触发真实体素
// ============================================================

void test_distant_celestial_no_voxels() {
    std::cerr << "[U3] test_distant_celestial_no_voxels" << std::endl;

    UniverseWorldCore core;
    setup_two_planet_universe(core);

    CelestialLodSystem lod_sys;

    // 玩家在星球 A 附近
    GlobalPos player_pos{1000.0, 0.0, 0.0};

    auto lods = lod_sys.compute_all_lods(player_pos, core);

    check(lods.size() == 2, "should have 2 celestial LODs");

    // 星球 A 应是高 LOD（近）
    // 星球 B 应是低 LOD（远，距离约 119000）
    for (const auto& lod : lods) {
        if (lod.celestial_id == "planet_alpha") {
            check(lod.lod == LodLevel::Real,
                  "planet A should be LOD 0 (Real) when close");
            check(CelestialLodSystem::needs_real_voxels(lod.lod),
                  "LOD 0 should need real voxels");
        }
        if (lod.celestial_id == "planet_beta") {
            check(lod.lod == LodLevel::Billboard,
                  "planet B should be LOD 4 (Billboard) when far");
            check(!CelestialLodSystem::needs_real_voxels(lod.lod),
                  "LOD 4 should not need real voxels");
        }
    }
}

// ============================================================
// 10. InterestManager：完整航行场景
// ============================================================

void test_full_navigation_scenario() {
    std::cerr << "[U3] test_full_navigation_scenario" << std::endl;

    UniverseWorldCore core;
    setup_two_planet_universe(core);

    InterestManager im;

    StreamingConfig stream_cfg;
    stream_cfg.sphere_radius_chunks = 2;
    stream_cfg.max_chunk_requests_per_frame = 100;
    im.chunk_streaming().set_config(stream_cfg);

    FlightModeConfig flight_cfg;
    flight_cfg.cruise_speed_threshold = 200.0;
    flight_cfg.landing_approach_distance = 2000.0;
    flight_cfg.local_flight_distance = 500.0;
    im.flight_tracker().set_config(flight_cfg);

    // 阶段 1：玩家在星球 A 地表，低速
    im.register_player(1, GlobalPos{0.5, 64.5, 0.5},
                       GlobalPos{0.0, 0.0, 0.0}, SectorId{100});
    InterestSet interest1 = im.compute_interest(1, core.sector_manager(), core);
    check(interest1.flight_mode == FlightMode::LocalVoxelFlight,
          "stage 1: should be LocalVoxelFlight on surface");
    check(!interest1.chunks.empty(), "stage 1: should load real chunks");

    // 阶段 2：玩家起飞，高速进入 Cruise
    im.update_player(1, GlobalPos{0.5, 5000.5, 0.5},
                     GlobalPos{0.0, 500.0, 0.0});
    InterestSet interest2 = im.compute_interest(1, core.sector_manager(), core);
    check(interest2.flight_mode == FlightMode::Cruise,
          "stage 2: should be Cruise at high speed");
    check(interest2.chunks.empty(), "stage 2: should not load real chunks in Cruise");

    // 阶段 3：玩家在深空巡航
    im.update_player(1, GlobalPos{60000.0, 0.0, 0.0},
                     GlobalPos{500.0, 0.0, 0.0});
    InterestSet interest3 = im.compute_interest(1, core.sector_manager(), core);
    check(interest3.flight_mode == FlightMode::Cruise,
          "stage 3: should be Cruise in deep space");
    check(interest3.chunks.empty(), "stage 3: should not load real chunks");

    // 阶段 4：接近星球 B，设置着陆目标
    im.set_landing_target(1, "planet_beta", 1500.0);
    im.update_player(1, GlobalPos{120000.0, 1500.0, 0.0},
                     GlobalPos{300.0, 0.0, 0.0});
    InterestSet interest4 = im.compute_interest(1, core.sector_manager(), core);
    check(interest4.flight_mode == FlightMode::LandingApproach,
          "stage 4: should be LandingApproach near target");
    check(!interest4.chunks.empty(), "stage 4: should preload real chunks");

    // 阶段 5：着陆完成，低速
    im.set_landing_target(1, "planet_beta", 100.0);
    im.update_player(1, GlobalPos{120000.0, 100.0, 0.0},
                     GlobalPos{10.0, 0.0, 0.0});
    InterestSet interest5 = im.compute_interest(1, core.sector_manager(), core);
    check(interest5.flight_mode == FlightMode::LocalVoxelFlight,
          "stage 5: should be LocalVoxelFlight after landing");
    check(!interest5.chunks.empty(), "stage 5: should load real chunks");
}

} // namespace

int main() {
    std::cerr << "[U3Core] starting universe navigation tests" << std::endl;

    test_flight_mode_transitions();
    test_landing_approach();
    test_celestial_lod_selection();
    test_celestial_distance();
    test_lod_distances();
    test_interest_cruise_no_chunks();
    test_interest_local_voxel_loads_chunks();
    test_interest_landing_approach_preload();
    test_distant_celestial_no_voxels();
    test_full_navigation_scenario();

    if (g_failures > 0) {
        std::cerr << "[U3Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U3 universe navigation tests passed: flight modes, celestial LOD, "
                 "interest management, full navigation scenario." << std::endl;
    return 0;
}
