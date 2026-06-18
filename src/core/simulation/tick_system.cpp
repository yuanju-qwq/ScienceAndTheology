#include "tick_system.hpp"
#include "ecosystem_system.hpp"
#include "season_system.hpp"
#include "../world/world_data.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>

namespace science_and_theology {

TickSystem::TickSystem(WorldData* world_data)
    : world_data_(world_data)
    , event_bus_(std::make_unique<EventBus>())
    , error_handler_(std::make_unique<ErrorHandler>())
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

    // Publish the current tick to WorldData so subsystems can read it.
    if (world_data_) {
        world_data_->set_current_tick(tick_counter_);
    }

    rebuild_chunk_sets();

    // Build cross-system context once per tick.
    const TickContext ctx = build_tick_context();
    const TickContext* ctx_ptr = &ctx;

    // --- Phase 1: ACTIVE chunks — full simulation ---

    if (parallel_enabled_) {
        // Group subsystems by priority and run each group.
        run_chunks_by_priority_groups(active_chunks_, delta, true, ctx_ptr);
    } else {
        // Sequential: iterate subsystems in priority order.
        for (auto& sys : subsystems_) {
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
            run_chunks_by_priority_groups(sleep_near_chunks_, sleep_delta, false, ctx_ptr);
        } else {
            for (auto& sys : subsystems_) {
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
            run_chunks_by_priority_groups(sleep_mid_chunks_, sleep_delta, false, ctx_ptr);
        } else {
            for (auto& sys : subsystems_) {
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
            run_chunks_by_priority_groups(sleep_far_chunks_, sleep_delta, false, ctx_ptr);
        } else {
            for (auto& sys : subsystems_) {
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
    event_bus_->process_queue();

    // Phase 4: Advance state sync tick.
    state_sync_->set_tick_counter(tick_counter_);
}

void TickSystem::set_player_chunk(
    const std::string& dimension, int cx, int cy, int cz) {
    if (player_dimension_ == dimension &&
        player_cx_ == cx &&
        player_cy_ == cy &&
        player_cz_ == cz) {
        return;
    }
    player_dimension_ = dimension;
    player_cx_ = cx;
    player_cy_ = cy;
    player_cz_ = cz;
    rebuild_chunk_sets();
}

SleepTier TickSystem::classify_sleep_tier(int cx, int cy, int cz) const {
    int dx = std::abs(cx - player_cx_);
    int dy = std::abs(cy - player_cy_);
    int dz = std::abs(cz - player_cz_);
    int dist = std::max({dx, dy, dz});

    if (dist <= active_radius_ * 2) {
        return SleepTier::NEAR;
    } else if (dist <= active_radius_ * 3) {
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

    if (!world_data_) return;

    auto all_keys = world_data_->all_chunk_keys();
    for (const auto& key : all_keys) {
        if (key.dimension_id != player_dimension_) continue;

        int dx = std::abs(key.chunk_x - player_cx_);
        int dy = std::abs(key.chunk_y - player_cy_);
        int dz = std::abs(key.chunk_z - player_cz_);

        if (dx <= active_radius_ &&
            dy <= active_radius_ &&
            dz <= active_radius_) {
            active_chunks_.push_back(key);
        } else if (dx <= active_radius_ * 2 &&
                   dy <= active_radius_ * 2 &&
                   dz <= active_radius_ * 2) {
            sleep_near_chunks_.push_back(key);
        } else if (dx <= active_radius_ * 3 &&
                   dy <= active_radius_ * 3 &&
                   dz <= active_radius_ * 3) {
            sleep_mid_chunks_.push_back(key);
        } else if (dx <= active_radius_ * 4 &&
                   dy <= active_radius_ * 4 &&
                   dz <= active_radius_ * 4) {
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
    const TickContext* ctx) {

    if (chunks.empty()) return;

    // If the subsystem is not thread-safe, run sequentially.
    if (!sys.is_thread_safe()) {
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
    const TickContext* ctx) {

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
            run_subsystem_chunks(*sys, chunks, delta, is_active, ctx);
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
            [this, sys, chunks, delta, is_active, ctx]() {
                run_subsystem_chunks(*sys, chunks, delta, is_active, ctx);
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
    const TickContext* ctx) {

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
        run_priority_group(group, chunks, delta, is_active, ctx);
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

} // namespace science_and_theology
