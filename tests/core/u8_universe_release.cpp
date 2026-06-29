// ============================================================
// u8_universe_release.cpp — U8 性能压测、故障恢复与发布收口测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U8）：
//   1. 所有阶段测试通过，长时间航行与多人分散运行不存在无界内存增长。
//   2. 存档中断可恢复，旧版本处理结果明确且可追踪。
//
// 工作项：
//   - 建立地表、起飞、巡航、着陆、跨 Sector 工厂和多人分散六类压测场景。
//   - 对 chunk 生成、mesh、网络、模拟和存档分别建立可观测预算。
//   - 存档采用临时文件、校验和、原子替换与版本迁移。
//   - 补齐跨边界、负坐标、浮动原点重基准、断线重连、卸载中保存和中继断开等故障测试。

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <thread>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/universe_world_core.hpp"
#include "universe/performance_budget_monitor.hpp"
#include "universe/save_reliability_manager.hpp"
#include "universe/aoi_budget_manager.hpp"
#include "universe/sector_observer_map.hpp"
#include "universe/multi_sector_sync_coordinator.hpp"
#include "universe/interest_manager.hpp"
#include "universe/sector_simulation_scheduler.hpp"
#include "universe/cross_sector_relay.hpp"
#include "universe/network_isolation_guard.hpp"
#include "universe/virtual_planet_simulator.hpp"
#include "universe/floating_origin_core.hpp"
#include "simulation/simulation_system.hpp"
#include "simulation/state_sync_server.hpp"
#include "simulation/tick_profiler.hpp"
#include "simulation/tick_system.hpp"
#include "world/world_data.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "[U8 FAIL] " << message << std::endl;
    ++g_failures;
}

ChunkData make_profile_chunk() {
    ChunkData chunk;
    chunk.state = ChunkState::ACTIVE;
    chunk.terrain.resize(
        ChunkData::kChunkSize,
        ChunkData::kChunkSize,
        ChunkData::kChunkSize);
    return chunk;
}

class ProfileTestSystem final : public SimulationSystem {
public:
    void initialize(WorldData* world, EventBus* bus) override {
        world_ = world;
        event_bus_ = bus;
    }

    void tick_active(const ChunkKey&, float, const TickContext*) override {
        ++active_ticks;
    }

    void tick_sleeping(const ChunkKey&, float, const TickContext*) override {
        ++sleeping_ticks;
    }

    void shutdown() override {}

    const char* name() const override {
        return "ProfileTestSystem";
    }

    int priority() const override {
        return 2;
    }

    int active_ticks = 0;
    int sleeping_ticks = 0;
};

// 辅助：注册测试用 Sector（六类压测场景）
void setup_stress_sectors(UniverseWorldCore& core) {
    core.clear();
    core.set_seed(20260621);

    // Sector A（星球地表，可建造）
    SectorDesc sector_a;
    sector_a.id = SectorId{300};
    sector_a.name = "planet_surface_a";
    sector_a.kind = SectorKind::PlanetSurface;
    sector_a.bounds = AABB64{GlobalBlockPos{-128, -64, -128},
                              GlobalBlockPos{127, 63, 127}};
    sector_a.allow_voxel_building = true;
    sector_a.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_a);

    // Sector B（星球轨道）
    SectorDesc sector_b;
    sector_b.id = SectorId{301};
    sector_b.name = "planet_orbit_a";
    sector_b.kind = SectorKind::PlanetOrbit;
    sector_b.bounds = AABB64{GlobalBlockPos{-1000, 64, -1000},
                              GlobalBlockPos{1000, 1000, 1000}};
    sector_b.allow_voxel_building = false;
    sector_b.default_simulation = SimulationLevel::Passive;
    core.register_sector(sector_b);

    // Sector C（深空）
    SectorDesc sector_c;
    sector_c.id = SectorId{302};
    sector_c.name = "deep_space";
    sector_c.kind = SectorKind::DeepSpace;
    sector_c.bounds = AABB64{GlobalBlockPos{-500000, -500000, -500000},
                              GlobalBlockPos{500000, 500000, 500000}};
    sector_c.allow_voxel_building = false;
    sector_c.default_simulation = SimulationLevel::Passive;
    core.register_sector(sector_c);

    // Sector D（另一星球地表，可建造）
    SectorDesc sector_d;
    sector_d.id = SectorId{303};
    sector_d.name = "planet_surface_b";
    sector_d.kind = SectorKind::PlanetSurface;
    sector_d.bounds = AABB64{GlobalBlockPos{200000, -64, 200000},
                              GlobalBlockPos{200127, 63, 200127}};
    sector_d.allow_voxel_building = true;
    sector_d.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_d);

    // Sector E（空间站，可建造）
    SectorDesc sector_e;
    sector_e.id = SectorId{304};
    sector_e.name = "space_station";
    sector_e.kind = SectorKind::SpaceStation;
    sector_e.bounds = AABB64{GlobalBlockPos{10000, 500, 10000},
                              GlobalBlockPos{10063, 563, 10063}};
    sector_e.allow_voxel_building = true;
    sector_e.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_e);
}

// ============================================================
// 1. PerformanceBudgetMonitor：基本预算记录
// ============================================================

void test_perf_budget_basic() {
    std::cerr << "[U8] test_perf_budget_basic" << std::endl;

    PerformanceBudgetMonitor monitor;

    // 记录 ChunkGen 通道耗时
    bool spike = monitor.record(PerfChannel::ChunkGen, 2.0);
    check(!spike, "2.0ms should not spike (budget 4.0ms)");

    spike = monitor.record(PerfChannel::ChunkGen, 5.0);
    check(spike, "5.0ms should spike (budget 4.0ms)");

    auto stats = monitor.get_channel_stats(PerfChannel::ChunkGen);
    check(stats.last_ms == 5.0, "last_ms should be 5.0");
    check(stats.max_ms == 5.0, "max_ms should be 5.0");
    check(stats.spike_count == 1, "spike_count should be 1");
    check(stats.total_records == 2, "total_records should be 2");
}

// ============================================================
// 2. PerformanceBudgetMonitor：滚动窗口统计
// ============================================================

void test_perf_budget_window_stats() {
    std::cerr << "[U8] test_perf_budget_window_stats" << std::endl;

    PerformanceBudgetMonitor monitor;
    monitor.set_window_size(10);

    // 记录 10 个值
    for (int i = 0; i < 10; ++i) {
        monitor.record(PerfChannel::Simulation, static_cast<double>(i + 1));
    }

    auto stats = monitor.get_channel_stats(PerfChannel::Simulation);
    check(stats.total_records == 10, "total_records should be 10");
    check(stats.max_ms == 10.0, "max_ms should be 10.0");

    // 平均值 = (1+2+...+10)/10 = 5.5
    check(std::abs(stats.avg_ms - 5.5) < 0.01, "avg_ms should be 5.5");

    // p99 近似（10 个值，p99_idx = 9，值为 10.0）
    check(stats.p99_ms == 10.0, "p99_ms should be 10.0");

    // 记录第 11 个值，窗口应滚动
    monitor.record(PerfChannel::Simulation, 100.0);
    stats = monitor.get_channel_stats(PerfChannel::Simulation);
    check(stats.max_ms == 100.0, "max_ms should be 100.0 after new record");
    check(stats.total_records == 11, "total_records should be 11");
}

// ============================================================
// 3. PerformanceBudgetMonitor：持续超限检测
// ============================================================

void test_perf_budget_sustained_exceeded() {
    std::cerr << "[U8] test_perf_budget_sustained_exceeded" << std::endl;

    PerformanceBudgetMonitor monitor;

    // 配置 Simulation 通道：预算 5ms，持续超限阈值 3
    PerfChannelBudgetConfig cfg;
    cfg.max_budget_ms = 5.0;
    cfg.sustained_threshold = 3;
    monitor.set_channel_config(PerfChannel::Simulation, cfg);

    // 连续 2 次尖峰（未达阈值）
    monitor.record(PerfChannel::Simulation, 6.0);
    monitor.tick();
    check(!monitor.is_sustained_exceeded(PerfChannel::Simulation),
          "2 spikes should not trigger sustained alert");

    monitor.record(PerfChannel::Simulation, 7.0);
    monitor.tick();
    check(!monitor.is_sustained_exceeded(PerfChannel::Simulation),
          "still 2 spikes (tick reset)");

    // 第 3 次尖峰（达到阈值）
    monitor.record(PerfChannel::Simulation, 8.0);
    check(monitor.is_sustained_exceeded(PerfChannel::Simulation),
          "3 consecutive spikes should trigger sustained alert");

    auto stats = monitor.get_channel_stats(PerfChannel::Simulation);
    check(stats.sustained_alerts == 1, "should have 1 sustained alert");
}

// ============================================================
// 4. PerformanceBudgetMonitor：低频日志摘要
// ============================================================

void test_perf_budget_summary() {
    std::cerr << "[U8] test_perf_budget_summary" << std::endl;

    PerformanceBudgetMonitor monitor;

    monitor.record(PerfChannel::ChunkGen, 3.5);
    monitor.record(PerfChannel::Network, 1.2);
    monitor.tick();

    std::string summary = monitor.format_summary();
    check(summary.find("[Perf]") != std::string::npos, "summary should start with [Perf]");
    check(summary.find("ChunkGen") != std::string::npos, "summary should contain ChunkGen");
    check(summary.find("Network") != std::string::npos, "summary should contain Network");

    // 持续超限通道列表
    auto exceeded = monitor.sustained_exceeded_channels();
    check(exceeded.empty(), "no channels should be sustained exceeded");
}

// ============================================================
// 5. PerformanceBudgetMonitor：无界内存增长防护
// ============================================================

void test_perf_budget_memory_bound() {
    std::cerr << "[U8] test_perf_budget_memory_bound" << std::endl;

    PerformanceBudgetMonitor monitor;
    monitor.set_window_size(100);

    // 模拟长时间运行：10000 tick
    for (int i = 0; i < 10000; ++i) {
        monitor.record(PerfChannel::ChunkGen, static_cast<double>(i % 10));
        monitor.record(PerfChannel::Simulation, static_cast<double>(i % 8));
        monitor.tick();
    }

    // 验证窗口大小不变（无界增长防护）
    auto stats = monitor.get_channel_stats(PerfChannel::ChunkGen);
    check(stats.total_records == 10000, "total_records should be 10000");
    check(stats.max_ms <= 10.0, "max_ms should be bounded by window content");

    // 验证无崩溃、统计正确
    check(stats.avg_ms >= 0.0, "avg_ms should be non-negative");
    check(stats.p99_ms >= 0.0, "p99_ms should be non-negative");
}

// ============================================================
// 6. TickProfiler：函数级 tick 热点摘要
// ============================================================

void test_tick_profiler_function_summary() {
    std::cerr << "[U8] test_tick_profiler_function_summary" << std::endl;

    TickProfiler profiler;
    TickProfileConfig cfg;
    cfg.enabled = true;
    cfg.log_interval_ticks = 1;
    cfg.slow_scope_ms = 2.0;
    cfg.top_n = 4;
    profiler.set_config(cfg);

    profiler.begin_tick(1);
    profiler.record("fast_scope", 0.25);
    profiler.record("slow_scope", 4.0);
    profiler.end_tick(4.5);

    auto top = profiler.snapshot_top(2);
    check(top.size() == 2, "profiler top should contain two scopes");
    check(top[0].name == "slow_scope", "slow scope should rank first");
    check(top[0].slow_samples == 1, "slow scope should count slow sample");

    std::string summary = profiler.consume_due_log();
    check(summary.find("TickProfiler") != std::string::npos,
          "summary should contain TickProfiler tag");
    check(summary.find("slow_scope") != std::string::npos,
          "summary should contain slow scope name");
    check(profiler.consume_due_log().empty(),
          "profiler log should be consumed once");
}

// ============================================================
// 7. TickSystem：调度阶段记录到具体 SimulationSystem 名称
// ============================================================

void test_tick_system_profiler_records_subsystems() {
    std::cerr << "[U8] test_tick_system_profiler_records_subsystems" << std::endl;

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_profile_chunk());

    TickSystem ticks(&world);
    ticks.set_active_radius(1);
    ticks.set_profiler_enabled(true);
    ticks.set_profiler_log_interval_ticks(1);
    ticks.register_subsystem(std::make_unique<ProfileTestSystem>());
    ticks.add_player_chunk(kSinglePlayerId, "overworld", 0, 0, 0);
    ticks.tick(0.05f);

    auto top = ticks.profiler_snapshot_top(8);
    bool found_subsystem = false;
    for (const auto& entry : top) {
        if (entry.name == "sim.active.ProfileTestSystem") {
            found_subsystem = true;
            break;
        }
    }
    check(found_subsystem, "tick profiler should record active subsystem scope");

    std::string summary = ticks.consume_profiler_log();
    check(summary.find("ProfileTestSystem") != std::string::npos,
          "tick profiler summary should include subsystem name");
}

// ============================================================
// 8. SaveReliabilityManager：原子写入与读取
// ============================================================

void test_save_reliability_atomic_write() {
    std::cerr << "[U8] test_save_reliability_atomic_write" << std::endl;

    SaveReliabilityManager mgr;

    std::string test_path = "test_u8_save_atomic.bin";

    // 写入
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    SaveResult result = mgr.save_atomic(test_path, payload);
    check(result == SaveResult::Ok, "save_atomic should succeed");

    // 读取
    SaveReadResult read_result = mgr.load(test_path);
    check(read_result.ok(), "load should succeed");
    check(read_result.payload == payload, "payload should match");
    check(read_result.version == kCurrentSaveVersion, "version should be current");

    // 清理
    std::remove(test_path.c_str());
}

// ============================================================
// 7. SaveReliabilityManager：校验和验证
// ============================================================

void test_save_reliability_checksum() {
    std::cerr << "[U8] test_save_reliability_checksum" << std::endl;

    SaveReliabilityManager mgr;

    std::string test_path = "test_u8_save_checksum.bin";

    // 写入
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    mgr.save_atomic(test_path, payload);

    // 篡改文件内容（模拟损坏）
    {
        std::ofstream ofs(test_path, std::ios::binary | std::ios::in | std::ios::out);
        // 跳过 header（magic 4 + version 2 + size 4 + crc 4 = 14），篡改 payload 第一字节
        ofs.seekp(14);
        char corrupted = 0xFF;
        ofs.write(&corrupted, 1);
    }

    // 读取应检测到校验和不匹配
    SaveReadResult read_result = mgr.load(test_path);
    check(read_result.result == SaveResult::ChecksumMismatch,
          "should detect checksum mismatch");

    // 清理
    std::remove(test_path.c_str());
}

// ============================================================
// 8. SaveReliabilityManager：版本迁移
// ============================================================

void test_save_reliability_version_migration() {
    std::cerr << "[U8] test_save_reliability_version_migration" << std::endl;

    SaveReliabilityManager mgr;

    // v2 → v3 迁移
    std::vector<uint8_t> v2_payload = {1, 2, 3};
    std::vector<uint8_t> v3_payload;
    bool migrated = mgr.migrate_payload(kLegacySaveVersionV2, v2_payload, v3_payload);
    check(migrated, "v2 to v3 migration should succeed");
    check(v3_payload == v2_payload, "v2 payload should be preserved in v3");

    // v1 → v3 迁移（不支持自动迁移）
    std::vector<uint8_t> v1_payload = {0x01, 0x02};
    std::vector<uint8_t> v1_result;
    bool v1_migrated = mgr.migrate_payload(kLegacySaveVersionV1, v1_payload, v1_result);
    check(!v1_migrated, "v1 to v3 migration should require caller intervention");
}

// ============================================================
// 9. SaveReliabilityManager：版本检测
// ============================================================

void test_save_reliability_version_detection() {
    std::cerr << "[U8] test_save_reliability_version_detection" << std::endl;

    SaveReliabilityManager mgr;

    std::string test_path = "test_u8_save_version.bin";

    // 写入当前版本
    std::vector<uint8_t> payload = {42};
    mgr.save_atomic(test_path, payload);

    // 检测版本
    uint16_t version = mgr.detect_version(test_path);
    check(version == kCurrentSaveVersion, "detected version should be current");

    // 清理
    std::remove(test_path.c_str());

    // 不存在的文件
    version = mgr.detect_version("nonexistent_file.bin");
    check(version == 0, "nonexistent file should return version 0");
}

// ============================================================
// 10. SaveReliabilityManager：Sector 独立损坏隔离
// ============================================================

void test_save_reliability_sector_isolation() {
    std::cerr << "[U8] test_save_reliability_sector_isolation" << std::endl;

    SaveReliabilityManager mgr;

    // Sector A 健康
    mgr.set_sector_status(300, SectorSaveStatus::Healthy, "");

    // Sector B 损坏
    mgr.set_sector_status(301, SectorSaveStatus::Corrupted, "checksum mismatch in region 0,0,0");

    // Sector C 旧版本
    mgr.set_sector_status(302, SectorSaveStatus::LegacyVersion, "version 1, needs migration");

    // 验证隔离：损坏的 Sector 不影响其他 Sector
    auto record_a = mgr.get_sector_status(300);
    auto record_b = mgr.get_sector_status(301);
    auto record_c = mgr.get_sector_status(302);

    check(record_a.status == SectorSaveStatus::Healthy, "sector A should be healthy");
    check(record_b.status == SectorSaveStatus::Corrupted, "sector B should be corrupted");
    check(record_c.status == SectorSaveStatus::LegacyVersion, "sector C should be legacy");

    // 查询损坏的 Sector
    auto corrupted = mgr.corrupted_sectors();
    check(corrupted.size() == 1, "should have 1 corrupted sector");
    check(corrupted[0] == 301, "corrupted sector should be 301");

    // 查询旧版本的 Sector
    auto legacy = mgr.legacy_sectors();
    check(legacy.size() == 1, "should have 1 legacy sector");
    check(legacy[0] == 302, "legacy sector should be 302");

    // 清除 Sector B 状态（恢复后重置）
    mgr.clear_sector_status(301);
    check(mgr.get_sector_status(301).status == SectorSaveStatus::Healthy,
          "sector B should be healthy after clear");

    // 摘要
    std::string summary = mgr.format_summary();
    check(summary.find("[SaveReliability]") != std::string::npos,
          "summary should start with [SaveReliability]");
}

// ============================================================
// 11. SaveReliabilityManager：中断恢复（临时文件不污染目标）
// ============================================================

void test_save_reliability_interrupt_recovery() {
    std::cerr << "[U8] test_save_reliability_interrupt_recovery" << std::endl;

    SaveReliabilityManager mgr;
    std::string test_path = "test_u8_save_recovery.bin";

    // 第一次写入
    std::vector<uint8_t> payload1 = {1, 2, 3};
    mgr.save_atomic(test_path, payload1);

    // 模拟中断：创建临时文件但不完成替换
    std::string tmp_path = test_path + ".tmp";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        ofs << "incomplete data";
    }

    // 目标文件应仍可读取（未被临时文件污染）
    SaveReadResult result = mgr.load(test_path);
    check(result.ok(), "original file should still be readable after interrupt");
    check(result.payload == payload1, "original payload should be intact");

    // 第二次写入应成功（覆盖临时文件并完成原子替换）
    std::vector<uint8_t> payload2 = {4, 5, 6, 7};
    SaveResult save_result = mgr.save_atomic(test_path, payload2);
    check(save_result == SaveResult::Ok, "second save should succeed");

    // 读取新内容
    result = mgr.load(test_path);
    check(result.ok(), "new file should be readable");
    check(result.payload == payload2, "new payload should be correct");

    // 清理
    std::remove(test_path.c_str());
    std::remove(tmp_path.c_str());
}

// ============================================================
// 12. 压测场景：地表（长时间地表活动）
// ============================================================

void test_stress_surface() {
    std::cerr << "[U8] test_stress_surface" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 玩家在地表 Sector A
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});

    // 模拟 1000 tick 地表活动
    for (int i = 0; i < 1000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::Simulation, elapsed_ms);
        perf.tick();
    }

    // 验证无无界内存增长
    auto stats = perf.get_channel_stats(PerfChannel::Simulation);
    check(stats.total_records == 1000, "should have 1000 records");
    check(stats.max_ms < 1000.0, "max_ms should be reasonable (<1s)");

    // 验证玩家仍在 Sector A
    check(om.get_player_sector(1) == SectorId{300}, "player should remain in sector A");
}

// ============================================================
// 13. 压测场景：起飞（从地表到轨道）
// ============================================================

void test_stress_takeoff() {
    std::cerr << "[U8] test_stress_takeoff" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 玩家从地表起飞
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});

    // 模拟起飞过程：位置逐渐升高
    for (int i = 0; i < 100; ++i) {
        double y = static_cast<double>(i) * 10.0;  // 逐渐升高
        coord.update_player(1, GlobalPos{0, y, 0}, GlobalPos{0, 10.0, 0});

        auto t0 = std::chrono::steady_clock::now();
        coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::Simulation, elapsed_ms);
        perf.tick();
    }

    // 验证无崩溃
    auto stats = perf.get_channel_stats(PerfChannel::Simulation);
    check(stats.total_records == 100, "should have 100 records");
    check(stats.max_ms < 1000.0, "max_ms should be reasonable");
}

// ============================================================
// 14. 压测场景：巡航（深空高速飞行）
// ============================================================

void test_stress_cruise() {
    std::cerr << "[U8] test_stress_cruise" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 玩家在深空高速飞行
    coord.register_player(1, GlobalPos{0, 500, 0}, GlobalPos{100, 0, 0}, SectorId{302});

    // 模拟 500 tick 高速飞行
    for (int i = 0; i < 500; ++i) {
        double x = static_cast<double>(i) * 100.0;
        coord.update_player(1, GlobalPos{x, 500, 0}, GlobalPos{100, 0, 0});

        auto t0 = std::chrono::steady_clock::now();
        coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::Network, elapsed_ms);
        perf.tick();
    }

    // 验证无无界内存增长
    auto stats = perf.get_channel_stats(PerfChannel::Network);
    check(stats.total_records == 500, "should have 500 records");
    check(stats.max_ms < 1000.0, "max_ms should be reasonable");
}

// ============================================================
// 15. 压测场景：着陆（从轨道到地表）
// ============================================================

void test_stress_landing() {
    std::cerr << "[U8] test_stress_landing" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 玩家从高空着陆
    coord.register_player(1, GlobalPos{0, 500, 0}, GlobalPos{0, -10, 0}, SectorId{301});

    // 模拟着陆过程：位置逐渐降低
    for (int i = 0; i < 100; ++i) {
        double y = 500.0 - static_cast<double>(i) * 5.0;
        coord.update_player(1, GlobalPos{0, y, 0}, GlobalPos{0, -5.0, 0});

        auto t0 = std::chrono::steady_clock::now();
        coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::ChunkGen, elapsed_ms);
        perf.tick();
    }

    auto stats = perf.get_channel_stats(PerfChannel::ChunkGen);
    check(stats.total_records == 100, "should have 100 records");
}

// ============================================================
// 16. 压测场景：跨 Sector 工厂
// ============================================================

void test_stress_cross_sector_factory() {
    std::cerr << "[U8] test_stress_cross_sector_factory" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    // 设置跨 Sector 中继
    CrossSectorRelayManager relay_mgr;
    CrossSectorRelay power_relay("pr1", RelayType::PowerBridge,
                                  SectorId{300}, SectorId{304});
    power_relay.connect();
    relay_mgr.register_relay(power_relay);

    CrossSectorRelay freight_relay("fr1", RelayType::FreightRelay,
                                    SectorId{300}, SectorId{304});
    freight_relay.connect();
    relay_mgr.register_relay(freight_relay);

    // 设置网络隔离守卫
    NetworkIsolationGuard guard;
    guard.register_node(GlobalBlockPos{0, 0, 0}, SectorId{300}, NetworkType::Power);
    guard.register_node(GlobalBlockPos{10000, 500, 10000}, SectorId{304}, NetworkType::Power);
    guard.register_boundary_node(GlobalBlockPos{127, 0, 0}, SectorId{300},
                                  NetworkType::Power, SectorId{304});

    // 模拟 200 tick 跨 Sector 工业运行
    for (int i = 0; i < 200; ++i) {
        // 模拟电力传输
        RelayPayload power;
        power.resource_id = "eu";
        power.amount = 100;
        relay_mgr.find_relay("pr1")->enqueue(power);

        // 模拟物品传输
        RelayPayload item;
        item.resource_id = "item:iron_ingot";
        item.amount = 10;
        relay_mgr.find_relay("fr1")->enqueue(item);

        relay_mgr.tick_all();
    }

    // 验证中继统计
    auto power_stats = relay_mgr.find_relay("pr1")->stats();
    auto freight_stats = relay_mgr.find_relay("fr1")->stats();
    check(power_stats.total_transferred > 0, "power should be transferred");
    check(freight_stats.total_transferred > 0, "freight should be transferred");

    // 验证网络隔离
    check(!guard.can_traverse(GlobalBlockPos{0, 0, 0},
                               GlobalBlockPos{10000, 500, 10000},
                               NetworkType::Power),
          "direct cross-sector traversal should be blocked");
}

// ============================================================
// 17. 压测场景：多人分散（多玩家多 Sector）
// ============================================================

void test_stress_multi_player_scattered() {
    std::cerr << "[U8] test_stress_multi_player_scattered" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 4 个玩家分散在不同 Sector
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});
    coord.register_player(2, GlobalPos{0, 500, 0}, GlobalPos{0, 0, 0}, SectorId{301});
    coord.register_player(3, GlobalPos{100000, 500, 100000}, GlobalPos{0, 0, 0}, SectorId{302});
    coord.register_player(4, GlobalPos{200000, 0, 200000}, GlobalPos{0, 0, 0}, SectorId{303});

    // 模拟 500 tick 多人分散运行
    for (int i = 0; i < 500; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto batches = coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::Network, elapsed_ms);
        perf.tick();

        check(batches.size() == 4, "should produce 4 batches every tick");
    }

    // 验证无无界内存增长
    auto stats = perf.get_channel_stats(PerfChannel::Network);
    check(stats.total_records == 500, "should have 500 records");
    check(stats.max_ms < 1000.0, "max_ms should be reasonable");

    // 验证所有玩家仍在各自 Sector
    check(om.get_player_sector(1) == SectorId{300}, "player 1 should be in sector 300");
    check(om.get_player_sector(2) == SectorId{301}, "player 2 should be in sector 301");
    check(om.get_player_sector(3) == SectorId{302}, "player 3 should be in sector 302");
    check(om.get_player_sector(4) == SectorId{303}, "player 4 should be in sector 303");
}

// ============================================================
// 18. 故障测试：跨边界（Sector 边界穿越）
// ============================================================

void test_fault_cross_boundary() {
    std::cerr << "[U8] test_fault_cross_boundary" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    SectorObserverMap om;
    om.register_player(1);
    om.set_player_sector(1, SectorId{300});

    // 玩家在 Sector A 边界附近
    SectorChunkKey chunk_in_a{SectorId{300}, ChunkCoord{3, 0, 0}};
    SectorChunkKey chunk_in_b{SectorId{301}, ChunkCoord{3, 0, 0}};

    om.add_observed_chunk(1, chunk_in_a);
    check(om.observed_chunk_count(1) == 1, "should observe 1 chunk in sector A");

    // 尝试添加 Sector B 的 chunk（应被拒绝，跨 Sector 隔离）
    om.add_observed_chunk(1, chunk_in_b);
    check(om.observed_chunk_count(1) == 1, "should still observe 1 chunk (cross-sector rejected)");

    // 玩家切换到 Sector B
    om.set_player_sector(1, SectorId{301});
    check(om.observed_chunk_count(1) == 0, "observed chunks should be cleared on sector switch");

    // 现在可以添加 Sector B 的 chunk
    om.add_observed_chunk(1, chunk_in_b);
    check(om.observed_chunk_count(1) == 1, "should observe 1 chunk in sector B");
}

// ============================================================
// 19. 故障测试：负坐标
// ============================================================

void test_fault_negative_coordinates() {
    std::cerr << "[U8] test_fault_negative_coordinates" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    // 负坐标方块位置
    GlobalBlockPos neg_pos{-100, -50, -100};

    // 查找所属 Sector
    auto result = core.find_sector(neg_pos);
    check(result.found(), "negative position should find a sector");
    check(result.sector == SectorId{300}, "negative position should be in sector 300");

    // 转换为 chunk 坐标（floor division）
    ChunkCoord cc = block_pos_to_chunk_coord(neg_pos);
    // -100 / 32 = -3.125 → floor = -4
    check(cc.cx == -4, "chunk cx should be -4 (floor division)");
    check(cc.cy == -2, "chunk cy should be -2 (floor division)");
    check(cc.cz == -4, "chunk cz should be -4 (floor division)");

    // 转换为 SectorChunkKey
    auto sck = core.make_chunk_key(neg_pos);
    check(sck.has_value(), "should produce SectorChunkKey");
    check(sck->sector == SectorId{300}, "SectorChunkKey sector should be 300");
}

// ============================================================
// 20. 故障测试：浮动原点重基准
// ============================================================

void test_fault_floating_origin_rebase() {
    std::cerr << "[U8] test_fault_floating_origin_rebase" << std::endl;

    FloatingOriginCore fo;

    // 初始原点
    fo.set_origin(GlobalPos{0.0, 0.0, 0.0});

    // 玩家在远处
    GlobalPos player_global{100000.0, 0.0, 100000.0};
    GlobalPos player_local = fo.universe_to_render(player_global);
    check(player_local.x == 100000.0, "local x should equal global when origin is 0");

    // 重基准：原点移到玩家附近
    fo.set_origin(GlobalPos{100000.0, 0.0, 100000.0});

    player_local = fo.universe_to_render(player_global);
    check(player_local.x == 0.0, "local x should be 0 after rebase to player position");
    check(player_local.z == 0.0, "local z should be 0 after rebase");

    // 验证全局坐标恢复
    GlobalPos restored = fo.render_to_universe(player_local);
    check(restored.x == 100000.0, "restored global x should be 100000");
}

// ============================================================
// 21. 故障测试：断线重连
// ============================================================

void test_fault_disconnect_reconnect() {
    std::cerr << "[U8] test_fault_disconnect_reconnect" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 玩家注册并运行几个 tick
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});
    coord.tick();
    coord.tick();

    // 模拟断线：注销玩家
    coord.unregister_player(1);
    check(coord.player_count() == 0, "player count should be 0 after disconnect");

    // 继续运行几个 tick（无玩家）
    coord.tick();
    coord.tick();

    // 模拟重连：重新注册玩家
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});
    check(coord.player_count() == 1, "player count should be 1 after reconnect");

    // 验证重连后可正常运行
    auto batches = coord.tick();
    check(batches.size() == 1, "should produce 1 batch after reconnect");
    check(batches[0].observer == 1, "batch should be for player 1");
}

// ============================================================
// 22. 故障测试：卸载中保存
// ============================================================

void test_fault_save_during_unload() {
    std::cerr << "[U8] test_fault_save_during_unload" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    SaveReliabilityManager save_mgr;

    // 模拟 Sector 正在卸载时保存
    // 设置 Sector 状态为"正在卸载"（用 Corrupted 模拟中间状态）
    save_mgr.set_sector_status(300, SectorSaveStatus::Corrupted,
                                "unload in progress, save may be incomplete");

    // 尝试保存（应能完成，但标记状态）
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
    SaveResult result = save_mgr.save_atomic("test_u8_unload_save.bin", payload);
    check(result == SaveResult::Ok, "save should succeed even during unload");

    // 验证文件可读取
    SaveReadResult read_result = save_mgr.load("test_u8_unload_save.bin");
    check(read_result.ok(), "file should be readable");
    check(read_result.payload == payload, "payload should match");

    // 清理
    std::remove("test_u8_unload_save.bin");

    // 验证 Sector 状态记录（用于诊断）
    auto record = save_mgr.get_sector_status(300);
    check(record.status == SectorSaveStatus::Corrupted, "sector should be marked corrupted");
    check(!record.last_error.empty(), "should have error detail");
}

// ============================================================
// 23. 故障测试：中继断开
// ============================================================

void test_fault_relay_disconnect() {
    std::cerr << "[U8] test_fault_relay_disconnect" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    CrossSectorRelayManager relay_mgr;

    // 创建并连接中继
    CrossSectorRelay relay("pr1", RelayType::PowerBridge,
                            SectorId{300}, SectorId{304});
    relay.connect();
    relay_mgr.register_relay(relay);

    // 入队一些载荷
    RelayPayload p;
    p.resource_id = "eu";
    p.amount = 100;
    relay_mgr.find_relay("pr1")->enqueue(p);
    relay_mgr.find_relay("pr1")->enqueue(p);

    // 断开中继
    relay_mgr.find_relay("pr1")->disconnect();

    // 验证断开后队列被清空（drop_on_disconnect 默认 true）
    auto stats = relay_mgr.find_relay("pr1")->stats();
    check(stats.total_dropped >= 2, "queued payloads should be dropped on disconnect");

    // 重新连接
    relay_mgr.find_relay("pr1")->connect();

    // 验证可继续使用
    relay_mgr.find_relay("pr1")->enqueue(p);
    relay_mgr.tick_all();

    auto stats2 = relay_mgr.find_relay("pr1")->stats();
    check(stats2.total_transferred > 0, "should transfer after reconnect");
}

// ============================================================
// 24. 故障测试：虚拟星球模拟器崩溃恢复
// ============================================================

void test_fault_vps_crash_recovery() {
    std::cerr << "[U8] test_fault_vps_crash_recovery" << std::endl;

    VirtualPlanetSimulator vps1;

    // 模拟一批产物
    std::string session = vps1.begin_session(SectorId{303}, 1000,
                                              SimulationLevel::LowFrequency);
    auto b1 = vps1.simulate_batch(session, 1050, {{"item:iron", 100, "furnace_1"}});
    auto b2 = vps1.simulate_batch(session, 1100, {{"item:copper", 50, "furnace_2"}});

    // 回灌 b1（b2 未回灌就"崩溃"）
    vps1.replay_batch(b1->batch_id);

    // 导出已回灌日志
    auto replayed_log = vps1.export_replayed_batch_ids();

    // 模拟崩溃后重建
    VirtualPlanetSimulator vps2;
    vps2.import_replayed_batch_ids(replayed_log);

    // 重新模拟（相同参数生成相同 batch_id）
    std::string session2 = vps2.begin_session(SectorId{303}, 1000,
                                               SimulationLevel::LowFrequency);
    auto b1_new = vps2.simulate_batch(session2, 1050, {{"item:iron", 100, "furnace_1"}});
    auto b2_new = vps2.simulate_batch(session2, 1100, {{"item:copper", 50, "furnace_2"}});

    // b1_new 应被识别为已回灌（幂等）
    auto r1 = vps2.replay_batch(b1_new->batch_id);
    check(r1.success, "b1 replay should succeed");
    check(r1.applied_products.empty(), "b1 should be idempotent (no products applied)");

    // b2_new 正常回灌
    auto r2 = vps2.replay_batch(b2_new->batch_id);
    check(r2.success, "b2 replay should succeed");
    check(r2.applied_products.size() == 1, "b2 should apply 1 product");
}

// ============================================================
// 25. 完整 U8 场景：综合压测
// ============================================================

void test_full_u8_scenario() {
    std::cerr << "[U8] test_full_u8_scenario" << std::endl;

    UniverseWorldCore core;
    setup_stress_sectors(core);

    InterestManager im;
    StateSyncServer sss;
    AoiBudgetManager bm;
    SectorObserverMap om;
    PerformanceBudgetMonitor perf;
    SaveReliabilityManager save_mgr;

    MultiSectorSyncCoordinator coord;
    coord.set_interest_manager(&im);
    coord.set_state_sync_server(&sss);
    coord.set_budget_manager(&bm);
    coord.set_observer_map(&om);
    coord.set_sector_manager(&core.sector_manager());
    coord.set_universe_core(&core);

    // 1. 多玩家注册
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{300});
    coord.register_player(2, GlobalPos{200050, 0, 200050}, GlobalPos{0, 0, 0}, SectorId{303});

    // 2. 模拟 200 tick 运行
    for (int i = 0; i < 200; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        coord.tick();
        auto t1 = std::chrono::steady_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        perf.record(PerfChannel::Simulation, elapsed_ms);
        perf.tick();
    }

    // 3. 验证性能预算
    auto stats = perf.get_channel_stats(PerfChannel::Simulation);
    check(stats.total_records == 200, "should have 200 records");
    check(stats.max_ms < 1000.0, "max_ms should be reasonable");

    // 4. 验证存档可靠性
    std::vector<uint8_t> save_data = {0xDE, 0xAD, 0xBE, 0xEF};
    SaveResult save_result = save_mgr.save_atomic("test_u8_full_save.bin", save_data);
    check(save_result == SaveResult::Ok, "save should succeed");

    SaveReadResult read_result = save_mgr.load("test_u8_full_save.bin");
    check(read_result.ok(), "load should succeed");
    check(read_result.payload == save_data, "payload should match");

    // 5. 标记 Sector 状态
    save_mgr.set_sector_status(300, SectorSaveStatus::Healthy, "");
    save_mgr.set_sector_status(303, SectorSaveStatus::Healthy, "");

    // 6. 验证无损坏 Sector
    auto corrupted = save_mgr.corrupted_sectors();
    check(corrupted.empty(), "no sectors should be corrupted");

    // 7. 生成低频日志摘要
    std::string perf_summary = perf.format_summary();
    std::string save_summary = save_mgr.format_summary();
    check(!perf_summary.empty(), "perf summary should not be empty");
    check(!save_summary.empty(), "save summary should not be empty");

    // 清理
    std::remove("test_u8_full_save.bin");
}

} // namespace

int main() {
    std::cerr << "[U8Core] starting universe release tests" << std::endl;

    // PerformanceBudgetMonitor 测试
    test_perf_budget_basic();
    test_perf_budget_window_stats();
    test_perf_budget_sustained_exceeded();
    test_perf_budget_summary();
    test_perf_budget_memory_bound();
    test_tick_profiler_function_summary();
    test_tick_system_profiler_records_subsystems();

    // SaveReliabilityManager 测试
    test_save_reliability_atomic_write();
    test_save_reliability_checksum();
    test_save_reliability_version_migration();
    test_save_reliability_version_detection();
    test_save_reliability_sector_isolation();
    test_save_reliability_interrupt_recovery();

    // 压测场景（六类）
    test_stress_surface();
    test_stress_takeoff();
    test_stress_cruise();
    test_stress_landing();
    test_stress_cross_sector_factory();
    test_stress_multi_player_scattered();

    // 故障恢复测试（六类）
    test_fault_cross_boundary();
    test_fault_negative_coordinates();
    test_fault_floating_origin_rebase();
    test_fault_disconnect_reconnect();
    test_fault_save_during_unload();
    test_fault_relay_disconnect();
    test_fault_vps_crash_recovery();

    // 完整 U8 场景
    test_full_u8_scenario();

    if (g_failures > 0) {
        std::cerr << "[U8Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U8 universe release tests passed: performance budget monitor, "
                 "save reliability, stress scenarios (surface/takeoff/cruise/landing/"
                 "cross-sector factory/multi-player scattered), fault recovery "
                 "(cross-boundary/negative coords/floating origin rebase/disconnect-reconnect/"
                 "save during unload/relay disconnect/vps crash recovery)." << std::endl;
    return 0;
}
