// MachineCollisionOverlay unit tests.
// Verifies the overlay correctly tracks per-(dimension, cell) machine occupied
// state and produces per-chunk masks aligned with GDChunkHelper::terrain_index
// ordering. Current runtime boundary: docs/项目架构与运行时.md.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "world/machine_collision_overlay.hpp"
#include "world/world_data.hpp"

using namespace science_and_theology;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) {
        return;
    }
    std::cerr << "machine_collision_overlay FAIL: " << message << '\n';
    ++g_failures;
}

// Overlay basic set / clear / query.
bool test_overlay_basic() {
    MachineCollisionOverlay overlay;
    check(overlay.size() == 0, "empty overlay should have zero cells");

    overlay.mark("overworld", 1, 2, 3);
    check(overlay.size() == 1, "single mark should yield size 1");
    check(overlay.is_occupied("overworld", 1, 2, 3),
          "marked cell should report occupied");
    check(!overlay.is_occupied("overworld", 1, 2, 4),
          "unmarked cell should report not occupied");
    check(!overlay.is_occupied("other", 1, 2, 3),
          "same cell in different dimension should not collide");

    overlay.clear("overworld", 1, 2, 3);
    check(overlay.size() == 0, "cleared cell should drop from overlay");
    check(!overlay.is_occupied("overworld", 1, 2, 3),
          "cleared cell should report not occupied");
    return g_failures == 0;
}

// set(false) on an absent cell is a no-op (must not insert a tombstone).
bool test_overlay_clear_absent() {
    MachineCollisionOverlay overlay;
    overlay.set("overworld", 0, 0, 0, false);
    check(overlay.size() == 0, "clearing absent cell must be a no-op");
    overlay.mark("overworld", 0, 0, 0);
    overlay.set("overworld", 0, 0, 0, false);
    check(overlay.size() == 0, "clearing present cell should erase it");
    return g_failures == 0;
}

// Per-dimension clear leaves other dimensions intact.
bool test_overlay_clear_dimension() {
    MachineCollisionOverlay overlay;
    overlay.mark("overworld", 0, 0, 0);
    overlay.mark("overworld", 1, 0, 0);
    overlay.mark("moon", 0, 0, 0);

    const size_t removed = overlay.clear_dimension("overworld");
    check(removed == 2, "clear_dimension should report removed count");
    check(overlay.size() == 1, "moon cell should remain after overworld clear");
    check(overlay.is_occupied("moon", 0, 0, 0),
          "moon cell should still be occupied");
    return g_failures == 0;
}

// Chunk mask layout matches terrain_index((x, y, z), size_x, size_z).
bool test_overlay_chunk_mask_layout() {
    MachineCollisionOverlay overlay;
    // Mark a few cells inside a 4x4x4 chunk at chunk origin (0,0,0).
    overlay.mark("overworld", 0, 0, 0);  // local (0,0,0)
    overlay.mark("overworld", 1, 2, 3);  // local (1,2,3)
    overlay.mark("overworld", 3, 3, 3);  // local (3,3,3)
    // Mark a cell outside this chunk — should not appear in the mask.
    overlay.mark("overworld", 4, 0, 0);
    overlay.mark("other", 0, 0, 0);

    constexpr int32_t kSize = 4;
    auto mask = overlay.get_chunk_mask("overworld", 0, 0, 0,
                                       kSize, kSize, kSize);
    check(mask.size() == static_cast<size_t>(kSize * kSize * kSize),
          "mask should cover full chunk volume");

    const auto idx = [&](int x, int y, int z) -> int {
        return (y * kSize + z) * kSize + x;
    };
    check(mask[idx(0, 0, 0)] == 1, "(0,0,0) should be marked");
    check(mask[idx(1, 2, 3)] == 1, "(1,2,3) should be marked");
    check(mask[idx(3, 3, 3)] == 1, "(3,3,3) should be marked");
    check(mask[idx(2, 0, 0)] == 0, "(2,0,0) should be clear");
    check(mask[idx(4 - 1, 0, 0)] == 0, "out-of-chunk cell should not leak");
    return g_failures == 0;
}

// Chunk mask for a non-origin chunk uses the correct origin offset.
bool test_overlay_chunk_mask_offset() {
    MachineCollisionOverlay overlay;
    constexpr int32_t kSize = 4;
    // Chunk (1, 0, 0) covers cells x in [4..7].
    overlay.mark("overworld", 5, 0, 0);
    auto mask = overlay.get_chunk_mask("overworld", 1, 0, 0,
                                       kSize, kSize, kSize);
    const auto idx = [&](int x, int y, int z) -> int {
        return (y * kSize + z) * kSize + x;
    };
    // Cell (5, 0, 0) → local (1, 0, 0) inside chunk (1, 0, 0).
    check(mask[idx(1, 0, 0)] == 1,
          "cell (5,0,0) should map to local (1,0,0) of chunk (1,0,0)");
    check(mask[idx(0, 0, 0)] == 0, "local (0,0,0) of chunk (1,0,0) should be 0");
    return g_failures == 0;
}

// WorldData integration: set/query routes through the world container.
bool test_world_data_integration() {
    WorldData world;
    world.set_machine_collision("overworld", 7, 8, 9, true);
    check(world.is_machine_collision("overworld", 7, 8, 9),
          "WorldData should report marked cell as machine collision");
    check(!world.is_machine_collision("overworld", 7, 8, 10),
          "WorldData should not report unmarked cell as machine collision");
    check(world.machine_collision_overlay().size() == 1,
          "WorldData overlay size should reflect entries");
    world.set_machine_collision("overworld", 7, 8, 9, false);
    check(!world.is_machine_collision("overworld", 7, 8, 9),
          "Cleared cell should no longer be marked");
    check(world.machine_collision_overlay().size() == 0,
          "Overlay should be empty after clearing the only entry");
    return g_failures == 0;
}

} // namespace

int main() {
    test_overlay_basic();
    test_overlay_clear_absent();
    test_overlay_clear_dimension();
    test_overlay_chunk_mask_layout();
    test_overlay_chunk_mask_offset();
    test_world_data_integration();

    if (g_failures == 0) {
        std::cout << "machine_collision_overlay: all tests passed\n";
        return 0;
    }
    std::cerr << "machine_collision_overlay: " << g_failures << " failure(s)\n";
    return 1;
}
