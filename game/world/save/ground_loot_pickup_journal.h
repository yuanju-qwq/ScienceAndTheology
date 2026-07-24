// Crash-recovery journal for one-way ground-loot pickup transfers.
//
// Player state and chunk sidecars are separate durable files. A journal entry
// retains the original chunk-owned stack until recovery can observe a matching
// player receipt and choose exactly one durable owner.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct GameGroundLootPickupClaim {
    uint64_t loot_id = 0;
    std::string account_id;
    ChunkKey chunk;
    GameGroundLootRecord record;

    friend bool operator==(const GameGroundLootPickupClaim&,
                           const GameGroundLootPickupClaim&) = default;
};

class GameGroundLootPickupJournal final {
public:
    // Missing state is a valid empty journal. A primary corruption is rejected
    // even when a backup exists, matching current player-save recovery rules.
    [[nodiscard]] static snt::core::Expected<std::vector<GameGroundLootPickupClaim>> load(
        std::string_view universe_save_dir);
    [[nodiscard]] static snt::core::Expected<void> save(
        std::string_view universe_save_dir,
        std::span<const GameGroundLootPickupClaim> claims);
};

}  // namespace snt::game
