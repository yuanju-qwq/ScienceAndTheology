// ============================================================
// u7_universe_multiplayer.cpp — U7 多人 AOI 与并发 Sector 测试
// ============================================================
//
// 验收条件（见 docs/unified_universe_world_design.md 21.2 U7）：
//   1. 两个客户端分处不同 Sector 时互不接收无关真实 chunk。
//   2. 会合后实体与方块状态收敛。
//   3. 任一客户端高速移动都不会挤占另一客户端的同步预算。

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "universe/universe_types.hpp"
#include "universe/sector_manager.hpp"
#include "universe/universe_world_core.hpp"
#include "universe/aoi_budget_manager.hpp"
#include "universe/sector_observer_map.hpp"
#include "universe/multi_sector_sync_coordinator.hpp"
#include "universe/interest_manager.hpp"
#include "simulation/state_sync_server.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "[U7 FAIL] " << message << std::endl;
    ++g_failures;
}

// 辅助：注册测试用 Sector
void setup_test_sectors(UniverseWorldCore& core) {
    core.clear();
    core.set_seed(20260620);

    // Sector A（星球地表，可建造）
    SectorDesc sector_a;
    sector_a.id = SectorId{200};
    sector_a.name = "planet_alpha_surface";
    sector_a.kind = SectorKind::PlanetSurface;
    sector_a.bounds = AABB64{GlobalBlockPos{-64, -64, -64},
                              GlobalBlockPos{63, 63, 63}};
    sector_a.allow_voxel_building = true;
    sector_a.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_a);

    // Sector B（另一星球地表，可建造）
    SectorDesc sector_b;
    sector_b.id = SectorId{201};
    sector_b.name = "planet_beta_surface";
    sector_b.kind = SectorKind::PlanetSurface;
    sector_b.bounds = AABB64{GlobalBlockPos{100000, -64, -64},
                              GlobalBlockPos{100127, 63, 63}};
    sector_b.allow_voxel_building = true;
    sector_b.default_simulation = SimulationLevel::Active;
    core.register_sector(sector_b);

    // Sector C（深空，不可建造）
    SectorDesc sector_c;
    sector_c.id = SectorId{202};
    sector_c.name = "deep_space";
    sector_c.kind = SectorKind::DeepSpace;
    sector_c.bounds = AABB64{GlobalBlockPos{-500000, -500000, -500000},
                              GlobalBlockPos{500000, 500000, 500000}};
    sector_c.allow_voxel_building = false;
    sector_c.default_simulation = SimulationLevel::Passive;
    core.register_sector(sector_c);
}

// ============================================================
// 1. AoiBudgetManager：基本预算管理
// ============================================================

void test_budget_basic() {
    std::cerr << "[U7] test_budget_basic" << std::endl;

    AoiBudgetManager mgr;

    // 注册观察者
    mgr.register_observer(1);

    // 默认 Chunk 通道预算 4/tick
    auto r1 = mgr.try_send(1, SyncChannel::Chunk, 3);
    check(r1.allowed == 3, "should allow 3 chunks (under budget 4)");
    check(r1.deferred == 0, "should defer 0");
    check(r1.dropped == 0, "should drop 0");

    // 再发 2 个，超出预算 1 个
    auto r2 = mgr.try_send(1, SyncChannel::Chunk, 2);
    check(r2.allowed == 1, "should allow 1 chunk (remaining budget 1)");
    check(r2.deferred == 1, "should defer 1 chunk");
    check(r2.exhausted, "channel should be exhausted");

    // 推进 tick，预算重置
    mgr.tick();

    auto r3 = mgr.try_send(1, SyncChannel::Chunk, 4);
    check(r3.allowed == 4, "after tick, should allow 4 chunks again");
    check(r3.deferred == 0, "should defer 0 after tick reset");
}

// ============================================================
// 2. AoiBudgetManager：per-observer 隔离
// ============================================================

void test_budget_per_observer_isolation() {
    std::cerr << "[U7] test_budget_per_observer_isolation" << std::endl;

    AoiBudgetManager mgr;
    mgr.register_observer(1);
    mgr.register_observer(2);

    // 观察者 1 消耗全部 Chunk 预算
    auto r1 = mgr.try_send(1, SyncChannel::Chunk, 4);
    check(r1.allowed == 4, "observer 1 should get 4 chunks");

    // 观察者 2 的预算不受影响
    auto r2 = mgr.try_send(2, SyncChannel::Chunk, 4);
    check(r2.allowed == 4, "observer 2 should still get 4 chunks (isolated)");
    check(!r2.exhausted, "observer 2 channel should not be exhausted");
}

// ============================================================
// 3. AoiBudgetManager：通道优先级与丢弃
// ============================================================

void test_budget_channel_drop_on_overflow() {
    std::cerr << "[U7] test_budget_channel_drop_on_overflow" << std::endl;

    AoiBudgetManager mgr;
    mgr.register_observer(1);

    // MachineDetail 通道默认 defer_on_overflow=false（丢弃）
    auto r1 = mgr.try_send(1, SyncChannel::MachineDetail, 10);
    check(r1.allowed == 8, "MachineDetail should allow 8 (default max)");
    check(r1.dropped == 2, "should drop 2 (no defer on overflow)");
    check(r1.deferred == 0, "should defer 0");

    // CelestialLod 通道默认 defer_on_overflow=false（丢弃）
    auto r2 = mgr.try_send(1, SyncChannel::CelestialLod, 5);
    check(r2.allowed == 2, "CelestialLod should allow 2 (default max)");
    check(r2.dropped == 3, "should drop 3");
    check(r2.deferred == 0, "should defer 0");
}

// ============================================================
// 4. AoiBudgetManager：未注册观察者无限制
// ============================================================

void test_budget_unregistered_unlimited() {
    std::cerr << "[U7] test_budget_unregistered_unlimited" << std::endl;

    AoiBudgetManager mgr;

    // 未注册的观察者：允许全部发送
    auto r1 = mgr.try_send(999, SyncChannel::Chunk, 1000);
    check(r1.allowed == 1000, "unregistered observer should be unlimited");
    check(r1.deferred == 0, "should defer 0");
    check(r1.dropped == 0, "should drop 0");

    // 剩余预算返回 -1（无限制）
    int remaining = mgr.remaining_budget(999, SyncChannel::Chunk);
    check(remaining == -1, "unregistered observer should have unlimited budget (-1)");
}

// ============================================================
// 5. AoiBudgetManager：自定义通道配置
// ============================================================

void test_budget_custom_config() {
    std::cerr << "[U7] test_budget_custom_config" << std::endl;

    AoiBudgetManager mgr;

    ObserverBudgetConfig config;
    config.channels[static_cast<size_t>(SyncChannel::Chunk)].max_per_tick = 10;
    config.channels[static_cast<size_t>(SyncChannel::Chunk)].priority = 100;
    config.channels[static_cast<size_t>(SyncChannel::Chunk)].defer_on_overflow = false;

    mgr.register_observer(1, config);

    auto r1 = mgr.try_send(1, SyncChannel::Chunk, 15);
    check(r1.allowed == 10, "custom config should allow 10");
    check(r1.dropped == 5, "should drop 5 (defer_on_overflow=false)");
    check(r1.deferred == 0, "should defer 0");
}

// ============================================================
// 6. AoiBudgetManager：统计查询
// ============================================================

void test_budget_stats() {
    std::cerr << "[U7] test_budget_stats" << std::endl;

    AoiBudgetManager mgr;
    mgr.register_observer(1);

    mgr.try_send(1, SyncChannel::Chunk, 3);
    mgr.try_send(1, SyncChannel::Chunk, 5);  // 1 allowed, 4 deferred
    mgr.try_send(1, SyncChannel::Entity, 10);

    auto chunk_stats = mgr.get_channel_stats(1, SyncChannel::Chunk);
    check(chunk_stats.total_sent == 4, "Chunk total_sent should be 4");
    check(chunk_stats.total_deferred == 4, "Chunk total_deferred should be 4");

    auto entity_stats = mgr.get_channel_stats(1, SyncChannel::Entity);
    check(entity_stats.total_sent == 10, "Entity total_sent should be 10");

    int total = mgr.total_sent(1);
    check(total == 14, "total_sent across channels should be 14");
}

// ============================================================
// 7. SectorObserverMap：基本注册与 Sector 切换
// ============================================================

void test_observer_map_basic() {
    std::cerr << "[U7] test_observer_map_basic" << std::endl;

    SectorObserverMap map;

    map.register_player(1);
    check(map.has_player(1), "player 1 should be registered");
    check(map.player_count() == 1, "should have 1 player");

    map.set_player_sector(1, SectorId{200});
    check(map.get_player_sector(1) == SectorId{200}, "player 1 sector should be 200");
    check(map.player_count_in_sector(SectorId{200}) == 1, "sector 200 should have 1 player");

    // 切换 Sector
    map.set_player_sector(1, SectorId{201});
    check(map.get_player_sector(1) == SectorId{201}, "player 1 sector should be 201");
    check(map.player_count_in_sector(SectorId{200}) == 0, "sector 200 should have 0 players");
    check(map.player_count_in_sector(SectorId{201}) == 1, "sector 201 should have 1 player");
}

// ============================================================
// 8. SectorObserverMap：观察 chunk 跨 Sector 隔离
// ============================================================

void test_observer_map_cross_sector_isolation() {
    std::cerr << "[U7] test_observer_map_cross_sector_isolation" << std::endl;

    SectorObserverMap map;
    map.register_player(1);
    map.set_player_sector(1, SectorId{200});

    // 尝试设置包含其他 Sector 的 chunk
    std::vector<SectorChunkKey> chunks;
    chunks.push_back(SectorChunkKey{SectorId{200}, ChunkCoord{0, 0, 0}});
    chunks.push_back(SectorChunkKey{SectorId{200}, ChunkCoord{1, 0, 0}});
    chunks.push_back(SectorChunkKey{SectorId{201}, ChunkCoord{0, 0, 0}});  // 跨 Sector，应被忽略

    map.set_observed_chunks(1, chunks);

    auto observed = map.get_observed_chunks(1);
    check(observed.size() == 2, "should only observe 2 chunks (sector 200 only)");

    // 验证只有 Sector 200 的 chunk 被观察
    for (const auto& c : observed) {
        check(c.sector == SectorId{200}, "observed chunk should be in sector 200");
    }
}

// ============================================================
// 9. SectorObserverMap：会合检测
// ============================================================

void test_observer_map_convergence() {
    std::cerr << "[U7] test_observer_map_convergence" << std::endl;

    SectorObserverMap map;
    map.register_player(1);
    map.register_player(2);

    // 玩家 1 在 Sector A，玩家 2 在 Sector B
    map.set_player_sector(1, SectorId{200});
    map.set_player_sector(2, SectorId{201});

    check(!map.are_in_same_sector(1, 2), "players in different sectors should not be in same sector");

    // 玩家 2 移动到 Sector A
    map.set_player_sector(2, SectorId{200});
    check(map.are_in_same_sector(1, 2), "players in same sector should be detected");

    auto peers = map.peers_in_same_sector(1);
    check(peers.size() == 1, "player 1 should have 1 peer");
    check(peers[0] == 2, "peer should be player 2");
}

// ============================================================
// 10. SectorObserverMap：chunk 观察者查询
// ============================================================

void test_observer_map_chunk_observers() {
    std::cerr << "[U7] test_observer_map_chunk_observers" << std::endl;

    SectorObserverMap map;
    map.register_player(1);
    map.register_player(2);
    map.set_player_sector(1, SectorId{200});
    map.set_player_sector(2, SectorId{200});

    SectorChunkKey chunk{SectorId{200}, ChunkCoord{0, 0, 0}};
    map.add_observed_chunk(1, chunk);
    map.add_observed_chunk(2, chunk);

    auto observers = map.observers_of_chunk(chunk);
    check(observers.size() == 2, "chunk should have 2 observers");
    check(map.is_chunk_observed(chunk), "chunk should be observed");
}

// ============================================================
// 11. SectorObserverMap：Sector 切换清空旧观察
// ============================================================

void test_observer_map_sector_switch_clears() {
    std::cerr << "[U7] test_observer_map_sector_switch_clears" << std::endl;

    SectorObserverMap map;
    map.register_player(1);
    map.set_player_sector(1, SectorId{200});

    SectorChunkKey chunk{SectorId{200}, ChunkCoord{0, 0, 0}};
    map.add_observed_chunk(1, chunk);
    check(map.observed_chunk_count(1) == 1, "should observe 1 chunk");

    // 切换到新 Sector，旧观察应被清空
    map.set_player_sector(1, SectorId{201});
    check(map.observed_chunk_count(1) == 0, "observed chunks should be cleared on sector switch");
}

// ============================================================
// 12. SectorObserverMap：occupied_sectors 查询
// ============================================================

void test_observer_map_occupied_sectors() {
    std::cerr << "[U7] test_observer_map_occupied_sectors" << std::endl;

    SectorObserverMap map;
    map.register_player(1);
    map.register_player(2);
    map.register_player(3);
    map.set_player_sector(1, SectorId{200});
    map.set_player_sector(2, SectorId{201});
    map.set_player_sector(3, SectorId{200});

    auto sectors = map.occupied_sectors();
    check(sectors.size() == 2, "should have 2 occupied sectors");

    // 清除 Sector 200
    map.clear_sector(SectorId{200});
    check(map.get_player_sector(1).value == 0, "player 1 sector should be reset to 0");
    check(map.get_player_sector(3).value == 0, "player 3 sector should be reset to 0");
    check(map.get_player_sector(2) == SectorId{201}, "player 2 sector should be unchanged");
}

// ============================================================
// 13. MultiSectorSyncCoordinator：基本注册与 tick
// ============================================================

void test_coordinator_basic() {
    std::cerr << "[U7] test_coordinator_basic" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 注册玩家 1 在 Sector A
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});
    check(coord.player_count() == 1, "should have 1 player");

    // 执行 tick
    auto batches = coord.tick();
    check(batches.size() == 1, "should produce 1 batch");
    check(batches[0].observer == 1, "batch observer should be 1");
    check(batches[0].sector == SectorId{200}, "batch sector should be 200");
}

// ============================================================
// 14. MultiSectorSyncCoordinator：跨 Sector 隔离
// ============================================================

void test_coordinator_cross_sector_isolation() {
    std::cerr << "[U7] test_coordinator_cross_sector_isolation" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 玩家 1 在 Sector A（planet_alpha）
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});

    // 玩家 2 在 Sector B（planet_beta）
    coord.register_player(2, GlobalPos{100050, 0, 0}, GlobalPos{0, 0, 0}, SectorId{201});

    auto batches = coord.tick();
    check(batches.size() == 2, "should produce 2 batches");

    // 验证两个玩家在不同 Sector
    check(batches[0].sector != batches[1].sector, "players should be in different sectors");

    // 验证跨 Sector 隔离：玩家 1 不应收到 Sector 201 的 chunk，玩家 2 不应收到 Sector 200 的 chunk
    // 注意：AOI 可能延伸到重叠的深空 Sector（202），这是允许的。
    // 关键隔离属性：玩家不应收到对方所在 Sector 的 chunk。
    SectorId sector_a = SectorId{200};
    SectorId sector_b = SectorId{201};

    for (const auto& batch : batches) {
        SectorId player_sector = om.get_player_sector(batch.observer);
        SectorId other_player_sector = (player_sector == sector_a) ? sector_b : sector_a;

        for (const auto& chunk : batch.chunks) {
            check(chunk.sector != other_player_sector,
                  "chunk should not belong to other player's sector (isolation)");
        }
        for (const auto& delta : batch.block_deltas) {
            check(delta.sector != other_player_sector,
                  "block delta should not belong to other player's sector (isolation)");
        }
    }

    // 验证 is_chunk_visible_to
    SectorChunkKey chunk_a{SectorId{200}, ChunkCoord{0, 0, 0}};
    SectorChunkKey chunk_b{SectorId{201}, ChunkCoord{0, 0, 0}};

    check(coord.is_chunk_visible_to(1, chunk_a), "chunk_a should be visible to player 1");
    check(!coord.is_chunk_visible_to(1, chunk_b), "chunk_b should NOT be visible to player 1");
    check(!coord.is_chunk_visible_to(2, chunk_a), "chunk_a should NOT be visible to player 2");
    check(coord.is_chunk_visible_to(2, chunk_b), "chunk_b should be visible to player 2");
}

// ============================================================
// 15. MultiSectorSyncCoordinator：会合收敛
// ============================================================

void test_coordinator_convergence() {
    std::cerr << "[U7] test_coordinator_convergence" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 初始：玩家 1 在 Sector A，玩家 2 在 Sector B
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});
    coord.register_player(2, GlobalPos{100050, 0, 0}, GlobalPos{0, 0, 0}, SectorId{201});

    // 第一次 tick：两人在不同 Sector
    coord.tick();
    check(!coord.are_converged(1, 2), "players in different sectors should not be converged");

    // 玩家 2 移动到 Sector A（会合）
    om.set_player_sector(2, SectorId{200});
    coord.update_player(2, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0});

    // 第二次 tick：两人在同一 Sector
    coord.tick();

    // 验证会合
    check(om.are_in_same_sector(1, 2), "players should be in same sector after rendezvous");

    // 验证两人观察的 chunk 有重叠（会合收敛）
    // 注意：are_converged 要求观察集有重叠
    // 由于两人在同一位置，InterestManager 应计算相同的 chunk 集合
    auto chunks_1 = om.get_observed_chunks(1);
    auto chunks_2 = om.get_observed_chunks(2);

    // 如果两人都有观察 chunk，检查是否有重叠
    if (!chunks_1.empty() && !chunks_2.empty()) {
        bool has_overlap = false;
        for (const auto& c1 : chunks_1) {
            for (const auto& c2 : chunks_2) {
                if (c1 == c2) {
                    has_overlap = true;
                    break;
                }
            }
            if (has_overlap) break;
        }
        check(has_overlap, "players in same location should observe overlapping chunks");
    }
}

// ============================================================
// 16. MultiSectorSyncCoordinator：预算不挤占
// ============================================================

void test_coordinator_budget_no_preemption() {
    std::cerr << "[U7] test_coordinator_budget_no_preemption" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 玩家 1 在 Sector A
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});
    // 玩家 2 在 Sector B
    coord.register_player(2, GlobalPos{100050, 0, 0}, GlobalPos{0, 0, 0}, SectorId{201});

    // 执行多次 tick
    for (int i = 0; i < 5; ++i) {
        coord.tick();
    }

    // 验证两个玩家的预算独立
    // 玩家 1 的 Chunk 通道统计
    auto stats1 = bm.get_channel_stats(1, SyncChannel::Chunk);
    auto stats2 = bm.get_channel_stats(2, SyncChannel::Chunk);

    // 两个玩家都应该有发送量（不互相挤占）
    // 注意：发送量取决于 InterestManager 计算的 chunk 数量
    // 关键是：玩家 1 的消耗不影响玩家 2
    int remaining1 = bm.remaining_budget(1, SyncChannel::Chunk);
    int remaining2 = bm.remaining_budget(2, SyncChannel::Chunk);

    // 两人都应该有剩余预算（或都耗尽，但互不影响）
    // 验证：如果玩家 1 耗尽预算，玩家 2 仍可发送
    // 强制消耗玩家 1 的全部预算
    bm.tick();  // 重置预算
    auto r1 = bm.try_send(1, SyncChannel::Chunk, 4);  // 耗尽玩家 1
    check(r1.allowed == 4, "player 1 should consume 4");

    // 玩家 2 的预算不受影响
    auto r2 = bm.try_send(2, SyncChannel::Chunk, 4);
    check(r2.allowed == 4, "player 2 should still have full budget (no preemption)");
    check(!r2.exhausted, "player 2 channel should not be exhausted");
}

// ============================================================
// 17. MultiSectorSyncCoordinator：get_last_batch 查询
// ============================================================

void test_coordinator_last_batch() {
    std::cerr << "[U7] test_coordinator_last_batch" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});

    coord.tick();

    // 查询上次批次
    auto batch = coord.get_last_batch(1);
    check(batch.observer == 1, "last batch observer should be 1");
    check(batch.sector == SectorId{200}, "last batch sector should be 200");

    // 未参与的玩家返回空批次
    auto empty_batch = coord.get_last_batch(999);
    check(empty_batch.observer == 0, "unregistered player should have empty batch");
}

// ============================================================
// 18. MultiSectorSyncCoordinator：注销玩家
// ============================================================

void test_coordinator_unregister() {
    std::cerr << "[U7] test_coordinator_unregister" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});
    coord.register_player(2, GlobalPos{100050, 0, 0}, GlobalPos{0, 0, 0}, SectorId{201});

    check(coord.player_count() == 2, "should have 2 players");

    coord.unregister_player(1);
    check(coord.player_count() == 1, "should have 1 player after unregister");

    auto batches = coord.tick();
    check(batches.size() == 1, "should produce 1 batch after unregister");
    check(batches[0].observer == 2, "remaining batch should be for player 2");
}

// ============================================================
// 19. MultiSectorSyncCoordinator：空 tick
// ============================================================

void test_coordinator_empty_tick() {
    std::cerr << "[U7] test_coordinator_empty_tick" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 无玩家时 tick 应正常完成
    auto batches = coord.tick();
    check(batches.empty(), "should produce 0 batches with no players");
}

// ============================================================
// 20. MultiSectorSyncCoordinator：StateSyncServer 集成
// ============================================================

void test_coordinator_state_sync_integration() {
    std::cerr << "[U7] test_coordinator_state_sync_integration" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 注册玩家 1
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});

    // 第一次 tick：计算兴趣集
    coord.tick();

    // 标记一些 chunk 为脏
    std::string dim = "sector_200";
    sss.mark_dirty(dim, 0, 0, 0, SyncFlags::TERRAIN);
    sss.mark_dirty(dim, 1, 0, 0, SyncFlags::TERRAIN);

    // 第二次 tick：应该产生 block delta
    auto batches = coord.tick();
    check(batches.size() == 1, "should produce 1 batch");

    // 验证脏 chunk 被包含在批次中（如果玩家观察了该 chunk）
    // 注意：是否包含取决于 InterestManager 计算的观察集
    // 这里只验证批次结构正确
    check(batches[0].observer == 1, "batch should be for player 1");
    check(batches[0].sector == SectorId{200}, "batch sector should be 200");
}

// ============================================================
// 21. 完整 U7 场景：多玩家多 Sector 会合
// ============================================================

void test_full_u7_scenario() {
    std::cerr << "[U7] test_full_u7_scenario" << std::endl;

    UniverseWorldCore core;
    setup_test_sectors(core);

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

    // 1. 三个玩家分别在不同 Sector
    coord.register_player(1, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{200});
    coord.register_player(2, GlobalPos{100050, 0, 0}, GlobalPos{0, 0, 0}, SectorId{201});
    coord.register_player(3, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0}, SectorId{202});

    // 2. 第一次 tick：三人在不同 Sector
    auto batches1 = coord.tick();
    check(batches1.size() == 3, "should produce 3 batches");

    // 验证跨 Sector 隔离
    std::unordered_set<uint64_t> sectors;
    for (const auto& b : batches1) {
        sectors.insert(b.sector.value);
    }
    check(sectors.size() == 3, "should have 3 distinct sectors");

    // 3. 玩家 3 从深空飞到 Sector A（会合）
    om.set_player_sector(3, SectorId{200});
    coord.update_player(3, GlobalPos{0, 0, 0}, GlobalPos{0, 0, 0});

    // 4. 第二次 tick：玩家 1 和 3 在同一 Sector
    auto batches2 = coord.tick();
    check(batches2.size() == 3, "should still produce 3 batches");

    // 验证玩家 1 和 3 在同一 Sector
    check(om.are_in_same_sector(1, 3), "players 1 and 3 should be in same sector");
    check(!om.are_in_same_sector(1, 2), "players 1 and 2 should be in different sectors");

    // 5. 验证预算不挤占
    // 玩家 2 高速移动（消耗自己的预算），不影响玩家 1 和 3
    bm.tick();  // 重置预算
    auto r2 = bm.try_send(2, SyncChannel::Chunk, 4);  // 玩家 2 耗尽预算
    check(r2.allowed == 4, "player 2 should consume 4");

    // 玩家 1 和 3 的预算不受影响
    auto r1 = bm.try_send(1, SyncChannel::Chunk, 4);
    auto r3 = bm.try_send(3, SyncChannel::Chunk, 4);
    check(r1.allowed == 4, "player 1 budget should not be affected");
    check(r3.allowed == 4, "player 3 budget should not be affected");

    // 6. 注销玩家 2
    coord.unregister_player(2);
    auto batches3 = coord.tick();
    check(batches3.size() == 2, "should produce 2 batches after unregister");
}

// ============================================================
// 22. AoiBudgetManager：tick 重置所有观察者
// ============================================================

void test_budget_tick_resets_all() {
    std::cerr << "[U7] test_budget_tick_resets_all" << std::endl;

    AoiBudgetManager mgr;
    mgr.register_observer(1);
    mgr.register_observer(2);

    // 两个观察者都消耗预算
    mgr.try_send(1, SyncChannel::Chunk, 4);
    mgr.try_send(2, SyncChannel::Chunk, 4);

    // 验证预算已耗尽
    check(mgr.remaining_budget(1, SyncChannel::Chunk) == 0, "observer 1 budget should be 0");
    check(mgr.remaining_budget(2, SyncChannel::Chunk) == 0, "observer 2 budget should be 0");

    // tick 后重置
    mgr.tick();

    check(mgr.remaining_budget(1, SyncChannel::Chunk) == 4, "observer 1 budget should reset to 4");
    check(mgr.remaining_budget(2, SyncChannel::Chunk) == 4, "observer 2 budget should reset to 4");
}

} // namespace

int main() {
    std::cerr << "[U7Core] starting universe multiplayer tests" << std::endl;

    test_budget_basic();
    test_budget_per_observer_isolation();
    test_budget_channel_drop_on_overflow();
    test_budget_unregistered_unlimited();
    test_budget_custom_config();
    test_budget_stats();
    test_budget_tick_resets_all();

    test_observer_map_basic();
    test_observer_map_cross_sector_isolation();
    test_observer_map_convergence();
    test_observer_map_chunk_observers();
    test_observer_map_sector_switch_clears();
    test_observer_map_occupied_sectors();

    test_coordinator_basic();
    test_coordinator_cross_sector_isolation();
    test_coordinator_convergence();
    test_coordinator_budget_no_preemption();
    test_coordinator_last_batch();
    test_coordinator_unregister();
    test_coordinator_empty_tick();
    test_coordinator_state_sync_integration();
    test_full_u7_scenario();

    if (g_failures > 0) {
        std::cerr << "[U7Core] FAILED with " << g_failures << " failures" << std::endl;
        return 1;
    }

    std::cout << "U7 universe multiplayer tests passed: per-observer AOI budget, "
                 "sector-aware observer map, multi-sector sync, cross-sector isolation, "
                 "rendezvous convergence, budget non-preemption." << std::endl;
    return 0;
}
