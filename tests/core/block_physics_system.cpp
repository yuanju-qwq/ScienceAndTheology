#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "core/simulation/block_physics_system.hpp"
#include "core/simulation/event_bus.hpp"
#include "core/world/world_data.hpp"

using namespace science_and_theology;

namespace {

constexpr const char* kDim = "overworld";
constexpr int kSize = ChunkData::kChunkSize;

ChunkData make_empty_chunk() {
    ChunkData chunk;
    chunk.terrain.resize(kSize, kSize, kSize);
    return chunk;
}

void run_tick(BlockPhysicsSystem& physics, WorldData& world, int64_t tick) {
    world.set_current_tick(tick);
    physics.tick_active(ChunkKey{kDim, 0, 0, 0}, 0.05f, nullptr);
}

void test_gravity_event_checks_origin_cell() {
    WorldData world;
    EventBus bus;
    int terrain_events = 0;
    bus.subscribe(GameEventType::TERRAIN_CHANGED,
        [&terrain_events](const GameEvent&) { ++terrain_events; });

    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(
        1, 2, 1,
        static_cast<TerrainMaterial>(3),
        TF_WALKABLE | TF_GRAVITY_FALL);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    BlockPhysicsSystem physics;
    physics.initialize(&world, &bus);

    // Simulates a placement/mutation event at the gravity block itself.
    // The regression this protects: old scheduling checked only neighbors,
    // so the newly placed origin block never fell.
    world.push_physics_event(BlockPhysicsEvent{kDim, 1, 2, 1});

    run_tick(physics, world, 1); // consumes event, schedules checks
    run_tick(physics, world, 2); // processes gravity check
    bus.process_queue();

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(1, 2, 1).material == 0);
    assert(out->terrain.cell_at(1, 1, 1).material == 3);
    assert(terrain_events == 2); // source air + destination moved material
}

void test_collapse_settles_original_block_material() {
    std::srand(1);

    WorldData world;
    world.gameplay_config().collapse_chance_multiplier = 10.0f;

    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(
        2, 4, 2,
        static_cast<TerrainMaterial>(1),
        TF_SOLID | TF_MINEABLE | TF_COLLAPSE_RISK);
    chunk.terrain.set_cell(
        2, 0, 2,
        static_cast<TerrainMaterial>(9),
        TF_SOLID | TF_MINEABLE);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    EventBus bus;
    int terrain_events = 0;
    bus.subscribe(GameEventType::TERRAIN_CHANGED,
        [&terrain_events](const GameEvent&) { ++terrain_events; });

    BlockPhysicsSystem physics;
    physics.initialize(&world, &bus);

    world.push_physics_event(BlockPhysicsEvent{kDim, 2, 4, 2});

    run_tick(physics, world, 1); // consumes event
    run_tick(physics, world, 2); // future collapse check not yet due for all entries
    run_tick(physics, world, 3); // origin collapse check is due
    bus.process_queue();

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(2, 4, 2).material == 0);
    // Cave-in is instant-settle, not per-tick falling: the original material
    // appears at the last empty cell before the support block.
    assert(out->terrain.cell_at(2, 1, 2).material == 1);
    assert(out->terrain.cell_at(2, 0, 2).material == 9);
    assert(terrain_events == 2); // source air + original material at resting cell
}

void test_support_beam_prevents_collapse() {
    std::srand(1);

    WorldData world;
    world.gameplay_config().collapse_chance_multiplier = 10.0f;
    world.gameplay_config().support_beam_radius = 3;

    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(
        3, 3, 3,
        static_cast<TerrainMaterial>(1),
        TF_SOLID | TF_MINEABLE | TF_COLLAPSE_RISK);
    chunk.terrain.set_cell(
        4, 3, 3,
        static_cast<TerrainMaterial>(9),
        TF_SOLID | TF_SUPPORT_BEAM);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    EventBus bus;
    BlockPhysicsSystem physics;
    physics.initialize(&world, &bus);

    world.push_physics_event(BlockPhysicsEvent{kDim, 3, 3, 3});

    run_tick(physics, world, 1);
    run_tick(physics, world, 2);
    run_tick(physics, world, 3);
    bus.process_queue();

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(3, 3, 3).material == 1);
}

} // namespace

int main() {
    test_gravity_event_checks_origin_cell();
    test_collapse_settles_original_block_material();
    test_support_beam_prevents_collapse();
    std::cout << "block_physics_system regression tests passed\n";
    return 0;
}
