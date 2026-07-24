// Native creature presentation replication codec and client value cache.
//
// Ownership: authoritative wildlife emits GameCreaturePresentationState
// values, and the server filters the complete visible set by player AOI.
// This module serializes and caches that value-only state; it never creates
// ECS entities or makes a far visual representative interactive.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/world/defs/creature_presentation.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameCreaturePresentationReplicationVersion = 2;
inline constexpr size_t kMaxGameCreaturePresentationStates = 4096;

// Each payload is a complete current set for one observer. The outer
// replication source compares it against the peer baseline and only emits a
// delta when this set or its state actually changes.
struct GameCreaturePresentationSnapshot {
    uint64_t source_tick = 0;
    std::vector<GameCreaturePresentationState> creatures;
};

[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_creature_presentation_snapshot(
    const GameCreaturePresentationSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<GameCreaturePresentationSnapshot>
decode_game_creature_presentation_snapshot(std::span<const std::byte> payload);

// Client-only cache for the remote server's current creature presentation
// set. A graphical adapter reconciles its native ECS entities from copies of
// `creatures()` after this cache has accepted a snapshot or delta.
class GameRemoteCreatureWorld final {
public:
    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] std::vector<GameCreaturePresentationState> creatures() const;
    [[nodiscard]] std::optional<GameCreaturePresentationState> find_creature(
        uint64_t entity_id) const;
    [[nodiscard]] size_t creature_count() const noexcept { return creatures_.size(); }
    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] uint64_t latest_source_tick() const noexcept { return latest_source_tick_; }
    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> replace_current_set(
        const GameCreaturePresentationSnapshot& snapshot);

    std::map<uint64_t, GameCreaturePresentationState> creatures_;
    uint64_t latest_source_tick_ = 0;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
