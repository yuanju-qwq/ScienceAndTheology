// snt_game_world_test -- game-owned ECS benchmark and world smoke-test tool.
//
// Usage:
//   snt_game_world_test ecs_test            # 1000 entities, 1000 frames, report timing
//   snt_game_world_test ecs_test <n>        # n entities, 1000 frames
//   snt_game_world_test ecs_test <n> <frames>
//   snt_game_world_test world_test          # generate chunk (0,0,0), report non-air cells
//
// Measures:
//   1. Entity + component creation time (1000 entities x 2 components).
//   2. System update time (MovementSystem integrates Velocity into Position).
//   3. TerrainGenerator smoke test (chunk (0,0,0) non-air cell count).
//
// MovementSystem: a minimal system that iterates all entities with
// Position + Velocity and integrates velocity into position each tick.
// This is the canonical ECS hot-path benchmark.

#include "ecs/core_components.h"
#include "ecs/system.h"
#include "ecs/system_scheduler.h"
#include "ecs/world.h"

// Game-world includes for the world_test subcommand.
#include "game/world/game_chunk.h"
#include "game/worldgen/world_seed.h"
#include "game/worldgen/terrain_generator.h"
#include "game/worldgen/world_gen_config.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

using namespace snt::ecs;
using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// MovementSystem — integrates Velocity into Position each tick.
//
// This is the hot path: view<Position, Velocity> iterates all matching
// entities and mutates Position in place. EnTT's SoA layout makes this
// cache-friendly.
// ---------------------------------------------------------------------------
class MovementSystem : public System {
public:
    SystemMetadata metadata() const override {
        return {
            "tool.movement",
            SystemThreadAffinity::MainThread,
            {
                {"ecs.position", SystemResourceAccessMode::Write},
                {"ecs.velocity", SystemResourceAccessMode::Read},
            },
        };
    }

    void update(World& world, float dt) override {
        auto view = world.registry().view<Position, Velocity>();
        for (auto [entity, pos, vel] : view.each()) {
            pos.x += static_cast<int32_t>(vel.vx * dt * 60.0f);
            pos.y += static_cast<int32_t>(vel.vy * dt * 60.0f);
            pos.z += static_cast<int32_t>(vel.vz * dt * 60.0f);
        }
    }
};

// ---------------------------------------------------------------------------
// ecs_test: create N entities with Position+Velocity, run frames.
// ---------------------------------------------------------------------------
static int run_ecs_test(int entity_count, int frame_count) {
    std::cout << "=== ECS Benchmark ===" << std::endl;
    std::cout << "Entities: " << entity_count << std::endl;
    std::cout << "Frames:   " << frame_count << std::endl;
    std::cout << "Components per entity: Position + Velocity" << std::endl;
    std::cout << std::endl;

    World world;
    entt::entity first_entity = entt::null;

    // --- Phase 1: entity + component creation ---
    auto t0 = Clock::now();
    for (int i = 0; i < entity_count; ++i) {
        auto e = world.create_entity();
        if (i == 0) first_entity = e;
        world.registry().emplace<Position>(e, Position{i, 0, 0});
        world.registry().emplace<Velocity>(e, Velocity{1.0f, 0.5f, 0.25f});
    }
    auto t1 = Clock::now();
    double create_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << "Entity creation: " << create_ms << " ms ("
              << (create_ms / entity_count) << " ms/entity)" << std::endl;

    // --- Phase 2: scheduler registration ---
    snt::core::JobSystem jobs;
    SystemScheduler scheduler(jobs);
    auto movement = std::make_shared<MovementSystem>();
    if (auto result = scheduler.register_main(std::move(movement)); !result) {
        std::cerr << "Failed to register MovementSystem: "
                  << result.error().format() << std::endl;
        return 1;
    }

    // --- Phase 3: fixed-tick update loop ---
    const float dt = 1.0f / 60.0f;
    auto t2 = Clock::now();
    for (int f = 0; f < frame_count; ++f) {
        if (auto result = scheduler.fixed_tick(world, dt); !result) {
            std::cerr << "Movement fixed tick failed: "
                      << result.error().format() << std::endl;
            return 1;
        }
    }
    auto t3 = Clock::now();
    double update_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "System update:   " << update_ms << " ms ("
              << (update_ms / frame_count) << " ms/frame)" << std::endl;
    std::cout << std::endl;

    // --- Summary ---
    double total_ms = create_ms + update_ms;
    std::cout << "Total:           " << total_ms << " ms" << std::endl;

    // Sanity check: the first created entity starts at x=0 and moves one
    // unit per fixed tick. Do not use a registry view's unspecified order.
    if (first_entity != entt::null) {
        auto& pos = world.registry().get<Position>(first_entity);
        std::cout << "Sanity: entity[0] Position = ("
                  << pos.x << ", " << pos.y << ", " << pos.z << ")"
                  << " (expected x = " << frame_count << ")"
                  << std::endl;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// world_test: generate chunk (0,0,0) via TerrainGenerator and report the
// number of non-air cells. Mirrors the TerrainGenerator smoke test in
// game/tests/test_game_world.cpp but as a standalone command-line tool.
// ---------------------------------------------------------------------------
static int run_world_test() {
    using namespace snt::game;

    std::cout << "=== Game World Smoke Test ===" << std::endl;
    std::cout << "Generating chunk (0,0,0)..." << std::endl;

    // Register semantic terrain keys, then derive compact runtime IDs before
    // the generator receives its immutable snapshot.
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    config->materials.push_back({.key = "snt:air"});
    config->materials.push_back({.key = "snt:stone", .flags = TF_SOLID});
    config->role_keys.air = "snt:air";
    config->role_keys.stone = "snt:stone";
    if (auto result = finalize_world_gen_config(*config); !result) {
        std::cerr << "Failed to finalize world-gen config: "
                  << result.error().format() << std::endl;
        return 1;
    }

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = config->roles.stone;
    config->base_terrain_rules.push_back(rule);

    config->content_hash = hash_world_gen_config(*config);

    WorldSeed seed(12345);
    TerrainGenerator generator(seed, config);
    GameChunk chunk = generator.generate_chunk("overworld", 0, 0, 0);

    // Count non-air cells (material id != 0).
    int64_t non_air = 0;
    int64_t total = 0;
    for (int i = 0; i < chunk.terrain.size_x; ++i) {
        for (int j = 0; j < chunk.terrain.size_y; ++j) {
            for (int k = 0; k < chunk.terrain.size_z; ++k) {
                ++total;
                if (chunk.terrain.cell_at(i, j, k).material != 0) {
                    ++non_air;
                }
            }
        }
    }

    std::cout << "Chunk dimensions: "
              << chunk.terrain.size_x << "x"
              << chunk.terrain.size_y << "x"
              << chunk.terrain.size_z << std::endl;
    std::cout << "Total cells:      " << total << std::endl;
    std::cout << "Non-air cells:    " << non_air << std::endl;

    return 0;
}

// ---------------------------------------------------------------------------
// Main: dispatch subcommands.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: snt_game_world_test <subcommand> [args...]" << std::endl;
        std::cerr << "Subcommands:" << std::endl;
        std::cerr << "  ecs_test [entities] [frames]  - ECS benchmark (default 1000 entities, 1000 frames)" << std::endl;
        std::cerr << "  world_test                    - TerrainGenerator smoke test (chunk 0,0,0 non-air cells)" << std::endl;
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "ecs_test") {
        int entities = (argc > 2) ? std::atoi(argv[2]) : 1000;
        int frames = (argc > 3) ? std::atoi(argv[3]) : 1000;
        if (entities <= 0) entities = 1000;
        if (frames <= 0) frames = 1000;
        return run_ecs_test(entities, frames);
    }

    if (cmd == "world_test") {
        return run_world_test();
    }

    std::cerr << "Unknown subcommand: " << cmd << std::endl;
    return 1;
}
