// Stable game-owned quest progress values and persistence boundary.
//
// Ownership: this header is intentionally independent of content scripts and
// QuestRegistry so save providers can depend on the values without linking the
// simulation runtime. All concrete persistence stays game-owned.

#pragma once

#include "core/expected.h"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

// Quest progress uses the game account_id verbatim. Current account ids are
// namespaced (`steam:<SteamID64>` or `local-name:<validated UTF-8 name>`), so
// equal visible names from Steam and local startup profiles never share task
// state, while equal local names intentionally do.
using QuestPlayerId = std::string;

enum class QuestState : uint8_t {
    kLocked = 0,
    // Better Questing-style tasks become active as soon as their prerequisite
    // graph is satisfied. There is deliberately no player acceptance state.
    kInProgress = 1,
    kCompleted = 2,
};

// Complete per-player value record. It carries no script objects, ECS handles,
// pointers, or filesystem paths. Removed quest definitions stay as orphan
// records so a later content reload can reconcile them without data loss.
struct QuestProgressRecord {
    std::string quest_id;
    QuestState state = QuestState::kLocked;
    std::map<std::string, int32_t, std::less<>> objective_counts;
    uint64_t completed_tick = 0;
    uint32_t completion_count = 0;
    bool reward_claimed = false;
};

// GameSaveManager owns the concrete file provider. QuestRegistry only passes
// stable values through this interface at explicit player load/save boundaries;
// fixed ticks must never perform filesystem I/O through it.
class IQuestProgressPersistence {
public:
    virtual ~IQuestProgressPersistence() = default;

    virtual snt::core::Expected<std::vector<QuestProgressRecord>> load_player_progress(
        std::string_view player_id) = 0;
    virtual snt::core::Expected<void> save_player_progress(
        std::string_view player_id, std::span<const QuestProgressRecord> progress) = 0;
};

}  // namespace snt::game
