// Client task-book and platform-achievement value boundaries.
//
// QuestRegistry remains the authoritative owner of mutable progress. A task
// book only receives copied snapshots, while a future Steam adapter receives
// confirmed game achievement events through the platform-neutral sink.

#pragma once

#include "core/expected.h"
#include "game/quest/quest_progress.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::game {

struct QuestBookSnapshot {
    std::string account_id;
    // Semantic content fingerprint supplied by the authoritative registry.
    // It lets the client reject progress values that belong to a different
    // chapter graph or objective/reward definition package.
    uint64_t content_fingerprint = 0;
    uint64_t progress_revision = 0;
    std::vector<QuestProgressRecord> progress;
};

class IQuestBookPresenter {
public:
    virtual ~IQuestBookPresenter() = default;

    virtual void publish_quest_book_snapshot(QuestBookSnapshot snapshot) = 0;
};

struct GameAchievementEvent {
    std::string account_id;
    std::string achievement_id;
};

class IGameAchievementSink {
public:
    virtual ~IGameAchievementSink() = default;

    virtual snt::core::Expected<void> submit_achievement_event(
        const GameAchievementEvent& event) = 0;
};

}  // namespace snt::game
