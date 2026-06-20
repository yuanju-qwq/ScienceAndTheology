// ============================================================
// u6_universe_simulation.cpp — U6 分段模拟与跨 Sector 工业中继测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U6）：
//   1. TickSystem 按 Sector 的 SimulationLevel 调度。
//   2. 子系统只在所属 Sector 内 tick，不跨 Sector 遍历。
//   3. 跨 Sector 资源只能经过中继，受吞吐与损耗约束。
//   4. 网络图 dirty/重建不递归加载另一 Sector 普通节点。
//   5. 休眠 Sector 恢复后状态可确定重放（产物回灌幂等）。

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/universe_world_core.hpp"
#include "universe/sector_simulation_scheduler.hpp"
#include "universe/cross_sector_relay.hpp"
#include "universe/network_isolation_guard.hpp"
#include "universe/virtual_planet_simulator.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U6 FAIL] " << message << std::endl;
    ++g_failures;
}

// 辅助：注册测试用 Sector
void setup_test_sectors(UniverseWorldCore& core) {
    core.clear();
    core.set_seed(20260620);

    // Sector A（地表，Active）
    SectorDesc sector_a;
    sector_a.id = SectorId{100};
    sector_a.name = "sector_a";
    sector_a.kind = SectorKind::PlanetSurface;
    sector_a.bounds = AABB64{GlobalBlockPos{-32, -32, -32},
                              GlobalBlockPos{31, 31, 31}};
    sector_a.allow_voxel_building = true;
    sector_a.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_a);

    // Sector B（地表，LowFrequency）
    SectorDesc sector_b;
    sector_b.id = SectorId{101};
    sector_b.name = "sector_b";
    sector_b.kind = SectorKind::PlanetSurface;
    sector_b.bounds = AABB64{GlobalBlockPos{32, -32, -32},
                              GlobalBlockPos{95, 31, 31}};
    sector_b.allow_voxel_building = true;
    sector_b.default_simulation = SimulationLevel::LowFrequency;
    core.register_sector(sector_b);

    // Sector C（深空，Passive）
    SectorDesc sector_c;
    sector_c.id = SectorId{102};
    sector_c.name = "sector_c";
    sector_c.kind = SectorKind::DeepSpace;
    sector_c.bounds = AABB64{GlobalBlockPos{-10000, -10000, -10000},
                              GlobalBlockPos{10000, 10000, 10000}};
    sector_c.allow_voxel_building = false;
    sector_c.default_simulation = SimulationLevel::Passive;
    core.register_sector(sector_c);

    // Sector D（轨道，Unloaded）
    SectorDesc sector_d;
    sector_d.id = SectorId{103};
    sector_d.name = "sector_d";
    sector_d.kind = SectorKind::PlanetOrbit;
    sector_d.bounds = AABB64{GlobalBlockPos{20000, 256, 20000},
                              GlobalBlockPos{30000, 1024, 30000}};
    sector_d.allow_voxel_building = false;
    sector_d.default_simulation = SimulationLevel::Unloaded;
    core.register_sector(sector_d);
}

// ============================================================
// 1. SectorSimulationScheduler：基本调度
// ============================================================

void test_scheduler_basic() {
    std::cerr << "[U6] test_scheduler_basic" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

    SectorSimulationScheduler scheduler;
    scheduler.sync_from_sector_manager(core.sector_manager());

    // 记录每个 Sector 被 tick 的次数
    std::unordered_map<uint64_t, int> tick_counts;
    int tick_invocations = 0;

    SectorSubsystem test_subsystem;
    test_subsystem.name = "test";
    test_subsystem.priority = 0;
    test_subsystem.tick_callback = [&](const SectorTickContext& ctx) {
        ++tick_counts[ctx.sector.value];
        ++tick_invocations;
    };
    scheduler.register_subsystem(test_subsystem);

    // 推进 10 tick
    for (int i = 0; i < 10; ++i) {
        scheduler.tick(0.05f);
    }

    // Sector A（Active）应该每 tick 都调度（10 次）
    check(tick_counts[100] == 10, "Active sector should tick every frame");

    // Sector B（LowFrequency）应该按间隔调度
    // 默认间隔 4，10 tick 中应该调度 ceil(10/4)=3 次（tick 0,4,8）
    check(tick_counts[101] > 0, "LowFrequency sector should tick at least once");
    check(tick_counts[101] < 10, "LowFrequency sector should tick less than Active");

    // Sector C（Passive）不应该被调度
    check(tick_counts[102] == 0, "Passive sector should not tick");

    // Sector D（Unloaded）不应该被调度
    check(tick_counts[103] == 0, "Unloaded sector should not tick");

    // 验证调度统计
    check(scheduler.last_tick_invocations() > 0, "should have tick invocations");
}

// ============================================================
// 2. SectorSimulationScheduler：子系统按优先级排序
// ============================================================

void test_scheduler_priority() {
    std::cerr << "[U6] test_scheduler_priority" << std::endl;

    SectorSimulationScheduler scheduler;
    scheduler.set_sector_level(SectorId{100}, SimulationLevel::Active);

    std::vector<std::string> execution_order;

    SectorSubsystem low_priority;
    low_priority.name = "low";
    low_priority.priority = 10;
    low_priority.tick_callback = [&](const SectorTickContext&) {
        execution_order.push_back("low");
    };

    SectorSubsystem high_priority;
    high_priority.name = "high";
    high_priority.priority = 1;
    high_priority.tick_callback = [&](const SectorTickContext&) {
        execution_order.push_back("high");
    };

    SectorSubsystem mid_priority;
    mid_priority.name = "mid";
    mid_priority.priority = 5;
    mid_priority.tick_callback = [&](const SectorTickContext&) {
        execution_order.push_back("mid");
    };

    // 注册顺序与优先级不同
    scheduler.register_subsystem(low_priority);
    scheduler.register_subsystem(high_priority);
    scheduler.register_subsystem(mid_priority);

    scheduler.tick(0.05f);

    check(execution_order.size() == 3, "should execute all 3 subsystems");
    check(execution_order[0] == "high", "high priority should execute first");
    check(execution_order[1] == "mid", "mid priority should execute second");
    check(execution_order[2] == "low", "low priority should execute last");
}

// ============================================================
// 3. SectorSimulationScheduler：active_only 子系统
// ============================================================

void test_scheduler_active_only() {
    std::cerr << "[U6] test_scheduler_active_only" << std::endl;

    SectorSimulationScheduler scheduler;
    scheduler.set_sector_level(SectorId{100}, SimulationLevel::Active);
    scheduler.set_sector_level(SectorId{101}, SimulationLevel::LowFrequency);
    scheduler.set_low_frequency_interval(1);  // 每 tick 都调度 LowFrequency

    std::unordered_map<uint64_t, int> active_only_ticks;
    std::unordered_map<uint64_t, int> normal_ticks;

    SectorSubsystem active_only_sub;
    active_only_sub.name = "active_only";
    active_only_sub.priority = 0;
    active_only_sub.active_only = true;
    active_only_sub.tick_callback = [&](const SectorTickContext& ctx) {
        ++active_only_ticks[ctx.sector.value];
    };
    scheduler.register_subsystem(active_only_sub);

    SectorSubsystem normal_sub;
    normal_sub.name = "normal";
    normal_sub.priority = 1;
    normal_sub.active_only = false;
    normal_sub.tick_callback = [&](const SectorTickContext& ctx) {
        ++normal_ticks[ctx.sector.value];
    };
    scheduler.register_subsystem(normal_sub);

    scheduler.tick(0.05f);

    // active_only 只在 Active Sector tick
    check(active_only_ticks[100] == 1, "active_only should tick in Active sector");
    check(active_only_ticks[101] == 0, "active_only should NOT tick in LowFrequency sector");

    // normal 在 Active 和 LowFrequency 都 tick
    check(normal_ticks[100] == 1, "normal should tick in Active sector");
    check(normal_ticks[101] == 1, "normal should tick in LowFrequency sector");
}

// ============================================================
// 4. SectorSimulationScheduler：LowFrequency 间隔
// ============================================================

void test_scheduler_low_frequency_interval() {
    std::cerr << "[U6] test_scheduler_low_frequency_interval" << std::endl;

    SectorSimulationScheduler scheduler;
    scheduler.set_sector_level(SectorId{100}, SimulationLevel::LowFrequency);
    scheduler.set_low_frequency_interval(5);  // 每 5 tick 调度一次

    int tick_count = 0;
    SectorSubsystem sub;
    sub.name = "test";
    sub.priority = 0;
    sub.tick_callback = [&](const SectorTickContext&) {
        ++tick_count;
    };
    scheduler.register_subsystem(sub);

    // 推进 20 tick
    for (int i = 0; i < 20; ++i) {
        scheduler.tick(0.05f);
    }

    // 间隔 5，20 tick 中应该调度 4 次（tick 0, 5, 10, 15）
    check(tick_count == 4, "LowFrequency should tick 4 times in 20 ticks with interval 5");
}

// ============================================================
// 5. CrossSectorRelay：基本电力中继
// ============================================================

void test_relay_power_basic() {
    std::cerr << "[U6] test_relay_power_basic" << std::endl;

    CrossSectorRelay relay("relay_1", RelayType::PowerBridge,
                           SectorId{100}, SectorId{101});

    // 默认配置检查
    check(relay.type() == RelayType::PowerBridge, "should be PowerBridge type");
    check(relay.from_sector() == SectorId{100}, "from_sector should be 100");
    check(relay.to_sector() == SectorId{101}, "to_sector should be 101");
    check(relay.config().capacity_per_tick == 1000, "default capacity should be 1000");
    check(relay.config().loss_ratio == 0.05, "default loss should be 0.05");

    // 未连接时不能入队
    RelayPayload payload;
    payload.resource_id = "eu";
    payload.amount = 500;
    check(!relay.enqueue(payload), "should not enqueue when disconnected");

    // 连接
    relay.connect();
    check(relay.is_connected(), "should be connected");

    // 入队
    check(relay.enqueue(payload), "should enqueue when connected");
    check(relay.queue_size() == 1, "queue size should be 1");

    // tick 传输
    auto transferred = relay.tick_transfer();
    check(transferred.size() == 1, "should transfer 1 payload");
    check(transferred[0].amount < 500, "transferred amount should be less due to loss");
    check(transferred[0].amount > 0, "transferred amount should be positive");

    // 验证损耗
    // 500 * (1 - 0.05) = 475
    check(transferred[0].amount == 475, "should transfer 475 after 5% loss");

    // 队列应清空
    check(relay.queue_empty(), "queue should be empty after transfer");

    // 统计
    auto stats = relay.stats();
    check(stats.total_transferred == 475, "total_transferred should be 475");
    check(stats.total_lost == 25, "total_lost should be 25");
    check(stats.total_ticks == 1, "total_ticks should be 1");
}

// ============================================================
// 6. CrossSectorRelay：容量限制与队列
// ============================================================

void test_relay_capacity_and_queue() {
    std::cerr << "[U6] test_relay_capacity_and_queue" << std::endl;

    CrossSectorRelay relay("relay_2", RelayType::PowerBridge,
                           SectorId{100}, SectorId{101});

    RelayConfig config;
    config.type = RelayType::PowerBridge;
    config.capacity_per_tick = 1000;
    config.loss_ratio = 0.0;  // 无损耗，便于测试
    config.max_queue_size = 3;
    config.drop_on_disconnect = false;
    relay.set_config(config);
    relay.connect();

    // 入队 3 个载荷（队列上限 3）
    for (int i = 0; i < 3; ++i) {
        RelayPayload p;
        p.resource_id = "eu";
        p.amount = 600;
        check(relay.enqueue(p), "should enqueue payload");
    }

    // 第 4 个应该被拒绝
    RelayPayload p4;
    p4.resource_id = "eu";
    p4.amount = 100;
    check(!relay.enqueue(p4), "should reject 4th payload (queue full)");

    // tick 传输：容量 1000，第一个载荷 600 全部传输 + 第二个载荷部分传输 400
    auto t1 = relay.tick_transfer();
    check(t1.size() == 2, "should transfer 2 payloads in first tick (600 + 400)");
    check(t1[0].amount == 600, "first payload should be full 600");
    check(t1[1].amount == 400, "second payload should be partial 400");

    // 队列剩 2 个（200 + 600）
    check(relay.queue_size() == 2, "queue should have 2 remaining");
    check(relay.pending_amount() == 800, "pending amount should be 800");

    // 第二 tick：传输 200 + 600 = 800
    auto t2 = relay.tick_transfer();
    check(t2.size() == 2, "should transfer 2 payloads in second tick");
    check(t2[0].amount == 200, "first payload should be remaining 200");
    check(t2[1].amount == 600, "second payload should be full 600");

    // 队列清空
    check(relay.queue_empty(), "queue should be empty after second tick");
}

// ============================================================
// 7. CrossSectorRelay：断线与重连
// ============================================================

void test_relay_disconnect_reconnect() {
    std::cerr << "[U6] test_relay_disconnect_reconnect" << std::endl;

    CrossSectorRelay relay("relay_3", RelayType::FreightRelay,
                           SectorId{100}, SectorId{101});

    RelayConfig config;
    config.type = RelayType::FreightRelay;
    config.capacity_per_tick = 64;
    config.loss_ratio = 0.0;
    config.max_queue_size = 10;
    config.drop_on_disconnect = false;
    relay.set_config(config);
    relay.connect();

    // 入队
    RelayPayload p;
    p.resource_id = "item:iron";
    p.amount = 32;
    check(relay.enqueue(p), "should enqueue");

    // 断线
    relay.disconnect();
    check(!relay.is_connected(), "should be disconnected");

    // 断线时 tick 不传输
    auto t1 = relay.tick_transfer();
    check(t1.empty(), "should not transfer when disconnected");

    // 队列保留
    check(relay.queue_size() == 1, "queue should be preserved on disconnect");

    // 重连
    relay.connect();
    check(relay.is_connected(), "should be reconnected");

    // 重连后传输
    auto t2 = relay.tick_transfer();
    check(t2.size() == 1, "should transfer after reconnect");
    check(t2[0].amount == 32, "should transfer full 32");
    check(relay.queue_empty(), "queue should be empty after transfer");
}

// ============================================================
// 8. CrossSectorRelay：断线时丢弃队列
// ============================================================

void test_relay_drop_on_disconnect() {
    std::cerr << "[U6] test_relay_drop_on_disconnect" << std::endl;

    CrossSectorRelay relay("relay_4", RelayType::FluidRelay,
                           SectorId{100}, SectorId{101});

    RelayConfig config;
    config.type = RelayType::FluidRelay;
    config.capacity_per_tick = 4000;
    config.loss_ratio = 0.0;
    config.max_queue_size = 10;
    config.drop_on_disconnect = true;  // 断线时丢弃
    relay.set_config(config);
    relay.connect();

    // 入队
    RelayPayload p;
    p.resource_id = "fluid:water";
    p.amount = 1000;
    check(relay.enqueue(p), "should enqueue");

    // 断线
    relay.disconnect();

    // 队列应被丢弃
    check(relay.queue_empty(), "queue should be dropped on disconnect");

    auto stats = relay.stats();
    check(stats.total_dropped >= 1, "should record dropped count");
}

// ============================================================
// 9. CrossSectorRelay：AE2 量子链路（无损耗）
// ============================================================

void test_relay_ae2_quantum_link() {
    std::cerr << "[U6] test_relay_ae2_quantum_link" << std::endl;

    CrossSectorRelay relay("relay_5", RelayType::Ae2QuantumLink,
                           SectorId{100}, SectorId{101});

    // AE2 量子链路默认无损耗
    check(relay.config().loss_ratio == 0.0, "AE2 quantum link should have 0 loss");

    relay.connect();

    RelayPayload p;
    p.resource_id = "channel:main";
    p.amount = 5000;
    check(relay.enqueue(p), "should enqueue");

    auto transferred = relay.tick_transfer();
    check(transferred.size() == 1, "should transfer 1 payload");
    check(transferred[0].amount == 5000, "should transfer full 5000 (no loss)");

    auto stats = relay.stats();
    check(stats.total_lost == 0, "should have 0 loss");
}

// ============================================================
// 10. CrossSectorRelayManager：批量管理
// ============================================================

void test_relay_manager_batch() {
    std::cerr << "[U6] test_relay_manager_batch" << std::endl;

    CrossSectorRelayManager manager;

    // 注册多个中继
    CrossSectorRelay r1("r1", RelayType::PowerBridge, SectorId{100}, SectorId{101});
    r1.connect();
    manager.register_relay(r1);

    CrossSectorRelay r2("r2", RelayType::FreightRelay, SectorId{100}, SectorId{102});
    r2.connect();
    manager.register_relay(r2);

    CrossSectorRelay r3("r3", RelayType::FluidRelay, SectorId{101}, SectorId{102});
    r3.connect();
    manager.register_relay(r3);

    check(manager.relay_count() == 3, "should have 3 relays");

    // 查询 Sector 100 → 101 的中继
    auto relays = manager.find_relays_between(SectorId{100}, SectorId{101});
    check(relays.size() == 1, "should find 1 relay from 100 to 101");
    check(relays[0]->id() == "r1", "should find r1");

    // 入队载荷
    RelayPayload p;
    p.resource_id = "eu";
    p.amount = 100;
    manager.find_relay("r1")->enqueue(p);
    manager.find_relay("r2")->enqueue(p);
    manager.find_relay("r3")->enqueue(p);

    // 批量 tick
    auto all_transferred = manager.tick_all();
    check(all_transferred.size() == 3, "should transfer 3 payloads across all relays");

    // 断开 Sector 100 的所有中继
    manager.disconnect_sector(SectorId{100});
    check(!manager.find_relay("r1")->is_connected(), "r1 should be disconnected");
    check(!manager.find_relay("r2")->is_connected(), "r2 should be disconnected");
    check(manager.find_relay("r3")->is_connected(), "r3 should still be connected");

    // 重连
    manager.reconnect_sector(SectorId{100});
    check(manager.find_relay("r1")->is_connected(), "r1 should be reconnected");
    check(manager.find_relay("r2")->is_connected(), "r2 should be reconnected");
}

// ============================================================
// 11. NetworkIsolationGuard：同 Sector 遍历允许
// ============================================================

void test_network_isolation_same_sector() {
    std::cerr << "[U6] test_network_isolation_same_sector" << std::endl;

    NetworkIsolationGuard guard;

    // 注册同 Sector 的节点
    NetworkNodeId node_a{0, 0, 0};
    NetworkNodeId node_b{1, 0, 0};
    NetworkNodeId node_c{2, 0, 0};

    guard.register_node(node_a, SectorId{100}, NetworkType::Power);
    guard.register_node(node_b, SectorId{100}, NetworkType::Power);
    guard.register_node(node_c, SectorId{100}, NetworkType::Power);

    // 同 Sector 遍历应该允许
    check(guard.can_traverse(node_a, node_b, NetworkType::Power),
          "should allow traverse within same sector");
    check(guard.can_traverse(node_b, node_c, NetworkType::Power),
          "should allow traverse within same sector");
    check(guard.can_traverse(node_a, node_c, NetworkType::Power),
          "should allow traverse within same sector");

    // 查询节点所属 Sector
    check(guard.get_node_sector(node_a) == SectorId{100}, "node_a should be in sector 100");

    // 查询 Sector 内节点
    auto nodes = guard.nodes_in_sector(SectorId{100});
    check(nodes.size() == 3, "sector 100 should have 3 nodes");
}

// ============================================================
// 12. NetworkIsolationGuard：跨 Sector 遍历拒绝
// ============================================================

void test_network_isolation_cross_sector() {
    std::cerr << "[U6] test_network_isolation_cross_sector" << std::endl;

    NetworkIsolationGuard guard;

    // 注册不同 Sector 的节点
    NetworkNodeId node_a{0, 0, 0};   // Sector 100
    NetworkNodeId node_b{100, 0, 0}; // Sector 101

    guard.register_node(node_a, SectorId{100}, NetworkType::Power);
    guard.register_node(node_b, SectorId{101}, NetworkType::Power);

    // 跨 Sector 遍历应该拒绝
    check(!guard.can_traverse(node_a, node_b, NetworkType::Power),
          "should NOT allow traverse across sectors");
    check(!guard.can_traverse(node_b, node_a, NetworkType::Power),
          "should NOT allow traverse across sectors (reverse)");

    // 记录被阻止的访问
    guard.record_blocked_access(node_a, node_b, NetworkType::Power, 100);
    check(guard.blocked_access_count() == 1, "should have 1 blocked access record");

    auto blocked = guard.blocked_accesses();
    check(blocked.size() == 1, "should return 1 blocked access");
    check(blocked[0].source_sector == SectorId{100}, "source sector should be 100");
    check(blocked[0].target_sector == SectorId{101}, "target sector should be 101");
}

// ============================================================
// 13. NetworkIsolationGuard：边界节点
// ============================================================

void test_network_isolation_boundary_nodes() {
    std::cerr << "[U6] test_network_isolation_boundary_nodes" << std::endl;

    NetworkIsolationGuard guard;

    // 注册普通节点
    NetworkNodeId node_a{0, 0, 0};
    guard.register_node(node_a, SectorId{100}, NetworkType::Power);

    // 注册边界节点（中继端点）
    NetworkNodeId boundary{10, 0, 0};
    guard.register_boundary_node(boundary, SectorId{100}, NetworkType::Power, SectorId{101});

    // 边界节点属于 Sector 100
    check(guard.get_node_sector(boundary) == SectorId{100}, "boundary should be in sector 100");
    check(guard.is_boundary_node(boundary), "should be boundary node");
    check(guard.get_boundary_peer_sector(boundary) == SectorId{101},
          "boundary peer should be sector 101");

    // 边界节点可以被同 Sector 节点遍历到
    check(guard.can_traverse(node_a, boundary, NetworkType::Power),
          "should allow traverse to boundary node in same sector");

    // 查询 Sector 内的边界节点
    auto boundaries = guard.boundary_nodes_in_sector(SectorId{100});
    check(boundaries.size() == 1, "sector 100 should have 1 boundary node");

    // 清除 Sector
    guard.clear_sector(SectorId{100});
    check(guard.node_count() == 0, "should have 0 nodes after clear_sector");
}

// ============================================================
// 14. NetworkIsolationGuard：网络类型不匹配拒绝
// ============================================================

void test_network_isolation_type_mismatch() {
    std::cerr << "[U6] test_network_isolation_type_mismatch" << std::endl;

    NetworkIsolationGuard guard;

    NetworkNodeId node_a{0, 0, 0};
    NetworkNodeId node_b{1, 0, 0};

    guard.register_node(node_a, SectorId{100}, NetworkType::Power);
    guard.register_node(node_b, SectorId{100}, NetworkType::Fluid);

    // 同 Sector 但网络类型不同，应该拒绝
    check(!guard.can_traverse(node_a, node_b, NetworkType::Power),
          "should NOT allow traverse with mismatched network type");
    check(!guard.can_traverse(node_a, node_b, NetworkType::Fluid),
          "should NOT allow traverse with mismatched network type");
}

// ============================================================
// 15. VirtualPlanetSimulator：基本会话与回灌
// ============================================================

void test_vps_basic_session() {
    std::cerr << "[U6] test_vps_basic_session" << std::endl;

    VirtualPlanetSimulator vps;

    // 开始会话
    std::string session_id = vps.begin_session(SectorId{100}, 1000,
                                                SimulationLevel::LowFrequency);
    check(!session_id.empty(), "should create session");

    // 生成产物批次
    std::vector<ProductEntry> products;
    products.push_back({"item:iron", 100, "furnace_1"});
    products.push_back({"item:copper", 50, "furnace_2"});

    auto batch = vps.simulate_batch(session_id, 1050, products);
    check(batch.has_value(), "should simulate batch");
    check(batch->products.size() == 2, "batch should have 2 products");
    check(batch->simulated_tick == 1050, "batch tick should be 1050");

    // 结束会话
    check(vps.end_session(session_id), "should end session");

    // 回灌批次
    ReplayResult result = vps.replay_batch(batch->batch_id);
    check(result.success, "should replay batch successfully");
    check(result.applied_products.size() == 2, "should apply 2 products");

    // 验证已回灌
    check(vps.is_batch_replayed(batch->batch_id), "batch should be marked as replayed");
    check(vps.replayed_batch_count() == 1, "should have 1 replayed batch");
    check(vps.pending_batch_count() == 0, "should have 0 pending batches");
}

// ============================================================
// 16. VirtualPlanetSimulator：幂等回灌（重复回灌不重复结算）
// ============================================================

void test_vps_idempotent_replay() {
    std::cerr << "[U6] test_vps_idempotent_replay" << std::endl;

    VirtualPlanetSimulator vps;

    std::string session_id = vps.begin_session(SectorId{100}, 1000,
                                                SimulationLevel::Passive);

    // 生成多个批次
    auto b1 = vps.simulate_batch(session_id, 1050, {{"item:iron", 100, "furnace_1"}});
    auto b2 = vps.simulate_batch(session_id, 1100, {{"item:iron", 200, "furnace_1"}});
    auto b3 = vps.simulate_batch(session_id, 1150, {{"item:copper", 50, "furnace_2"}});

    vps.end_session(session_id);

    check(vps.pending_batch_count() == 3, "should have 3 pending batches");

    // 回灌 b1
    auto r1 = vps.replay_batch(b1->batch_id);
    check(r1.success, "should replay b1");
    check(r1.applied_products.size() == 1, "b1 should apply 1 product");

    // 重复回灌 b1（幂等）
    auto r1_dup = vps.replay_batch(b1->batch_id);
    check(r1_dup.success, "duplicate replay should return success");
    check(r1_dup.applied_products.empty(), "duplicate replay should NOT apply products");
    check(r1_dup.reason == "already replayed (idempotent)",
          "duplicate replay reason should indicate idempotent");

    // 回灌整个会话
    int replayed = vps.replay_session(session_id);
    check(replayed == 2, "should replay 2 remaining batches (b2, b3)");

    // 再次回灌整个会话（幂等）
    int replayed_dup = vps.replay_session(session_id);
    check(replayed_dup == 0, "duplicate session replay should replay 0 batches");

    check(vps.replayed_batch_count() == 3, "should have 3 replayed batches");
    check(vps.pending_batch_count() == 0, "should have 0 pending batches");
}

// ============================================================
// 17. VirtualPlanetSimulator：异常恢复（日志导入导出）
// ============================================================

void test_vps_crash_recovery() {
    std::cerr << "[U6] test_vps_crash_recovery" << std::endl;

    // 模拟器 1：模拟异常退出前的状态
    VirtualPlanetSimulator vps1;

    std::string session_id = vps1.begin_session(SectorId{100}, 1000,
                                                SimulationLevel::LowFrequency);
    auto b1 = vps1.simulate_batch(session_id, 1050, {{"item:iron", 100, "f1"}});
    auto b2 = vps1.simulate_batch(session_id, 1100, {{"item:iron", 200, "f1"}});

    // 回灌 b1（b2 未回灌就"崩溃"）
    vps1.replay_batch(b1->batch_id);

    // 导出已回灌日志
    auto replayed_ids = vps1.export_replayed_batch_ids();
    check(replayed_ids.size() == 1, "should export 1 replayed id");

    // 模拟器 2：重启后恢复
    VirtualPlanetSimulator vps2;

    // 重新生成相同的批次（模拟从存档恢复）
    // 注意：vps2 从 next_session_seq_=1 开始，生成的 session_id 与 vps1 相同，
    // 因此 batch_id 也相同——这正是崩溃恢复的幂等性保证。
    std::string session2 = vps2.begin_session(SectorId{100}, 1000,
                                               SimulationLevel::LowFrequency);
    auto b1_new = vps2.simulate_batch(session2, 1050, {{"item:iron", 100, "f1"}});
    auto b2_new = vps2.simulate_batch(session2, 1100, {{"item:iron", 200, "f1"}});

    // 验证 batch_id 与 vps1 相同（崩溃恢复场景）
    check(b1_new->batch_id == b1->batch_id, "b1_new should have same batch_id as b1");
    check(b2_new->batch_id == b2->batch_id, "b2_new should have same batch_id as b2");

    // 导入已回灌日志（恢复幂等状态）
    vps2.import_replayed_batch_ids(replayed_ids);

    // 导入后已回灌集合应包含导入的 id
    auto vps2_replayed = vps2.export_replayed_batch_ids();
    check(vps2_replayed.size() >= 1, "should have imported replayed ids");

    // 回灌 b1_new（应被识别为已回灌，幂等返回成功但不重复应用）
    auto r1 = vps2.replay_batch(b1_new->batch_id);
    check(r1.success, "should replay b1_new (idempotent success)");
    check(r1.applied_products.empty(), "b1_new should NOT apply products (already replayed)");

    // 回灌 b2_new（未回灌，应正常应用）
    auto r2 = vps2.replay_batch(b2_new->batch_id);
    check(r2.success, "should replay b2_new");
    check(r2.applied_products.size() == 1, "b2_new should apply 1 product");

    // 重复回灌 b2_new（幂等）
    auto r2_dup = vps2.replay_batch(b2_new->batch_id);
    check(r2_dup.success, "duplicate replay should be idempotent");
    check(r2_dup.applied_products.empty(), "duplicate should not apply products");
}

// ============================================================
// 18. VirtualPlanetSimulator：取消会话
// ============================================================

void test_vps_cancel_session() {
    std::cerr << "[U6] test_vps_cancel_session" << std::endl;

    VirtualPlanetSimulator vps;

    std::string session_id = vps.begin_session(SectorId{100}, 1000,
                                                SimulationLevel::LowFrequency);

    // 生成批次
    auto b1 = vps.simulate_batch(session_id, 1050, {{"item:iron", 100, "f1"}});

    // 取消会话（异常退出）
    check(vps.cancel_session(session_id), "should cancel session");

    const SimulationSession* session = vps.find_session(session_id);
    check(session != nullptr, "session should still exist after cancel");
    check(!session->completed, "cancelled session should not be completed");

    // 已生成的批次仍可回灌
    auto r = vps.replay_batch(b1->batch_id);
    check(r.success, "should still replay batch from cancelled session");
}

// ============================================================
// 19. 完整场景：分段模拟 + 跨 Sector 中继 + 网络隔离
// ============================================================

void test_full_u6_scenario() {
    std::cerr << "[U6] test_full_u6_scenario" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

    // 1. 设置调度器
    SectorSimulationScheduler scheduler;
    scheduler.sync_from_sector_manager(core.sector_manager());

    // 2. 设置中继管理器
    CrossSectorRelayManager relay_mgr;
    CrossSectorRelay power_relay("pr1", RelayType::PowerBridge,
                                  SectorId{100}, SectorId{101});
    power_relay.connect();
    relay_mgr.register_relay(power_relay);

    // 3. 设置网络隔离守卫
    NetworkIsolationGuard guard;
    // Sector 100 的节点
    guard.register_node(NetworkNodeId{0, 0, 0}, SectorId{100}, NetworkType::Power);
    guard.register_node(NetworkNodeId{1, 0, 0}, SectorId{100}, NetworkType::Power);
    // Sector 101 的节点
    guard.register_node(NetworkNodeId{50, 0, 0}, SectorId{101}, NetworkType::Power);
    // 边界节点（中继端点）
    guard.register_boundary_node(NetworkNodeId{31, 0, 0}, SectorId{100},
                                  NetworkType::Power, SectorId{101});
    guard.register_boundary_node(NetworkNodeId{32, 0, 0}, SectorId{101},
                                  NetworkType::Power, SectorId{100});

    // 4. 设置虚拟星球模拟器
    VirtualPlanetSimulator vps;

    // 5. 模拟 Sector B（LowFrequency）的产物
    std::string session = vps.begin_session(SectorId{101}, 0,
                                             SimulationLevel::LowFrequency);
    auto batch = vps.simulate_batch(session, 100, {{"item:iron", 50, "furnace_b"}});

    // 6. 调度 tick
    int power_ticks_sector_a = 0;
    int power_ticks_sector_b = 0;

    SectorSubsystem power_sub;
    power_sub.name = "power";
    power_sub.priority = 0;
    power_sub.tick_callback = [&](const SectorTickContext& ctx) {
        if (ctx.sector == SectorId{100}) ++power_ticks_sector_a;
        if (ctx.sector == SectorId{101}) ++power_ticks_sector_b;

        // 模拟电力传输：Sector A 通过中继向 Sector B 传输
        if (ctx.sector == SectorId{100}) {
            RelayPayload p;
            p.resource_id = "eu";
            p.amount = 200;
            relay_mgr.find_relay("pr1")->enqueue(p);
        }
    };
    scheduler.register_subsystem(power_sub);

    // 推进 10 tick
    for (int i = 0; i < 10; ++i) {
        scheduler.tick(0.05f);
        relay_mgr.tick_all();
    }

    // Sector A 应该每 tick 都调度
    check(power_ticks_sector_a == 10, "Sector A should tick 10 times");

    // Sector B 应该按 LowFrequency 间隔调度
    check(power_ticks_sector_b > 0, "Sector B should tick at least once");
    check(power_ticks_sector_b < 10, "Sector B should tick less than Sector A");

    // 7. 验证网络隔离
    // 同 Sector 遍历允许
    check(guard.can_traverse(NetworkNodeId{0, 0, 0}, NetworkNodeId{1, 0, 0},
                              NetworkType::Power),
          "should allow traverse within sector 100");

    // 跨 Sector 遍历拒绝
    check(!guard.can_traverse(NetworkNodeId{0, 0, 0}, NetworkNodeId{50, 0, 0},
                               NetworkType::Power),
          "should NOT allow traverse from sector 100 to 101");

    // 边界节点可以被同 Sector 节点访问
    check(guard.can_traverse(NetworkNodeId{0, 0, 0}, NetworkNodeId{31, 0, 0},
                              NetworkType::Power),
          "should allow traverse to boundary node");

    // 8. 回灌虚拟模拟产物
    vps.end_session(session);
    auto replay_result = vps.replay_batch(batch->batch_id);
    check(replay_result.success, "should replay virtual sim batch");
    check(replay_result.applied_products.size() == 1, "should apply 1 product");

    // 9. 重复回灌（幂等）
    auto replay_dup = vps.replay_batch(batch->batch_id);
    check(replay_dup.success, "duplicate replay should be idempotent");
    check(replay_dup.applied_products.empty(), "duplicate should not apply products");

    // 10. 验证中继统计
    auto relay_stats = relay_mgr.find_relay("pr1")->stats();
    check(relay_stats.total_transferred > 0, "should have transferred power");
    check(relay_stats.total_lost > 0, "should have loss (5% default)");
}

} // namespace

int main() {
    std::cerr << "[U6Core] starting universe simulation tests" << std::endl;

    test_scheduler_basic();
    test_scheduler_priority();
    test_scheduler_active_only();
    test_scheduler_low_frequency_interval();
    test_relay_power_basic();
    test_relay_capacity_and_queue();
    test_relay_disconnect_reconnect();
    test_relay_drop_on_disconnect();
    test_relay_ae2_quantum_link();
    test_relay_manager_batch();
    test_network_isolation_same_sector();
    test_network_isolation_cross_sector();
    test_network_isolation_boundary_nodes();
    test_network_isolation_type_mismatch();
    test_vps_basic_session();
    test_vps_idempotent_replay();
    test_vps_crash_recovery();
    test_vps_cancel_session();
    test_full_u6_scenario();

    if (g_failures > 0) {
        std::cerr << "[U6Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U6 universe simulation tests passed: sector scheduling, "
                 "cross-sector relays, network isolation, virtual planet sim." << std::endl;
    return 0;
}
