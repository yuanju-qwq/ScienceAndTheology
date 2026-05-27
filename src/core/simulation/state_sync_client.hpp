#pragma once

#include "state_sync_common.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology {

// Client-side state applier.
// Receives deltas from StateSyncServer and applies them to a local
// WorldData instance (rendering proxy in single-player, or remote
// client replica in multiplayer).
class StateSyncClient {
public:
    StateSyncClient() = default;

    // Apply a delta to a local WorldData instance.
    // This is a minimal implementation; the Godot rendering layer
    // typically handles view creation based on delta contents.
    void apply_delta(const StateDelta& delta, WorldData* world);

    // Returns the timestamp of the last applied delta.
    int64_t last_applied_timestamp() const { return last_timestamp_; }

private:
    int64_t last_timestamp_ = 0;
};

} // namespace science_and_theology
