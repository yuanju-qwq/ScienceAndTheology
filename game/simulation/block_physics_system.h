// Deterministic game-owned terrain physics.
//
// The system owns a bounded, fixed-tick queue for gravity-affected blocks and
// cave-ins. It operates only on current ChunkRegistry terrain values and
// emits value snapshots through block_physics_events.h; it deliberately has
// no WorldData, Godot, EventBus, renderer, or network dependency.

#pragma once

#include "game/simulation/block_physics_events.h"
#include "game/world/defs/gameplay_config.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/data/chunk_registry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace snt::game {

class GameBlockPhysicsSystem final {
public:
    static constexpr uint32_t kMaxChecksPerTick = 128;
    static constexpr int32_t kMaxCollapseSettleDistance = 16;

    GameBlockPhysicsSystem(snt::voxel::ChunkRegistry& chunks,
                           const WorldGenConfigSnapshot& worldgen_config,
                           const GameplayConfig& gameplay_config) noexcept;

    GameBlockPhysicsSystem(const GameBlockPhysicsSystem&) = delete;
    GameBlockPhysicsSystem& operator=(const GameBlockPhysicsSystem&) = delete;

    void set_mutation_sink(IBlockPhysicsMutationSink* sink) noexcept {
        mutation_sink_ = sink;
    }

    // Schedules checks for the changed cell and its immediate neighbors. The
    // mutation must already be committed before this is called.
    void schedule_after_terrain_mutation(std::string_view dimension_id,
                                         int32_t block_x,
                                         int32_t block_y,
                                         int32_t block_z,
                                         uint64_t source_tick);

    // Processes a bounded snapshot of ready checks. Missing chunks are left
    // untouched, keeping unloaded terrain outside the active simulation set.
    void tick(uint64_t current_tick);

    [[nodiscard]] size_t pending_count() const noexcept { return pending_.size(); }

private:
    enum class CheckKind : uint8_t {
        kGravityFall,
        kCollapse,
    };

    struct PendingCheck {
        std::string dimension_id;
        int32_t block_x = 0;
        int32_t block_y = 0;
        int32_t block_z = 0;
        uint64_t target_tick = 0;
        CheckKind kind = CheckKind::kGravityFall;
        uint32_t chain_depth = 0;
    };

    struct GravityStep {
        int32_t dx = 0;
        int32_t dy = -1;
        int32_t dz = 0;
    };

    void schedule_gravity_checks(std::string_view dimension_id,
                                 int32_t block_x,
                                 int32_t block_y,
                                 int32_t block_z,
                                 uint64_t source_tick,
                                 uint32_t chain_depth);
    void schedule_collapse_checks(std::string_view dimension_id,
                                  int32_t block_x,
                                  int32_t block_y,
                                  int32_t block_z,
                                  uint64_t source_tick,
                                  uint32_t chain_depth);
    [[nodiscard]] bool process_gravity_fall(const PendingCheck& check);
    [[nodiscard]] bool process_collapse(const PendingCheck& check,
                                        uint64_t current_tick);
    [[nodiscard]] GravityStep compute_gravity_step(std::string_view dimension_id,
                                                    int32_t block_x,
                                                    int32_t block_y,
                                                    int32_t block_z) const noexcept;
    [[nodiscard]] bool has_support_beam_nearby(std::string_view dimension_id,
                                               int32_t block_x,
                                               int32_t block_y,
                                               int32_t block_z,
                                               int32_t radius) const;
    void emit_terrain_change(std::string_view dimension_id,
                             int32_t block_x,
                             int32_t block_y,
                             int32_t block_z,
                             const snt::voxel::TerrainCell& previous,
                             const snt::voxel::TerrainCell& current) const;

    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    const WorldGenConfigSnapshot* worldgen_config_ = nullptr;
    const GameplayConfig* gameplay_config_ = nullptr;
    IBlockPhysicsMutationSink* mutation_sink_ = nullptr;
    std::deque<PendingCheck> pending_;
};

}  // namespace snt::game
