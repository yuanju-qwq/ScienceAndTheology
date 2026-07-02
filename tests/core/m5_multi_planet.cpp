// M5: Multi-planet concurrent tests.
// Design: docs/多人游戏系统设计.md §3.6 多星球并发, §6.2 M5
//
// Tests:
//   1. ServerCore::logged_in_player_handles() — enumerate actual IDs (with gaps)
//   2. TickSystem::get_player_dimension() — query player's current dimension
//   3. StateSyncServer::compute_deltas_batch() — multiple observers same dim
//      all receive deltas (dirty flags not cleared after first observer)
//   4. Per-dimension delta isolation — players on different planets only
//      receive their own planet's deltas
//   5. Planet switching — player switches dimension, gets new planet's
//      deltas, stops getting old planet's
//   6. Non-sequential player IDs — delta production works with gaps

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "core/player/player_handle.hpp"
#include "core/simulation/tick_system.hpp"
#include "core/world/world_data.hpp"
#include "server/server_core.hpp"
#include "server/network_client.hpp"

using namespace science_and_theology;
using namespace science_and_theology::server;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (cond) return;
    std::cerr << "  FAIL: " << msg << '\n';
    ++g_failures;
}

// Create a chunk with ACTIVE state at the given coordinate.
ChunkData make_active_chunk(int x, int y, int z) {
    ChunkData chunk;
    chunk.chunk_x = x;
    chunk.chunk_y = y;
    chunk.chunk_z = z;
    chunk.state = ChunkState::ACTIVE;
    chunk.terrain.resize(
        ChunkData::kChunkSize,
        ChunkData::kChunkSize,
        ChunkData::kChunkSize);
    return chunk;
}

// Helper: does a vector contain a specific value?
template <typename T>
bool contains(const std::vector<T>& vec, const T& val) {
    for (const auto& v : vec) {
        if (v == val) return true;
    }
    return false;
}

// --- Test 1: ServerCore::logged_in_player_handles() ---
// Verifies that the API returns actual logged-in player IDs, including
// when there are gaps in the ID space (player 2 disconnects, leaving
// players 1 and 3).
bool test_server_player_handles() {
    std::cerr << "[M5] test_server_player_handles" << '\n';

    const uint16_t tcp_port = 18930;
    const uint16_t udp_port = 18931;

    ServerCore server;
    std::vector<uint64_t> login_order;

    server.set_login_handler(
        [&login_order](uint64_t pid, const std::vector<uint8_t>&,
                       std::string&) -> bool {
            login_order.push_back(pid);
            return true;
        });
    server.set_disconnect_handler(
        [&login_order](uint64_t pid) {
            // Remove from login_order on disconnect.
            auto it = std::find(login_order.begin(), login_order.end(), pid);
            if (it != login_order.end()) login_order.erase(it);
        });

    check(server.start(tcp_port, udp_port), "server started");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll(0);

    // Connect 3 clients.
    NetworkClient c1, c2, c3;
    std::atomic<ClientState> s1{ClientState::DISCONNECTED};
    std::atomic<ClientState> s2{ClientState::DISCONNECTED};
    std::atomic<ClientState> s3{ClientState::DISCONNECTED};
    c1.set_state_handler([&s1](ClientState s) { s1 = s; });
    c2.set_state_handler([&s2](ClientState s) { s2 = s; });
    c3.set_state_handler([&s3](ClientState s) { s3 = s; });

    check(c1.connect("127.0.0.1", tcp_port), "c1 connect");
    check(c2.connect("127.0.0.1", tcp_port), "c2 connect");
    check(c3.connect("127.0.0.1", tcp_port), "c3 connect");

    // Poll until all connected.
    for (int i = 0; i < 100; ++i) {
        server.poll(0);
        c1.poll(0); c2.poll(0); c3.poll(0);
        if (c1.is_connected() && c2.is_connected() && c3.is_connected()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(c1.is_connected(), "c1 connected");
    check(c2.is_connected(), "c2 connected");
    check(c3.is_connected(), "c3 connected");

    // All 3 should be logged in.
    auto ids = server.logged_in_player_handles();
    check(ids.size() == 3, "3 logged-in player IDs");
    check(contains(ids, c1.player_handle()), "ids contains c1");
    check(contains(ids, c2.player_handle()), "ids contains c2");
    check(contains(ids, c3.player_handle()), "ids contains c3");

    // Disconnect player 2, leaving a gap in the ID space.
    const uint64_t p2_id = c2.player_handle();
    c2.disconnect();
    for (int i = 0; i < 100; ++i) {
        server.poll(0);
        c1.poll(0); c3.poll(0);
        auto current_ids = server.logged_in_player_handles();
        if (current_ids.size() == 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ids = server.logged_in_player_handles();
    check(ids.size() == 2, "2 logged-in after c2 disconnect");
    check(!contains(ids, p2_id), "p2_id gone from list");
    check(contains(ids, c1.player_handle()), "c1 still present");
    check(contains(ids, c3.player_handle()), "c3 still present");

    // Verify IDs are NOT sequential 1..N (p2's slot is gone).
    // The key point: iterating 1..player_count() would miss c3 if c3's
    // ID > 2 and we only iterated 1..2.
    bool has_gap = false;
    for (uint64_t id : ids) {
        if (id != c1.player_handle() && id != c3.player_handle()) {
            has_gap = true;
            break;
        }
    }
    check(!has_gap, "no unexpected IDs");

    c1.disconnect();
    c3.disconnect();
    server.stop();
    return g_failures == 0;
}

// --- Test 2: TickSystem::get_player_dimension() ---
bool test_tick_player_dimension() {
    std::cerr << "[M5] test_tick_player_dimension" << '\n';

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));
    world.set_chunk("mars", 0, 0, 0, make_active_chunk(0, 0, 0));

    TickSystem ts(&world);

    // Initially no players → empty dimension.
    check(ts.get_player_dimension(1).empty(), "unregistered player → empty");

    // Register player 1 in overworld.
    ts.add_player_chunk(1, "overworld", 0, 0, 0);
    check(ts.get_player_dimension(1) == "overworld", "p1 in overworld");

    // Register player 2 in mars.
    ts.add_player_chunk(2, "mars", 0, 0, 0);
    check(ts.get_player_dimension(2) == "mars", "p2 in mars");

    // Player 1 switches to mars.
    ts.add_player_chunk(1, "mars", 0, 0, 0);
    check(ts.get_player_dimension(1) == "mars", "p1 switched to mars");

    // Remove player 2 → empty.
    ts.remove_player_chunk(2);
    check(ts.get_player_dimension(2).empty(), "p2 removed → empty");

    return g_failures == 0;
}

// --- Test 3: compute_deltas_batch() — multiple observers same dimension ---
// Verifies that when two players are in the same dimension, BOTH receive
// the delta. The single-observer compute_delta_for would clear dirty
// flags after the first observer, starving the second.
bool test_batch_deltas_same_dimension() {
    std::cerr << "[M5] test_batch_deltas_same_dimension" << '\n';

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));
    world.set_chunk("overworld", 1, 0, 0, make_active_chunk(1, 0, 0));

    TickSystem ts(&world);
    auto* sync = ts.state_sync();

    // Mark chunks dirty.
    sync->mark_dirty("overworld", 0, 0, 0, SyncFlags::TERRAIN);
    sync->mark_dirty("overworld", 1, 0, 0, SyncFlags::ENTITY);

    // Two observers, both see the same chunks.
    std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>> views;
    views.push_back({1, {ChunkKey("overworld", 0, 0, 0),
                         ChunkKey("overworld", 1, 0, 0)}});
    views.push_back({2, {ChunkKey("overworld", 0, 0, 0),
                         ChunkKey("overworld", 1, 0, 0)}});

    auto results = sync->compute_deltas_batch(views);
    check(results.size() == 2, "batch returned 2 results");

    // Both observers should have the same delta.
    check(results[0].first == 1, "result 0 is player 1");
    check(results[1].first == 2, "result 1 is player 2");

    check(results[0].second.chunks_modified.size() == 2,
          "player 1 delta has 2 chunks");
    check(results[1].second.chunks_modified.size() == 2,
          "player 2 delta has 2 chunks");

    check((results[0].second.flags & SyncFlags::TERRAIN) != SyncFlags::NONE,
          "player 1 delta has TERRAIN");
    check((results[1].second.flags & SyncFlags::TERRAIN) != SyncFlags::NONE,
          "player 2 delta has TERRAIN");

    // After batch, dirty flags should be cleared.
    check(!sync->is_dirty(ChunkKey("overworld", 0, 0, 0)),
          "chunk 0 cleared after batch");
    check(!sync->is_dirty(ChunkKey("overworld", 1, 0, 0)),
          "chunk 1 cleared after batch");

    return g_failures == 0;
}

// --- Test 4: Per-dimension delta isolation ---
// Players on different planets only receive their own planet's deltas.
bool test_per_dimension_isolation() {
    std::cerr << "[M5] test_per_dimension_isolation" << '\n';

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));
    world.set_chunk("mars", 0, 0, 0, make_active_chunk(0, 0, 0));

    TickSystem ts(&world);
    auto* sync = ts.state_sync();

    // Mark chunks dirty in both dimensions.
    sync->mark_dirty("overworld", 0, 0, 0, SyncFlags::TERRAIN);
    sync->mark_dirty("mars", 0, 0, 0, SyncFlags::ENTITY);

    // Player 1 in overworld, Player 2 in mars.
    // Build observer views filtered by dimension (simulating what
    // GDNetworkServer::on_produce_deltas does).
    std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>> views;
    views.push_back({1, {ChunkKey("overworld", 0, 0, 0)}});
    views.push_back({2, {ChunkKey("mars", 0, 0, 0)}});

    auto results = sync->compute_deltas_batch(views);
    check(results.size() == 2, "batch returned 2 results");

    // Player 1 should only see overworld chunk.
    check(results[0].second.chunks_modified.size() == 1,
          "player 1 sees 1 chunk");
    check(results[0].second.chunks_modified[0].dimension_id == "overworld",
          "player 1 chunk is overworld");
    check((results[0].second.flags & SyncFlags::TERRAIN) != SyncFlags::NONE,
          "player 1 delta has TERRAIN");
    check((results[0].second.flags & SyncFlags::ENTITY) == SyncFlags::NONE,
          "player 1 delta has NO ENTITY (mars-only)");

    // Player 2 should only see mars chunk.
    check(results[1].second.chunks_modified.size() == 1,
          "player 2 sees 1 chunk");
    check(results[1].second.chunks_modified[0].dimension_id == "mars",
          "player 2 chunk is mars");
    check((results[1].second.flags & SyncFlags::ENTITY) != SyncFlags::NONE,
          "player 2 delta has ENTITY");
    check((results[1].second.flags & SyncFlags::TERRAIN) == SyncFlags::NONE,
          "player 2 delta has NO TERRAIN (overworld-only)");

    return g_failures == 0;
}

// --- Test 5: Planet switching ---
// Player switches dimension. After switching, the player should receive
// deltas for the new planet, not the old one.
bool test_planet_switching() {
    std::cerr << "[M5] test_planet_switching" << '\n';

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));
    world.set_chunk("mars", 0, 0, 0, make_active_chunk(0, 0, 0));

    TickSystem ts(&world);
    ts.set_active_radius(2);
    auto* sync = ts.state_sync();

    // Player 1 starts in overworld.
    ts.add_player_chunk(1, "overworld", 0, 0, 0);
    ts.tick(0.05f);
    check(ts.get_player_dimension(1) == "overworld", "p1 starts in overworld");

    // Mark overworld chunk dirty.
    sync->mark_dirty("overworld", 0, 0, 0, SyncFlags::TERRAIN);

    // Player 1 should receive overworld delta.
    auto results = sync->compute_deltas_batch(
        {{1, {ChunkKey("overworld", 0, 0, 0)}}});
    check(results.size() == 1, "first batch 1 result");
    check(results[0].second.chunks_modified.size() == 1,
          "p1 got overworld delta");

    // Player 1 switches to mars.
    ts.add_player_chunk(1, "mars", 0, 0, 0);
    ts.tick(0.05f);
    check(ts.get_player_dimension(1) == "mars", "p1 switched to mars");

    // Mark mars chunk dirty.
    sync->mark_dirty("mars", 0, 0, 0, SyncFlags::ENTITY);

    // Now player 1 should receive mars delta, NOT overworld.
    // Simulate the dimension filtering that GDNetworkServer does.
    std::string p1_dim = ts.get_player_dimension(1);
    check(p1_dim == "mars", "p1 dimension is mars");

    // Filter dirty chunks by player's dimension.
    auto dirty = sync->dirty_chunks();
    std::vector<ChunkKey> p1_view;
    for (const auto& key : dirty) {
        if (key.dimension_id == p1_dim) {
            p1_view.push_back(key);
        }
    }
    check(p1_view.size() == 1, "p1 filtered view has 1 chunk");
    check(p1_view[0].dimension_id == "mars", "p1 view chunk is mars");

    results = sync->compute_deltas_batch({{1, p1_view}});
    check(results.size() == 1, "second batch 1 result");
    check(results[0].second.chunks_modified.size() == 1,
          "p1 got mars delta after switch");
    check(results[0].second.chunks_modified[0].dimension_id == "mars",
          "p1 delta chunk is mars");

    return g_failures == 0;
}

// --- Test 6: Non-sequential player IDs ---
// Verifies that delta production works correctly when player IDs have
// gaps (e.g., player 1 and player 3, with player 2 disconnected).
bool test_non_sequential_ids() {
    std::cerr << "[M5] test_non_sequential_ids" << '\n';

    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));

    TickSystem ts(&world);
    auto* sync = ts.state_sync();

    // Register players 1 and 3 (skipping 2, simulating a disconnect).
    ts.add_player_chunk(1, "overworld", 0, 0, 0);
    ts.add_player_chunk(3, "overworld", 0, 0, 0);
    ts.tick(0.05f);

    check(ts.player_count() == 2, "2 players registered");
    check(ts.get_player_dimension(1) == "overworld", "p1 in overworld");
    check(ts.get_player_dimension(3) == "overworld", "p3 in overworld");

    // Mark chunk dirty.
    sync->mark_dirty("overworld", 0, 0, 0, SyncFlags::TERRAIN);

    // Build observer views for players 1 and 3 (NOT 1 and 2).
    std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>> views;
    views.push_back({1, {ChunkKey("overworld", 0, 0, 0)}});
    views.push_back({3, {ChunkKey("overworld", 0, 0, 0)}});

    auto results = sync->compute_deltas_batch(views);
    check(results.size() == 2, "batch returned 2 results");
    check(results[0].first == 1, "result 0 is player 1");
    check(results[1].first == 3, "result 1 is player 3");

    // Both should receive the delta.
    check(results[0].second.chunks_modified.size() == 1,
          "player 1 got delta");
    check(results[1].second.chunks_modified.size() == 1,
          "player 3 got delta");

    return g_failures == 0;
}

} // namespace

int main() {
    std::cerr << "=== M5 Multi-planet concurrent tests ===" << '\n';
    test_server_player_handles();
    test_tick_player_dimension();
    test_batch_deltas_same_dimension();
    test_per_dimension_isolation();
    test_planet_switching();
    test_non_sequential_ids();

    if (g_failures == 0) {
        std::cerr << "=== M5 PASSED ===" << '\n';
        return 0;
    }
    std::cerr << "=== M5 FAILED with " << g_failures << " failures ===" << '\n';
    return 1;
}
