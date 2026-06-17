#include "tick_system.hpp"
#include "../world/world_data.hpp"
#include <algorithm>
#include <cmath>

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

    rebuild_chunk_sets();

    // Phase 1: ACTIVE chunks — full simulation.
    for (auto& sys : subsystems_) {
        for (const auto& key : active_chunks_) {
            sys->tick_active(key, delta);
        }
    }

    // Phase 2: SLEEPING chunks — low-frequency simulation.
    if (tick_counter_ % kSleepTickInterval == 0) {
        for (auto& sys : subsystems_) {
            for (const auto& key : sleeping_chunks_) {
                if (should_tick_sleeping(key.chunk_x, key.chunk_y, key.chunk_z)) {
                    sys->tick_sleeping(key, delta * kSleepTickInterval);
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

void TickSystem::rebuild_chunk_sets() {
    active_chunks_.clear();
    sleeping_chunks_.clear();

    if (!world_data_) return;

    auto all_keys = world_data_->all_chunk_keys();
    for (const auto& key : all_keys) {
        int dx = std::abs(key.chunk_x - player_cx_);
        int dy = std::abs(key.chunk_y - player_cy_);
        int dz = std::abs(key.chunk_z - player_cz_);
        if (dx <= active_radius_ &&
            dy <= active_radius_ &&
            dz <= active_radius_ &&
            key.dimension_id == player_dimension_) {
            active_chunks_.push_back(key);
        } else if (key.dimension_id == player_dimension_ &&
                   dx <= active_radius_ * 2 &&
                   dy <= active_radius_ * 2 &&
                   dz <= active_radius_ * 2) {
            sleeping_chunks_.push_back(key);
        }
    }
}

bool TickSystem::should_tick_sleeping(int cx, int cy, int cz) const {
    // Distribute sleeping ticks across different chunks each pass.
    // Use a simple spatial hash to ensure every sleep chunk ticks eventually.
    int64_t hash = static_cast<int64_t>(cx) * 73856093 +
                   static_cast<int64_t>(cy) * 19349663 +
                   static_cast<int64_t>(cz) * 83492791;
    return (hash % kSleepTickInterval) ==
           (tick_counter_ / kSleepTickInterval) % kSleepTickInterval;
}

} // namespace science_and_theology
