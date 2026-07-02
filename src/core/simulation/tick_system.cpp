#include "tick_system.hpp"
#include "ecosystem_system.hpp"
#include "season_system.hpp"
#include "../world/world_data.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <future>
#include <sstream>
#include <thread>

namespace science_and_theology {

TickSystem::TickSystem(WorldData* world_data)
    : world_data_(world_data)
    , event_bus_(std::make_unique<EventBus>())
    , state_sync_(std::make_unique<StateSyncServer>()) {
    state_sync_->set_world_data(world_data);
}

TickSystem::~TickSystem() {
    for (auto& sys : subsystems_) {
        sys->shutdown();
    }
}

void TickSystem::register_subsystem(std::unique_ptr<SimulationSystem> subsystem) {
    subsystem->initialize(world_data_, event_bus_.get());
    subsystems_.push_back(std::move(subsystem));
    std::sort(subsystems_.begin(), subsystems_.end(),
        [](const auto& a, const auto& b) {
            return a->priority() < b->priority();
        });
}

void TickSystem::tick(float delta) {
    ++tick_counter_;
    const auto tick_started = std::chrono::steady_clock::now();
    profiler_.begin_tick(tick_counter_);

    // Publish the current tick to WorldData so subsystems can read it.
    if (world_data_) {
        world_data_->set_current_tick(tick_counter_);
    }

    {
        ScopedTickProfile profile(&profiler_, "TickSystem::rebuild_chunk_sets");
        rebuild_chunk_sets();
    }

    // Build cross-system context once per tick.
    TickContext ctx;
    {
        ScopedTickProfile profile(&profiler_, "TickSystem::build_tick_context");
        ctx = build_tick_context();
    }
    const TickContext* ctx_ptr = &ctx;

    // --- Phase 1: ACTIVE chunks — full simulation ---

    if (parallel_enabled_) {
        // Group subsystems by priority and run each group.
        run_chunks_by_priority_groups(
            active_chunks_, delta, true, ctx_ptr, "sim.active");
    } else {
        // Sequential: iterate subsystems in priority order.
        for (auto& sys : subsystems_) {
            ScopedTickProfile profile(
                &profiler_, subsystem_profile_name("sim.active", *sys));
            for (const auto& key : active_chunks_) {
                sys->tick_active(key, delta, ctx_ptr);
            }
        }
    }

    // --- Phase 2: SLEEPING chunks — tiered low-frequency simulation ---

    // NEAR tier.
    if (tick_counter_ % sleep_near_interval_ == 0) {
        const float sleep_delta = delta * sleep_near_interval_;
        if (parallel_enabled_) {
            run_chunks_by_priority_groups(
                sleep_near_chunks_, sleep_delta, false, ctx_ptr, "sim.sleep_near");
        } else {
            for (auto& sys : subsystems_) {
                ScopedTickProfile profile(
                    &profiler_, subsystem_profile_name("sim.sleep_near", *sys));
                for (const auto& key : sleep_near_chunks_) {
                    if (should_tick_sleeping(key.chunk_x, key.chunk_y,
                                             key.chunk_z, SleepTier::NEAR)) {
                        sys->tick_sleeping(key, sleep_delta, ctx_ptr);
                    }
                }
            }
        }
    }

    // MID tier.
    if (tick_counter_ % sleep_mid_interval_ == 0) {
        const float sleep_delta = delta * sleep_mid_interval_;
        if (parallel_enabled_) {
            run_chunks_by_priority_groups(
                sleep_mid_chunks_, sleep_delta, false, ctx_ptr, "sim.sleep_mid");
        } else {
            for (auto& sys : subsystems_) {
                ScopedTickProfile profile(
                    &profiler_, subsystem_profile_name("sim.sleep_mid", *sys));
                for (const auto& key : sleep_mid_chunks_) {
                    if (should_tick_sleeping(key.chunk_x, key.chunk_y,
                                             key.chunk_z, SleepTier::MID)) {
                        sys->tick_sleeping(key, sleep_delta, ctx_ptr);
                    }
                }
            }
        }
    }

    // FAR tier.
    if (tick_counter_ % sleep_far_interval_ == 0) {
        const float sleep_delta = delta * sleep_far_interval_;
        if (parallel_enabled_) {
            run_chunks_by_priority_groups(
                sleep_far_chunks_, sleep_delta, false, ctx_ptr, "sim.sleep_far");
        } else {
            for (auto& sys : subsystems_) {
                ScopedTickProfile profile(
                    &profiler_, subsystem_profile_name("sim.sleep_far", *sys));
                for (const auto& key : sleep_far_chunks_) {
                    if (should_tick_sleeping(key.chunk_x, key.chunk_y,
                                             key.chunk_z, SleepTier::FAR)) {
                        sys->tick_sleeping(key, sleep_delta, ctx_ptr);
                    }
                }
            }
        }
    }

    // Phase 3: Drain deferred events.
    {
        ScopedTickProfile profile(&profiler_, "TickSystem::event_bus.process_queue");
        event_bus_->process_queue();
    }

    // Phase 4: Advance state sync tick.
    {
        ScopedTickProfile profile(&profiler_, "TickSystem::state_sync.set_tick_counter");
        state_sync_->set_tick_counter(tick_counter_);
    }

    const auto tick_finished = std::chrono::steady_clock::now();
    const double total_ms = std::chrono::duration<double, std::milli>(
        tick_finished - tick_started).count();
    profiler_.end_tick(total_ms);
}

void TickSystem::add_player_chunk(
    PlayerHandle id, const std::string& dimension, int cx, int cy, int cz) {
    if (id == kInvalidPlayerHandle) return;

    auto it = player_chunks_.find(id);
    if (it != player_chunks_.end()) {
        PlayerChunkPos& pos = it->second;
        if (pos.dimension == dimension &&
            pos.cx == cx && pos.cy == cy && pos.cz == cz) {
            return; // unchanged
        }
        pos.dimension = dimension;
        pos.cx = cx;
        pos.cy = cy;
        pos.cz = cz;
    } else {
        PlayerChunkPos pos;
        pos.dimension = dimension;
        pos.cx = cx;
        pos.cy = cy;
        pos.cz = cz;
        player_chunks_[id] = pos;
    }
    player_chunks_dirty_ = true;
    // Rebuild immediately so active_chunks() reflects the new position
    // before the next tick (matches legacy set_player_chunk behavior).
    if (world_data_) {
        rebuild_chunk_sets();
    }
}

void TickSystem::remove_player_chunk(PlayerHandle id) {
    if (player_chunks_.erase(id) > 0) {
        player_chunks_dirty_ = true;
    }
}

void TickSystem::clear_player_chunks() {
    if (player_chunks_.empty()) return;
    player_chunks_.clear();
    player_chunks_dirty_ = true;
}

std::string TickSystem::get_player_dimension(PlayerHandle id) const {
    auto it = player_chunks_.find(id);
    if (it == player_chunks_.end()) return std::string();
    return it->second.dimension;
}

SleepTier TickSystem::classify_sleep_tier(int cx, int cy, int cz) const {
    // Find the minimum Chebyshev distance to any registered player
    // in the same dimension. If no players are registered, return FAR.
    int min_dist = INT32_MAX;
    for (const auto& pair : player_chunks_) {
        const PlayerChunkPos& pos = pair.second;
        // Players in other dimensions do not contribute.
        // (Chunk keys carry their own dimension; the caller filters.)
        int dx = std::abs(cx - pos.cx);
        int dy = std::abs(cy - pos.cy);
        int dz = std::abs(cz - pos.cz);
        int dist = std::max({dx, dy, dz});
        if (dist < min_dist) min_dist = dist;
    }

    if (min_dist == INT32_MAX) {
        return SleepTier::FAR;
    }

    if (min_dist <= active_radius_ * 2) {
        return SleepTier::NEAR;
    } else if (min_dist <= active_radius_ * 3) {
        return SleepTier::MID;
    } else {
        return SleepTier::FAR;
    }
}

void TickSystem::rebuild_chunk_sets() {
    active_chunks_.clear();
    sleep_near_chunks_.clear();
    sleep_mid_chunks_.clear();
    sleep_far_chunks_.clear();
    external_active_chunk_count_ = 0;
    external_sleeping_chunk_count_ = 0;
    skipped_external_sleeping_chunk_count_ = 0;

    player_chunks_dirty_ = false;

    if (!world_data_) return;
    if (player_chunks_.empty()) return;

    // Collect the set of dimensions that have at least one player,
    // so we skip chunks in dimensions with no observers.
    // (all_chunk_keys may span multiple dimensions in the future;
    // for now this is a minor optimization.)
    auto all_keys = world_data_->all_chunk_keys();
    for (const auto& key : all_keys) {
        const ChunkData* chunk = world_data_->get_chunk(
            key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
        if (chunk == nullptr) continue;

        // Compute the minimum Chebyshev distance to any player in the
        // same dimension. Players in other dimensions are ignored.
        int min_dist = INT32_MAX;
        for (const auto& pair : player_chunks_) {
            const PlayerChunkPos& pos = pair.second;
            if (pos.dimension != key.dimension_id) continue;
            int dx = std::abs(key.chunk_x - pos.cx);
            int dy = std::abs(key.chunk_y - pos.cy);
            int dz = std::abs(key.chunk_z - pos.cz);
            int dist = std::max({dx, dy, dz});
            if (dist < min_dist) min_dist = dist;
        }

        // No player in this dimension — skip the chunk.
        if (min_dist == INT32_MAX) continue;

        const ChunkState state = chunk->state;
        const bool external_active =
            respect_external_chunk_state_ && state == ChunkState::ACTIVE;
        const bool external_sleeping =
            respect_external_chunk_state_ && state == ChunkState::SLEEPING;

        // Renderer/shell streaming may explicitly keep a chunk ACTIVE even if
        // it is outside the normal player radius, for example indexed deep
        // chunks in a nearby surface column.
        if (external_active) {
            active_chunks_.push_back(key);
            ++external_active_chunk_count_;
            continue;
        }

        // Immediate player-neighborhood chunks remain active even if their last
        // renderer state is SLEEPING. This prevents stale sleeping marks from
        // blocking direct interaction when the player comes back.
        if (min_dist <= active_radius_) {
            active_chunks_.push_back(key);
            continue;
        }

        // Chunks explicitly marked sleeping are never promoted to ACTIVE by the
        // distance-only scheduler. They run only at sleeping cadence while still
        // inside the simulation horizon.
        if (external_sleeping) {
            ++external_sleeping_chunk_count_;
            if (min_dist <= active_radius_ * 2) {
                sleep_near_chunks_.push_back(key);
            } else if (min_dist <= active_radius_ * 3) {
                sleep_mid_chunks_.push_back(key);
            } else if (min_dist <= active_radius_ * 4) {
                sleep_far_chunks_.push_back(key);
            } else {
                ++skipped_external_sleeping_chunk_count_;
            }
            continue;
        }

        if (min_dist <= active_radius_ * 2) {
            sleep_near_chunks_.push_back(key);
        } else if (min_dist <= active_radius_ * 3) {
            sleep_mid_chunks_.push_back(key);
        } else if (min_dist <= active_radius_ * 4) {
            sleep_far_chunks_.push_back(key);
        }
    }
}

bool TickSystem::should_tick_sleeping(
    int cx, int cy, int cz, SleepTier tier) const {
    int64_t hash = static_cast<int64_t>(cx) * 73856093 +
                   static_cast<int64_t>(cy) * 19349663 +
                   static_cast<int64_t>(cz) * 83492791;

    int interval = 1;
    switch (tier) {
        case SleepTier::NEAR: interval = sleep_near_interval_; break;
        case SleepTier::MID:  interval = sleep_mid_interval_;  break;
        case SleepTier::FAR:  interval = sleep_far_interval_;  break;
        default: break;
    }

    return (hash % interval) ==
           (tick_counter_ / interval) % interval;
}

// --- Parallel execution ---

int TickSystem::effective_worker_threads() const {
    if (max_worker_threads_ > 0) return max_worker_threads_;
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, hw - 1);
}

void TickSystem::run_subsystem_chunks(
    SimulationSystem& sys,
    const std::vector<ChunkKey>& chunks,
    float delta,
    bool is_active,
    const TickContext* ctx,
    const char* phase_name) {

    if (chunks.empty()) return;

    ScopedTickProfile profile(
        &profiler_, subsystem_profile_name(phase_name, sys));

    // If the subsystem is not thread-safe, run sequentially.
    if (!parallel_enabled_ || !sys.is_thread_safe()) {
        for (const auto& key : chunks) {
            if (is_active) {
                sys.tick_active(key, delta, ctx);
            } else {
                sys.tick_sleeping(key, delta, ctx);
            }
        }
        return;
    }

    // Chunk-level parallelism: distribute chunks across worker threads.
    const int num_workers = effective_worker_threads();
    if (num_workers <= 1 || chunks.size() <= 1) {
        // Not enough workers or chunks — run sequentially.
        for (const auto& key : chunks) {
            if (is_active) {
                sys.tick_active(key, delta, ctx);
            } else {
                sys.tick_sleeping(key, delta, ctx);
            }
        }
        return;
    }

    // Split chunks into partitions.
    const size_t chunks_per_worker =
        (chunks.size() + num_workers - 1) / num_workers;
    std::vector<std::future<void>> futures;
    futures.reserve(num_workers);

    for (int w = 0; w < num_workers; ++w) {
        size_t start = w * chunks_per_worker;
        size_t end = std::min(start + chunks_per_worker, chunks.size());
        if (start >= end) break;

        // Capture chunks by value to guarantee lifetime safety across
        // async workers — ChunkKey is cheap to copy.
        futures.push_back(std::async(std::launch::async,
            [&sys, chunks, delta, is_active, ctx, start, end]() {
                for (size_t i = start; i < end; ++i) {
                    if (is_active) {
                        sys.tick_active(chunks[i], delta, ctx);
                    } else {
                        sys.tick_sleeping(chunks[i], delta, ctx);
                    }
                }
            }));
    }

    // Wait for all workers to complete.
    for (auto& f : futures) {
        f.get();
    }
}

void TickSystem::run_priority_group(
    const std::vector<SimulationSystem*>& group,
    const std::vector<ChunkKey>& chunks,
    float delta,
    bool is_active,
    const TickContext* ctx,
    const char* phase_name) {

    if (group.empty() || chunks.empty()) return;

    // Check if all subsystems in this group are thread-safe.
    bool all_safe = true;
    for (const auto* sys : group) {
        if (!sys->is_thread_safe()) {
            all_safe = false;
            break;
        }
    }

    if (!all_safe || group.size() <= 1) {
        // Not all safe or only one subsystem — run sequentially.
        for (auto* sys : group) {
            run_subsystem_chunks(*sys, chunks, delta, is_active, ctx, phase_name);
        }
        return;
    }

    // Priority-group parallelism: run all subsystems concurrently.
    std::vector<std::future<void>> futures;
    futures.reserve(group.size());

    // Capture chunks by value to guarantee lifetime safety across
    // async workers — ChunkKey is cheap to copy.
    for (auto* sys : group) {
        futures.push_back(std::async(std::launch::async,
            [this, sys, chunks, delta, is_active, ctx, phase_name]() {
                run_subsystem_chunks(*sys, chunks, delta, is_active, ctx, phase_name);
            }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

void TickSystem::run_chunks_by_priority_groups(
    const std::vector<ChunkKey>& chunks,
    float delta,
    bool is_active,
    const TickContext* ctx,
    const char* phase_name) {

    // Group subsystems by priority.
    std::vector<std::vector<SimulationSystem*>> groups;
    int current_priority = -1;

    for (auto& sys : subsystems_) {
        int p = sys->priority();
        if (p != current_priority) {
            groups.emplace_back();
            current_priority = p;
        }
        groups.back().push_back(sys.get());
    }

    // Run each priority group in order, but subsystems within
    // a group may run in parallel.
    for (auto& group : groups) {
        run_priority_group(group, chunks, delta, is_active, ctx, phase_name);
    }
}

TickContext TickSystem::build_tick_context() const {
    TickContext ctx;
    for (const auto& sys : subsystems_) {
        const char* n = sys->name();
        if (std::strcmp(n, "EcosystemSystem") == 0) {
            ctx.ecosystem = static_cast<EcosystemSystem*>(sys.get());
        } else if (std::strcmp(n, "SeasonSystem") == 0) {
            ctx.season = static_cast<SeasonSystem*>(sys.get());
        }
    }
    return ctx;
}

std::string TickSystem::subsystem_profile_name(
    const char* phase_name,
    const SimulationSystem& sys) const {
    std::ostringstream oss;
    oss << (phase_name != nullptr ? phase_name : "sim") << "." << sys.name();
    return oss.str();
}

} // namespace science_and_theology
