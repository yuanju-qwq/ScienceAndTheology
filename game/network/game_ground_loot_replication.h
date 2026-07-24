// Chunk-owned ground-loot presentation replication codec and client cache.
//
// The authoritative record remains in GameChunkSidecar. This module only
// carries an AOI-filtered presentation set and never grants inventory items.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/resources/resource_key.h"
#include "voxel/data/chunk_registry.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameGroundLootPresentationReplicationVersion = 1;
inline constexpr size_t kMaxGameGroundLootPresentationStates = 4096;

struct GameGroundLootPresentationState {
    uint64_t loot_id = 0;
    snt::voxel::ChunkKey chunk;
    ResourceContentStack resource;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    uint64_t spawned_tick = 0;
};

// Each payload is one observer's complete current ground-loot set. The outer
// replication source computes upserts/removals against its per-peer baseline.
struct GameGroundLootPresentationSnapshot {
    uint64_t source_tick = 0;
    std::vector<GameGroundLootPresentationState> loot;
};

[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_ground_loot_presentation_snapshot(
    const GameGroundLootPresentationSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameGroundLootPresentationSnapshot>
decode_game_ground_loot_presentation_snapshot(std::span<const std::byte> payload);

// Client-only cache for the remote server's current ground-loot set. A later
// renderer or input adapter can reconcile visuals and issue pickup commands
// from copies returned by `loot()` without owning authoritative state.
class GameRemoteGroundLootWorld final {
public:
    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] std::vector<GameGroundLootPresentationState> loot() const;
    [[nodiscard]] std::optional<GameGroundLootPresentationState> find_loot(
        uint64_t loot_id) const;
    [[nodiscard]] size_t loot_count() const noexcept { return loot_.size(); }
    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] uint64_t latest_source_tick() const noexcept { return latest_source_tick_; }
    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> replace_current_set(
        const GameGroundLootPresentationSnapshot& snapshot);

    std::map<uint64_t, GameGroundLootPresentationState> loot_;
    uint64_t latest_source_tick_ = 0;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
