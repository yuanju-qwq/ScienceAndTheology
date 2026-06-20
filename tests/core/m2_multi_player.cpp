// M2: Multi-player local validation test (no network).
// Design: docs/多人游戏系统设计.md §6.2 M2, §6.3 M1 验收标准
//
// Validates that the shared refactoring (M1) correctly supports multiple
// players in-process:
//   1. PlayerManager register/unregister/routing by id
//   2. TickSystem multi-player active set = union of player neighborhoods
//   3. Sleep tier classification by min distance to ANY player
//   4. Players in different dimensions do not cross-contribute
//   5. StateSyncServer per-observer API
//   6. Single-player mode (one player, id=1) behaves identically to legacy

#include <iostream>
#include <string>
#include <vector>

#include "core/player/player_manager.hpp"
#include "core/simulation/tick_system.hpp"
#include "core/world/world_data.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (cond) return;
    std::cerr << "  FAIL: " << msg << '\n';
    ++g_failures;
}

// Helper: does a ChunkKey vector contain a specific coordinate?
bool contains_chunk(const std::vector<ChunkKey>& chunks,
                    const std::string& dim, int x, int y, int z) {
    for (const auto& k : chunks) {
        if (k.dimension_id == dim && k.chunk_x == x &&
            k.chunk_y == y && k.chunk_z == z) {
            return true;
        }
    }
    return false;
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

// --- Test 1: PlayerManager basic registry ---
bool test_player_manager_basic() {
    std::cerr << "[M2] test_player_manager_basic" << '\n';
    PlayerManager pm;

    // Single-player id = 1.
    check(pm.register_player(kSinglePlayerId), "register single-player id");
    check(pm.has_player(kSinglePlayerId), "has single player");
    check(pm.player_count() == 1, "count == 1");
    check(pm.get_player(kSinglePlayerId) != nullptr, "get_player non-null");
    check(pm.get_player(kSinglePlayerId)->id == kSinglePlayerId, "id matches");

    // Reject invalid id.
    check(!pm.register_player(kInvalidPlayerId), "reject invalid id 0");
    // Reject duplicate.
    check(!pm.register_player(kSinglePlayerId), "reject duplicate id");

    // Multi-player: register id 2, 3.
    check(pm.register_player(2), "register player 2");
    check(pm.register_player(3), "register player 3");
    check(pm.player_count() == 3, "count == 3");

    auto ids = pm.all_ids();
    check(ids.size() == 3, "all_ids size 3");

    // Unregister.
    check(pm.unregister_player(2), "unregister player 2");
    check(!pm.has_player(2), "player 2 gone");
    check(pm.player_count() == 2, "count == 2 after unregister");
    check(!pm.unregister_player(999), "unregister unknown fails");

    // set_player_chunk updates position.
    check(pm.set_player_chunk(kSinglePlayerId, "overworld", 5, -3, 2),
          "set_player_chunk ok");
    auto* st = pm.get_player(kSinglePlayerId);
    check(st != nullptr, "state non-null");
    check(st->current_dimension == "overworld", "dim matches");
    check(st->current_cx == 5 && st->current_cy == -3 && st->current_cz == 2,
          "chunk coords match");

    pm.clear();
    check(pm.player_count() == 0, "clear ok");
    return g_failures == 0;
}

// --- Test 2: TickSystem single-player active set ---
bool test_tick_single_player() {
    std::cerr << "[M2] test_tick_single_player" << '\n';
    WorldData world;
    // Place chunks in a line so we can reason about distance.
    for (int x = -8; x <= 8; ++x) {
        world.set_chunk("overworld", x, 0, 0, make_active_chunk(x, 0, 0));
    }

    TickSystem ts(&world);
    ts.set_active_radius(4);

    // Single player at origin.
    ts.add_player_chunk(kSinglePlayerId, "overworld", 0, 0, 0);
    ts.tick(0.05f);  // triggers rebuild_chunk_sets

    // Active chunks: |x| <= 4 → x in [-4, 4] = 9 chunks.
    const auto& active = ts.active_chunks();
    check(active.size() == 9, "single-player active count == 9 (radius 4)");
    check(contains_chunk(active, "overworld", 0, 0, 0), "origin active");
    check(contains_chunk(active, "overworld", 4, 0, 0), "edge +4 active");
    check(contains_chunk(active, "overworld", -4, 0, 0), "edge -4 active");
    check(!contains_chunk(active, "overworld", 5, 0, 0), "+5 not active");

    // Sleep tier classification.
    check(ts.classify_sleep_tier(5, 0, 0) == SleepTier::NEAR, "x=5 NEAR");
    check(ts.classify_sleep_tier(9, 0, 0) == SleepTier::MID, "x=9 MID");
    check(ts.classify_sleep_tier(13, 0, 0) == SleepTier::FAR, "x=13 FAR");

    return g_failures == 0;
}

// --- Test 3: TickSystem multi-player active set = union ---
bool test_tick_multi_player_union() {
    std::cerr << "[M2] test_tick_multi_player_union" << '\n';
    WorldData world;
    for (int x = -20; x <= 20; ++x) {
        world.set_chunk("overworld", x, 0, 0, make_active_chunk(x, 0, 0));
    }

    TickSystem ts(&world);
    ts.set_active_radius(4);

    // Player 1 at x=-10, Player 2 at x=+10.
    ts.add_player_chunk(1, "overworld", -10, 0, 0);
    ts.add_player_chunk(2, "overworld", 10, 0, 0);
    ts.tick(0.05f);

    const auto& active = ts.active_chunks();
    // Player 1 active: [-14, -6] = 9 chunks.
    // Player 2 active: [6, 14]   = 9 chunks.
    // Union = 18 chunks (no overlap since gap is 20 > 2*radius).
    check(active.size() == 18, "two-player union active count == 18");
    check(contains_chunk(active, "overworld", -10, 0, 0), "p1 origin active");
    check(contains_chunk(active, "overworld", 10, 0, 0), "p2 origin active");
    check(contains_chunk(active, "overworld", -14, 0, 0), "p1 edge active");
    check(contains_chunk(active, "overworld", 14, 0, 0), "p2 edge active");
    // Gap between players should NOT be active.
    check(!contains_chunk(active, "overworld", 0, 0, 0), "midpoint not active");
    check(!contains_chunk(active, "overworld", 5, 0, 0), "x=5 not active");

    // Sleep tier: x=0 is distance 10 from both → MID (radius*2=8 < 10 <= 12=radius*3).
    check(ts.classify_sleep_tier(0, 0, 0) == SleepTier::MID,
          "midpoint tier MID (min dist 10)");

    // Remove player 2 → only player 1's neighborhood remains.
    ts.remove_player_chunk(2);
    ts.tick(0.05f);
    const auto& active_after = ts.active_chunks();
    check(active_after.size() == 9, "after remove p2, active == 9");
    check(contains_chunk(active_after, "overworld", -10, 0, 0), "p1 still active");
    check(!contains_chunk(active_after, "overworld", 10, 0, 0), "p2 gone");

    return g_failures == 0;
}

// --- Test 4: Players in different dimensions don't cross-contribute ---
bool test_tick_multi_dimension() {
    std::cerr << "[M2] test_tick_multi_dimension" << '\n';
    WorldData world;
    // Chunks in "overworld" and "nether" at the same coordinates.
    for (int x = -5; x <= 5; ++x) {
        world.set_chunk("overworld", x, 0, 0, make_active_chunk(x, 0, 0));
        world.set_chunk("nether", x, 0, 0, make_active_chunk(x, 0, 0));
    }

    TickSystem ts(&world);
    ts.set_active_radius(4);

    // Player 1 in overworld at origin, Player 2 in nether at origin.
    ts.add_player_chunk(1, "overworld", 0, 0, 0);
    ts.add_player_chunk(2, "nether", 0, 0, 0);
    ts.tick(0.05f);

    const auto& active = ts.active_chunks();
    // 9 in overworld + 9 in nether = 18.
    check(active.size() == 18, "cross-dimension active == 18");
    check(contains_chunk(active, "overworld", 0, 0, 0), "ow origin active");
    check(contains_chunk(active, "nether", 0, 0, 0), "nether origin active");
    // Nether chunk far from player 2 but close in coordinate to player 1
    // should NOT be active (different dimension).
    check(!contains_chunk(active, "nether", 5, 0, 0), "nether x=5 not active");

    return g_failures == 0;
}

// --- Test 5: StateSyncServer per-observer API ---
bool test_state_sync_observers() {
    std::cerr << "[M2] test_state_sync_observers" << '\n';
    WorldData world;
    world.set_chunk("overworld", 0, 0, 0, make_active_chunk(0, 0, 0));
    world.set_chunk("overworld", 1, 0, 0, make_active_chunk(1, 0, 0));

    TickSystem ts(&world);
    auto* sync = ts.state_sync();

    // Register two observers.
    sync->register_observer(1);
    sync->register_observer(2);
    check(sync->has_observer(1), "observer 1 registered");
    check(sync->has_observer(2), "observer 2 registered");
    check(!sync->has_observer(999), "observer 999 not registered");

    // Mark chunks dirty.
    sync->mark_dirty("overworld", 0, 0, 0, SyncFlags::TERRAIN);
    sync->mark_dirty("overworld", 1, 0, 0, SyncFlags::ENTITY);
    check(sync->is_dirty(ChunkKey("overworld", 0, 0, 0)), "chunk 0 dirty");
    check(sync->is_dirty(ChunkKey("overworld", 1, 0, 0)), "chunk 1 dirty");

    // Observer 1 sees both chunks.
    std::vector<ChunkKey> view1 = {
        ChunkKey("overworld", 0, 0, 0),
        ChunkKey("overworld", 1, 0, 0),
    };
    auto delta1 = sync->compute_delta_for(1, view1);
    check((delta1.flags & SyncFlags::TERRAIN) != SyncFlags::NONE,
          "delta1 has TERRAIN flag");
    check((delta1.flags & SyncFlags::ENTITY) != SyncFlags::NONE,
          "delta1 has ENTITY flag");
    check(delta1.chunks_modified.size() == 2, "delta1 modified 2 chunks");

    // After compute_delta, dirty flags are cleared (shared map in M1).
    check(!sync->is_dirty(ChunkKey("overworld", 0, 0, 0)),
          "chunk 0 cleared after delta");

    // Unregister observer.
    sync->unregister_observer(2);
    check(!sync->has_observer(2), "observer 2 unregistered");

    return g_failures == 0;
}

// --- Test 6: Single-player mode degenerates to legacy behavior ---
bool test_single_player_degenerates() {
    std::cerr << "[M2] test_single_player_degenerates" << '\n';
    WorldData world;
    for (int x = -3; x <= 3; ++x) {
        world.set_chunk("overworld", x, 0, 0, make_active_chunk(x, 0, 0));
    }

    TickSystem ts(&world);
    ts.set_active_radius(2);

    // Exactly one player with kSinglePlayerId.
    ts.add_player_chunk(kSinglePlayerId, "overworld", 0, 0, 0);
    ts.tick(0.05f);

    check(ts.player_count() == 1, "single player count == 1");
    const auto& active = ts.active_chunks();
    // radius 2 → [-2, 2] = 5 chunks.
    check(active.size() == 5, "single-player radius-2 active == 5");

    // Clearing all players → no active chunks.
    ts.clear_player_chunks();
    ts.tick(0.05f);
    check(ts.active_chunks().empty(), "no players → no active chunks");

    return g_failures == 0;
}

} // namespace

int main() {
    std::cerr << "=== M2 Multi-player local validation ===" << '\n';
    test_player_manager_basic();
    test_tick_single_player();
    test_tick_multi_player_union();
    test_tick_multi_dimension();
    test_state_sync_observers();
    test_single_player_degenerates();

    if (g_failures == 0) {
        std::cerr << "=== M2 PASSED ===" << '\n';
        return 0;
    }
    std::cerr << "=== M2 FAILED with " << g_failures << " failures ===" << '\n';
    return 1;
}
