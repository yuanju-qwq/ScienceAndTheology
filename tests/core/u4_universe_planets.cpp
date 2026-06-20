// ============================================================
// u4_universe_planets.cpp — U4 第二颗真实星球与连续着陆测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U4）：
//   1. 两颗星球各有独立的环境配置（重力、大气、地形种子）。
//   2. 重力场管理：多重力场重叠时通过滞后机制选择主导场，避免方向瞬跳。
//   3. LOD 加载链路：Billboard -> Proxy -> Approach -> Simplified -> Real。
//   4. Sector 转换：跨 Sector 边界时更新玩家状态，不切换 active_dimension。
//   5. 连续旅行：从星球 A 地表起飞，经轨道和 Deep Space 到星球 B 着陆，路径连续。

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
#include "universe/planet_environment.hpp"
#include "universe/gravity_field_manager.hpp"
#include "universe/celestial_lod_pipeline.hpp"
#include "universe/sector_transition_manager.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U4 FAIL] " << message << std::endl;
    ++g_failures;
}

// 辅助：注册两颗星球的 Sector、天体和环境（U4 双星球场景）
// 与 U3 的 setup_two_planet_universe 类似，但增加环境配置和重力场
void setup_two_planet_universe_with_env(UniverseWorldCore& core,
                                         GravityFieldManager& gravity) {
    core.clear();
    gravity.clear();
    core.set_seed(20260620);

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

    // 深空区域
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

    // 星球 B 轨道
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

    // 星球 A 环境配置（标准地球类）
    PlanetEnvironment env_a;
    env_a.celestial_id = "planet_alpha";
    env_a.surface_gravity = 9.8;
    env_a.gravity_influence_radius = 8192.0;
    env_a.gravity_falloff = GravityFalloff::Linear;
    env_a.atmosphere_density = 1.0;
    env_a.atmosphere_height = 1000.0;
    env_a.terrain_seed = 1001;
    env_a.biome_seed = 1002;
    env_a.resource_seed = 1003;
    env_a.day_length_seconds = 1200.0;
    core.register_planet_environment(env_a);
    gravity.register_from_environment("planet_alpha", body_a.center,
                                       body_a.radius, env_a);

    // 星球 B 环境配置（低重力、稀薄大气，资源差异）
    PlanetEnvironment env_b;
    env_b.celestial_id = "planet_beta";
    env_b.surface_gravity = 3.7;  // 火星类低重力
    env_b.gravity_influence_radius = 8192.0;
    env_b.gravity_falloff = GravityFalloff::Linear;
    env_b.atmosphere_density = 0.2;
    env_b.atmosphere_height = 500.0;
    env_b.terrain_seed = 2001;
    env_b.biome_seed = 2002;
    env_b.resource_seed = 2003;
    env_b.day_length_seconds = 2400.0;
    core.register_planet_environment(env_b);
    gravity.register_from_environment("planet_beta", body_b.center,
                                       body_b.radius, env_b);
}

// ============================================================
// 1. PlanetEnvironment：环境配置注册与查询
// ============================================================

void test_planet_environment_registration() {
    std::cerr << "[U4] test_planet_environment_registration" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    // 两颗星球都应有环境配置
    const PlanetEnvironment* env_a = core.find_planet_environment("planet_alpha");
    check(env_a != nullptr, "planet_alpha environment should exist");
    check(env_a->surface_gravity == 9.8, "planet_alpha gravity should be 9.8");
    check(env_a->atmosphere_density == 1.0, "planet_alpha atmosphere should be 1.0");
    check(env_a->terrain_seed == 1001, "planet_alpha terrain seed should be 1001");

    const PlanetEnvironment* env_b = core.find_planet_environment("planet_beta");
    check(env_b != nullptr, "planet_beta environment should exist");
    check(env_b->surface_gravity == 3.7, "planet_beta gravity should be 3.7 (low)");
    check(env_b->atmosphere_density == 0.2, "planet_beta atmosphere should be 0.2 (thin)");
    check(env_b->terrain_seed == 2001, "planet_beta terrain seed should be 2001");

    // 两颗星球环境应有差异
    check(env_a->surface_gravity != env_b->surface_gravity,
          "two planets should have different gravity");
    check(env_a->terrain_seed != env_b->terrain_seed,
          "two planets should have different terrain seeds");

    // 所有环境数量
    auto all_envs = core.all_planet_environments();
    check(all_envs.size() == 2, "should have 2 planet environments");

    // 不存在的天体
    check(core.find_planet_environment("nonexistent") == nullptr,
          "nonexistent planet should not have environment");
}

// ============================================================
// 2. GravityFieldManager：单重力场计算
// ============================================================

void test_single_gravity_field() {
    std::cerr << "[U4] test_single_gravity_field" << std::endl;

    GravityFieldManager gravity;

    GravityField field;
    field.celestial_id = "test_planet";
    field.center = GlobalPos{0.0, 0.0, 0.0};
    field.radius = 1000.0;
    field.surface_gravity = 10.0;
    field.influence_radius = 3000.0;
    field.falloff = GravityFalloff::Linear;
    gravity.register_field(field);

    // 在表面：重力应为 surface_gravity
    GravityQueryResult surface = gravity.compute_gravity(1, GlobalPos{1000.0, 0.0, 0.0});
    check(surface.has_gravity, "should have gravity at surface");
    check(surface.dominant_celestial_id == "test_planet",
          "dominant should be test_planet");
    check(std::abs(surface.magnitude - 10.0) < 0.01,
          "surface gravity should be 10.0");
    check(std::abs(surface.distance_to_surface) < 0.01,
          "distance to surface should be 0");

    // 重力方向应指向中心（从 +x 方向指向原点，即 -x 方向）
    check(surface.direction.x < -0.99, "gravity direction x should be -1 (toward center)");
    check(std::abs(surface.direction.y) < 0.01, "gravity direction y should be 0");
    check(std::abs(surface.direction.z) < 0.01, "gravity direction z should be 0");

    // 在影响半径外：无重力
    GravityQueryResult outside = gravity.compute_gravity(1, GlobalPos{5000.0, 0.0, 0.0});
    check(!outside.has_gravity, "should have no gravity outside influence radius");

    // 在表面上方 1000 格（影响半径 3000，半径 1000，max_height = 2000）
    // 线性衰减：t = 1000/2000 = 0.5，g = 10 * (1 - 0.5) = 5.0
    GravityQueryResult above = gravity.compute_gravity(1, GlobalPos{2000.0, 0.0, 0.0});
    check(above.has_gravity, "should have gravity above surface");
    check(std::abs(above.magnitude - 5.0) < 0.01,
          "gravity at 1000 above surface should be 5.0 (linear falloff)");
}

// ============================================================
// 3. GravityFieldManager：多重力场重叠与滞后机制
// ============================================================

void test_gravity_field_hysteresis() {
    std::cerr << "[U4] test_gravity_field_hysteresis" << std::endl;

    GravityFieldManager gravity;

    // 两个重叠的重力场
    GravityField field_a;
    field_a.celestial_id = "planet_a";
    field_a.center = GlobalPos{0.0, 0.0, 0.0};
    field_a.radius = 1000.0;
    field_a.surface_gravity = 10.0;
    field_a.influence_radius = 5000.0;
    field_a.falloff = GravityFalloff::Constant;
    gravity.register_field(field_a);

    GravityField field_b;
    field_b.celestial_id = "planet_b";
    field_b.center = GlobalPos{8000.0, 0.0, 0.0};
    field_b.radius = 1000.0;
    field_b.surface_gravity = 10.0;
    field_b.influence_radius = 5000.0;
    field_b.falloff = GravityFalloff::Constant;
    gravity.register_field(field_b);

    // 玩家在星球 A 附近（x=1000）：A 主导
    GravityQueryResult near_a = gravity.compute_gravity(1, GlobalPos{1000.0, 0.0, 0.0});
    check(near_a.dominant_celestial_id == "planet_a",
          "near planet_a should be dominated by planet_a");

    // 玩家移动到中点（x=4000）：两场引力相等，应保持 A（滞后）
    GravityQueryResult midpoint = gravity.compute_gravity(1, GlobalPos{4000.0, 0.0, 0.0});
    check(midpoint.dominant_celestial_id == "planet_a",
          "at midpoint should stay planet_a due to hysteresis");

    // 玩家移动到星球 B 附近（x=7000）：B 引力远超 A，应切换到 B
    GravityQueryResult near_b = gravity.compute_gravity(1, GlobalPos{7000.0, 0.0, 0.0});
    check(near_b.dominant_celestial_id == "planet_b",
          "near planet_b should switch to planet_b");

    // 玩家回到中点（x=4000）：应保持 B（滞后）
    GravityQueryResult midpoint_again = gravity.compute_gravity(1, GlobalPos{4000.0, 0.0, 0.0});
    check(midpoint_again.dominant_celestial_id == "planet_b",
          "returning to midpoint should stay planet_b due to hysteresis");
}

// ============================================================
// 4. CelestialLodPipeline：LOD 链路状态机
// ============================================================

void test_lod_pipeline_transitions() {
    std::cerr << "[U4] test_lod_pipeline_transitions" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    CelestialLodSystem lod_sys;
    CelestialLodPipeline pipeline;
    pipeline.register_player(1);

    // 初始：玩家在星球 A 表面，星球 B 极远
    GlobalPos pos_on_a{0.0, 64.0, 0.0};
    auto lods = lod_sys.compute_all_lods(pos_on_a, core);
    auto transitions = pipeline.update(1, pos_on_a, lods,
                                        FlightMode::LocalVoxelFlight, "");

    // 星球 A 应逐步前进到 Real（需要多次 update 单步前进）
    // 第一次：Distant -> Proxy
    // 第二次：Proxy -> Approach (LOD 2 对应 Proxy，但 Real 目标需要多步)
    // 实际上 LOD 0 (Real) 对应 pipeline Real，需要从 Distant 前进 4 步

    // 多次 update 直到稳定
    for (int i = 0; i < 10; ++i) {
        lods = lod_sys.compute_all_lods(pos_on_a, core);
        pipeline.update(1, pos_on_a, lods,
                         FlightMode::LocalVoxelFlight, "");
    }

    LodPipelineState state_a = pipeline.get_state(1, "planet_alpha");
    check(state_a == LodPipelineState::Real,
          "planet_alpha should reach Real state when on surface");

    // 星球 B 极远，应为 Distant
    LodPipelineState state_b = pipeline.get_state(1, "planet_beta");
    check(state_b == LodPipelineState::Distant,
          "planet_beta should be Distant when far away");
}

// ============================================================
// 5. CelestialLodPipeline：LandingApproach 触发 Approach 状态
// ============================================================

void test_lod_pipeline_landing_approach() {
    std::cerr << "[U4] test_lod_pipeline_landing_approach" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    CelestialLodSystem lod_sys;
    CelestialLodPipeline pipeline;
    pipeline.register_player(1);

    // 玩家在星球 B 附近，处于 LandingApproach 模式
    GlobalPos pos_near_b{120000.0, 1500.0, 0.0};
    auto lods = lod_sys.compute_all_lods(pos_near_b, core);

    // 多次 update 让星球 A 稳定到 Distant
    for (int i = 0; i < 10; ++i) {
        lods = lod_sys.compute_all_lods(pos_near_b, core);
        pipeline.update(1, pos_near_b, lods,
                         FlightMode::LandingApproach, "planet_beta");
    }

    // 星球 B 应至少达到 Approach 状态（LandingApproach 模式触发）
    LodPipelineState state_b = pipeline.get_state(1, "planet_beta");
    check(CelestialLodPipeline::is_preloading_or_higher(state_b),
          "planet_beta should be at Approach or higher during LandingApproach mode");
    check(CelestialLodPipeline::needs_real_chunks(state_b),
          "planet_beta should need real chunks during LandingApproach");
}

// ============================================================
// 6. SectorTransitionManager：Sector 边界转换
// ============================================================

void test_sector_transition() {
    std::cerr << "[U4] test_sector_transition" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    SectorTransitionManager stm;
    stm.register_player(1, SectorId{100});  // 初始在星球 A 地表

    // 玩家在星球 A 地表移动：不应触发转换
    auto event1 = stm.update_player_position(1, GlobalPos{0.0, 64.0, 0.0},
                                              core.sector_manager());
    check(!event1.has_value(), "moving within sector should not trigger transition");
    check(stm.get_current_sector(1) == SectorId{100},
          "should still be in sector 100");

    // 玩家起飞进入轨道（y=256 是边界，y=300 在轨道内）
    auto event2 = stm.update_player_position(1, GlobalPos{0.0, 300.0, 0.0},
                                              core.sector_manager());
    check(event2.has_value(), "entering orbit should trigger transition");
    check(event2->from_sector == SectorId{100}, "from should be surface sector");
    check(event2->to_sector == SectorId{101}, "to should be orbit sector");
    check(event2->from_kind == SectorKind::PlanetSurface, "from kind should be PlanetSurface");
    check(event2->to_kind == SectorKind::PlanetOrbit, "to kind should be PlanetOrbit");
    check(stm.get_current_sector(1) == SectorId{101},
          "should now be in orbit sector 101");

    // 玩家进入深空
    auto event3 = stm.update_player_position(1, GlobalPos{50000.0, 0.0, 0.0},
                                              core.sector_manager());
    check(event3.has_value(), "entering deep space should trigger transition");
    check(event3->to_sector == SectorId{200}, "to should be deep space sector");
    check(event3->to_kind == SectorKind::DeepSpace, "to kind should be DeepSpace");

    // 玩家进入星球 B 轨道
    auto event4 = stm.update_player_position(1, GlobalPos{120000.0, 1500.0, 0.0},
                                              core.sector_manager());
    check(event4.has_value(), "entering planet B orbit should trigger transition");
    check(event4->to_sector == SectorId{301}, "to should be planet B orbit sector");

    // 玩家着陆到星球 B 地表
    auto event5 = stm.update_player_position(1, GlobalPos{120000.0, 64.0, 0.0},
                                              core.sector_manager());
    check(event5.has_value(), "landing on planet B should trigger transition");
    check(event5->to_sector == SectorId{300}, "to should be planet B surface sector");
    check(event5->to_kind == SectorKind::PlanetSurface, "to kind should be PlanetSurface");

    // 转换历史
    auto history = stm.get_transition_history(1);
    check(history.size() == 4, "should have 4 transitions in history");
}

// ============================================================
// 7. SectorTransitionManager：不切换 active_dimension
// ============================================================

void test_no_active_dimension_switch() {
    std::cerr << "[U4] test_no_active_dimension_switch" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    SectorTransitionManager stm;
    stm.register_player(1, SectorId{100});

    // 玩家从星球 A 飞到星球 B
    stm.update_player_position(1, GlobalPos{0.0, 300.0, 0.0}, core.sector_manager());
    stm.update_player_position(1, GlobalPos{50000.0, 0.0, 0.0}, core.sector_manager());
    stm.update_player_position(1, GlobalPos{120000.0, 1500.0, 0.0}, core.sector_manager());
    stm.update_player_position(1, GlobalPos{120000.0, 64.0, 0.0}, core.sector_manager());

    // 玩家当前应在星球 B 地表 Sector
    check(stm.get_current_sector(1) == SectorId{300},
          "player should be in planet B surface sector");

    // 但所有 Sector 仍然注册可用（没有切换 active_dimension 的概念）
    // 验证：星球 A 的 Sector 仍然可查询
    SectorQueryResult q_a = core.find_sector(GlobalBlockPos{0, 64, 0});
    check(q_a.found(), "planet A sector should still be queryable");
    check(q_a.sector == SectorId{100}, "planet A sector should still be 100");

    // 验证：星球 B 的 Sector 也可查询
    SectorQueryResult q_b = core.find_sector(GlobalBlockPos{120000, 64, 0});
    check(q_b.found(), "planet B sector should be queryable");
    check(q_b.sector == SectorId{300}, "planet B sector should be 300");

    // 验证：两个 Sector 都在 SectorManager 中注册
    check(core.sector_manager().sector_count() == 5,
          "should still have 5 sectors registered (no dimension switch)");
}

// ============================================================
// 8. 完整连续旅行场景：A 地表 -> A 轨道 -> 深空 -> B 轨道 -> B 地表
// ============================================================

void test_continuous_travel_scenario() {
    std::cerr << "[U4] test_continuous_travel_scenario" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    InterestManager im;
    CelestialLodPipeline pipeline;
    SectorTransitionManager stm;

    StreamingConfig stream_cfg;
    stream_cfg.sphere_radius_chunks = 2;
    stream_cfg.max_chunk_requests_per_frame = 100;
    im.chunk_streaming().set_config(stream_cfg);

    FlightModeConfig flight_cfg;
    flight_cfg.cruise_speed_threshold = 200.0;
    flight_cfg.landing_approach_distance = 2000.0;
    flight_cfg.local_flight_distance = 500.0;
    im.flight_tracker().set_config(flight_cfg);

    pipeline.register_player(1);
    stm.register_player(1, SectorId{100});

    // 阶段 1：星球 A 地表，低速
    GlobalPos pos1{0.5, 64.5, 0.5};
    im.register_player(1, pos1, GlobalPos{0.0, 0.0, 0.0}, SectorId{100});
    InterestSet interest1 = im.compute_interest(1, core.sector_manager(), core);
    check(interest1.flight_mode == FlightMode::LocalVoxelFlight,
          "stage 1: should be LocalVoxelFlight on planet A surface");
    check(!interest1.chunks.empty(), "stage 1: should load real chunks on A");

    // 重力：星球 A 主导
    GravityQueryResult grav1 = gravity.compute_gravity(1, pos1);
    check(grav1.dominant_celestial_id == "planet_alpha",
          "stage 1: gravity should be dominated by planet_alpha");

    // 阶段 2：起飞进入轨道，高速 Cruise
    GlobalPos pos2{0.5, 5000.5, 0.5};
    im.update_player(1, pos2, GlobalPos{0.0, 500.0, 0.0});
    auto trans2 = stm.update_player_position(1, pos2, core.sector_manager());
    InterestSet interest2 = im.compute_interest(1, core.sector_manager(), core);
    check(interest2.flight_mode == FlightMode::Cruise,
          "stage 2: should be Cruise after takeoff");
    check(interest2.chunks.empty(), "stage 2: should not load real chunks in Cruise");
    check(trans2.has_value(), "stage 2: should trigger sector transition");
    check(trans2->to_sector == SectorId{101}, "stage 2: should enter orbit sector");

    // 阶段 3：深空巡航
    GlobalPos pos3{60000.0, 0.0, 0.0};
    im.update_player(1, pos3, GlobalPos{500.0, 0.0, 0.0});
    auto trans3 = stm.update_player_position(1, pos3, core.sector_manager());
    InterestSet interest3 = im.compute_interest(1, core.sector_manager(), core);
    check(interest3.flight_mode == FlightMode::Cruise,
          "stage 3: should be Cruise in deep space");
    check(interest3.chunks.empty(), "stage 3: should not load real chunks");
    check(trans3.has_value(), "stage 3: should trigger sector transition");
    check(trans3->to_sector == SectorId{200}, "stage 3: should enter deep space sector");

    // 深空无重力
    GravityQueryResult grav3 = gravity.compute_gravity(1, pos3);
    check(!grav3.has_gravity, "stage 3: should have no gravity in deep space");

    // 阶段 4：接近星球 B，LandingApproach
    GlobalPos pos4{120000.0, 1500.0, 0.0};
    im.set_landing_target(1, "planet_beta", 1500.0);
    im.update_player(1, pos4, GlobalPos{300.0, 0.0, 0.0});
    auto trans4 = stm.update_player_position(1, pos4, core.sector_manager());
    InterestSet interest4 = im.compute_interest(1, core.sector_manager(), core);
    check(interest4.flight_mode == FlightMode::LandingApproach,
          "stage 4: should be LandingApproach near planet B");
    check(!interest4.chunks.empty(), "stage 4: should preload real chunks for B");
    check(trans4.has_value(), "stage 4: should trigger sector transition");
    check(trans4->to_sector == SectorId{301}, "stage 4: should enter planet B orbit sector");

    // 重力：星球 B 主导
    GravityQueryResult grav4 = gravity.compute_gravity(1, pos4);
    check(grav4.has_gravity, "stage 4: should have gravity near planet B");
    check(grav4.dominant_celestial_id == "planet_beta",
          "stage 4: gravity should be dominated by planet_beta");

    // 阶段 5：着陆星球 B，低速
    GlobalPos pos5{120000.5, 64.5, 0.5};
    im.set_landing_target(1, "planet_beta", 100.0);
    im.update_player(1, pos5, GlobalPos{10.0, 0.0, 0.0});
    auto trans5 = stm.update_player_position(1, pos5, core.sector_manager());
    InterestSet interest5 = im.compute_interest(1, core.sector_manager(), core);
    check(interest5.flight_mode == FlightMode::LocalVoxelFlight,
          "stage 5: should be LocalVoxelFlight after landing on B");
    check(!interest5.chunks.empty(), "stage 5: should load real chunks on B");
    check(trans5.has_value(), "stage 5: should trigger sector transition");
    check(trans5->to_sector == SectorId{300}, "stage 5: should enter planet B surface sector");

    // 重力：星球 B 主导，低重力
    GravityQueryResult grav5 = gravity.compute_gravity(1, pos5);
    check(grav5.dominant_celestial_id == "planet_beta",
          "stage 5: gravity should be dominated by planet_beta");
    check(grav5.magnitude < 5.0,
          "stage 5: planet B gravity should be low (3.7)");

    // 验证：星球 B 的 chunk 属于星球 B 的 Sector
    for (const auto& key : interest5.chunks) {
        check(key.sector == SectorId{300},
              "stage 5: chunks should belong to planet B surface sector");
    }
}

// ============================================================
// 9. GravityFieldManager：重力方向正确性
// ============================================================

void test_gravity_direction() {
    std::cerr << "[U4] test_gravity_direction" << std::endl;

    GravityFieldManager gravity;

    GravityField field;
    field.celestial_id = "test_planet";
    field.center = GlobalPos{1000.0, 2000.0, 3000.0};
    field.radius = 500.0;
    field.surface_gravity = 10.0;
    field.influence_radius = 2000.0;
    field.falloff = GravityFalloff::Constant;
    gravity.register_field(field);

    // 玩家在天体上方（+y 方向）
    GravityQueryResult above = gravity.compute_gravity(1, GlobalPos{1000.0, 2500.0, 3000.0});
    check(above.has_gravity, "should have gravity above planet");
    // 重力方向应指向中心，即 -y 方向
    check(above.direction.y < -0.99, "gravity should point down (-y) when above");
    check(std::abs(above.direction.x) < 0.01, "gravity x should be 0");
    check(std::abs(above.direction.z) < 0.01, "gravity z should be 0");

    // 玩家在天体下方（-y 方向）
    GravityQueryResult below = gravity.compute_gravity(1, GlobalPos{1000.0, 1500.0, 3000.0});
    check(below.has_gravity, "should have gravity below planet");
    // 重力方向应指向中心，即 +y 方向
    check(below.direction.y > 0.99, "gravity should point up (+y) when below");
}

// ============================================================
// 10. CelestialLodPipeline：远离时反向转换
// ============================================================

void test_lod_pipeline_reverse() {
    std::cerr << "[U4] test_lod_pipeline_reverse" << std::endl;

    UniverseWorldCore core;
    GravityFieldManager gravity;
    setup_two_planet_universe_with_env(core, gravity);

    CelestialLodSystem lod_sys;
    CelestialLodPipeline pipeline;
    pipeline.register_player(1);

    // 玩家在星球 A 表面：A 达到 Real
    GlobalPos pos_on_a{0.0, 64.0, 0.0};
    for (int i = 0; i < 10; ++i) {
        auto lods = lod_sys.compute_all_lods(pos_on_a, core);
        pipeline.update(1, pos_on_a, lods,
                         FlightMode::LocalVoxelFlight, "");
    }
    check(pipeline.get_state(1, "planet_alpha") == LodPipelineState::Real,
          "planet A should be Real when on surface");

    // 玩家飞到深空：A 应逐步后退到 Distant
    GlobalPos pos_deep{60000.0, 0.0, 0.0};
    for (int i = 0; i < 10; ++i) {
        auto lods = lod_sys.compute_all_lods(pos_deep, core);
        pipeline.update(1, pos_deep, lods,
                         FlightMode::Cruise, "");
    }
    LodPipelineState state_a_deep = pipeline.get_state(1, "planet_alpha");
    check(state_a_deep == LodPipelineState::Distant,
          "planet A should return to Distant when in deep space");
}

} // namespace

int main() {
    std::cerr << "[U4Core] starting universe planets tests" << std::endl;

    test_planet_environment_registration();
    test_single_gravity_field();
    test_gravity_field_hysteresis();
    test_lod_pipeline_transitions();
    test_lod_pipeline_landing_approach();
    test_sector_transition();
    test_no_active_dimension_switch();
    test_continuous_travel_scenario();
    test_gravity_direction();
    test_lod_pipeline_reverse();

    if (g_failures > 0) {
        std::cerr << "[U4Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U4 universe planets tests passed: planet environments, gravity fields, "
                 "LOD pipeline, sector transitions, continuous travel." << std::endl;
    return 0;
}
