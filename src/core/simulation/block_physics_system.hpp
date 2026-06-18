#pragma once

#include <queue>
#include <string>
#include <vector>

#include "simulation_system.hpp"
#include "../world/terrain_data.hpp"
#include "../world/gameplay_config.hpp"

namespace science_and_theology {

// Block physics simulation subsystem.
// Handles gravity-affected blocks (sand, gravel) and cave-in collapse.
//
// Design:
//   - When a block is mined, the caller enqueues a physics check for
//     neighboring blocks via schedule_check().
//   - Each tick, the system processes a budget of pending checks.
//   - Gravity fall: TF_GRAVITY_FALL blocks fall toward the planet center.
//   - Collapse: TF_COLLAPSE_RISK blocks cave-in when unsupported, unless
//     a TF_SUPPORT_BEAM block is within support_beam_radius.
//
// Thread safety: All methods must be called from the main thread only.
// The pending queue is not thread-safe.
class BlockPhysicsSystem : public SimulationSystem {
public:
    BlockPhysicsSystem() = default;

    SIMULATION_SYSTEM_NAME(BlockPhysicsSystem, "BlockPhysicsSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    // Runs after DayNight (priority 0) and before machines (priority 2)
    // so that block physics settles before machine tick reads terrain state.
    int priority() const override { return 1; }

    // --- Scheduled block checks ---

    // A pending block physics check.
    struct PendingCheck {
        std::string dimension_id;
        int block_x = 0;
        int block_y = 0;
        int block_z = 0;
        // Tick at which this check should be executed (delayed scheduling).
        int64_t target_tick = 0;
        // Type of check: 0 = gravity fall, 1 = collapse risk.
        int check_type = 0;
    };

    // Enqueue a block physics check. The check will be processed at
    // target_tick (or later if the budget is exhausted).
    void schedule_check(const PendingCheck& check);

    // Enqueue gravity fall checks for blocks above the mined position.
    // "Above" means in the anti-gravity direction (away from planet center).
    void schedule_gravity_fall_after_mine(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z,
        int64_t current_tick);

    // Enqueue collapse checks for blocks around the mined position.
    void schedule_collapse_after_mine(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z,
        int64_t current_tick);

    // --- Processing ---

    // Maximum number of pending checks to process per tick.
    // Prevents frame spikes from large cave-ins.
    static constexpr int kMaxChecksPerTick = 128;

    // Process pending checks up to the budget.
    void process_pending(int64_t current_tick);

    // --- Gravity direction ---

    // Compute the gravity step direction for a block position.
    // Returns the voxel-space step (e.g., (0,-1,0) for flat world,
    // or the axis-aligned step toward planet center for spherical world).
    // Returns (0, 0, 0) if no gravity source is found.
    struct GravityStep {
        int dx = 0;
        int dy = 0;
        int dz = 0;
    };

    GravityStep compute_gravity_step(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z) const;

    // --- Per-block physics ---

    // Process a gravity fall check for a single block.
    // Returns true if the block fell.
    bool process_gravity_fall(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z);

    // Process a collapse check for a single block.
    // Returns true if the block collapsed.
    bool process_collapse(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z);

    // Check if a support beam exists within radius of the given position.
    bool has_support_beam_nearby(
        const std::string& dimension_id,
        int block_x, int block_y, int block_z,
        int radius) const;

    // Returns the number of pending checks.
    size_t pending_count() const { return pending_.size(); }

private:
    std::queue<PendingCheck> pending_;
};

} // namespace science_and_theology
