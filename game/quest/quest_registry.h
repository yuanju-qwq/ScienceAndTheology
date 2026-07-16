// Game-owned quest definitions, progress, and lifecycle boundary.
//
// Ownership: GameContentRegistry owns hot-reloadable QuestDefinition values;
// QuestRegistry owns per-player mutable progress keyed only by stable strings.
// Thread affinity: every method is simulation-main-thread-only. Worker systems
// and scripts must publish value events instead of retaining this registry.

#pragma once

#include "game/client/game_content_registry.h"
#include "game/quest/quest_progress.h"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct QuestProgressEvent {
    QuestObjectiveKind kind = QuestObjectiveKind::kCraftItem;
    std::string target_id;
    int32_t amount = 1;
};

enum class QuestLifecycleEventKind : uint8_t {
    kStateChanged,
    kRewardClaimed,
};

struct QuestLifecycleEvent {
    QuestLifecycleEventKind kind = QuestLifecycleEventKind::kStateChanged;
    QuestPlayerId player_id;
    std::string quest_id;
    QuestState previous_state = QuestState::kLocked;
    QuestState state = QuestState::kLocked;
    uint64_t tick_index = 0;
};

// UI, replication, analytics, and save-dirty tracking can observe quest
// transitions without getting mutable progress access.
class IQuestLifecycleSink {
public:
    virtual ~IQuestLifecycleSink() = default;
    virtual void on_quest_lifecycle_event(const QuestLifecycleEvent& event) = 0;
};

// QuestRegistry resolves content reward declarations into these value-only
// inventory additions. The sink owns host inventory transactions and must
// either commit the complete list or reject it without a partial grant.
struct QuestItemReward {
    std::string item_id;
    int32_t count = 1;
};

// Reward effects may alter inventories or schedule world mutations, so they
// are committed by a game service rather than encoded into QuestProgressRecord.
// QuestRegistry itself handles kUnlockQuest progress-state changes.
class IQuestRewardSink {
public:
    virtual ~IQuestRewardSink() = default;
    virtual snt::core::Expected<void> grant_item_rewards(
        std::string_view player_id, std::string_view quest_id,
        std::span<const QuestItemReward> rewards) = 0;
};

class QuestRegistry final {
public:
    using InventoryCountQuery = std::function<int32_t(std::string_view item_id)>;

    explicit QuestRegistry(const GameContentRegistry& definitions,
                           IQuestLifecycleSink* lifecycle_sink = nullptr);

    QuestRegistry(const QuestRegistry&) = delete;
    QuestRegistry& operator=(const QuestRegistry&) = delete;

    // Copies effective content definitions only when the content revision has
    // changed. Existing progress is never reset by this synchronization.
    [[nodiscard]] snt::core::Expected<void> refresh_definitions();
    [[nodiscard]] snt::core::Expected<void> tick(uint64_t tick_index);

    [[nodiscard]] snt::core::Expected<void> accept(QuestPlayerId player_id,
                                                    std::string_view quest_id,
                                                    uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> record_progress(
        QuestPlayerId player_id, const QuestProgressEvent& event, uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> update_inventory(
        QuestPlayerId player_id, const InventoryCountQuery& item_count, uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> reset_repeatable(
        QuestPlayerId player_id, std::string_view quest_id, uint64_t tick_index);
    // Reward delivery is an explicit host-side transaction. A task-book UI
    // will invoke this through its dedicated command path; it is deliberately
    // not folded into QuestAccept or a generic world-interaction payload.
    [[nodiscard]] snt::core::Expected<void> claim_reward(
        QuestPlayerId player_id, std::string_view quest_id, uint64_t tick_index);

    // Composition-only lifecycle wiring. Callers must set sinks on the
    // simulation main thread while no worker task is publishing quest events.
    void set_lifecycle_sink(IQuestLifecycleSink* lifecycle_sink) noexcept {
        lifecycle_sink_ = lifecycle_sink;
    }
    void set_reward_sink(IQuestRewardSink* reward_sink) noexcept {
        reward_sink_ = reward_sink;
    }

    [[nodiscard]] const QuestProgressRecord* find_progress(
        std::string_view player_id, std::string_view quest_id) const;
    [[nodiscard]] bool is_visible(std::string_view player_id, std::string_view quest_id) const;
    [[nodiscard]] std::vector<QuestProgressRecord> snapshot_progress(
        std::string_view player_id) const;
    // Monotonic per-player revision for lifecycle-owned autosave policy. It
    // changes only when the persisted value snapshot changes; querying it
    // never synchronizes definitions or performs I/O.
    [[nodiscard]] uint64_t progress_revision(std::string_view player_id) const noexcept;
    [[nodiscard]] snt::core::Expected<void> restore_progress(
        QuestPlayerId player_id, std::vector<QuestProgressRecord> progress);
    [[nodiscard]] snt::core::Expected<void> load_player_progress(
        QuestPlayerId player_id, IQuestProgressPersistence& persistence);
    [[nodiscard]] snt::core::Expected<void> save_player_progress(
        std::string_view player_id, IQuestProgressPersistence& persistence) const;

    [[nodiscard]] size_t definition_count() const noexcept { return definitions_.size(); }
    void clear() noexcept;

private:
    using DefinitionMap = std::map<std::string, QuestDefinition, std::less<>>;
    using PlayerProgressMap = std::map<std::string, QuestProgressRecord, std::less<>>;

    [[nodiscard]] snt::core::Expected<void> validate_player_id(
        std::string_view player_id) const;
    [[nodiscard]] snt::core::Expected<void> synchronize_player(
        QuestPlayerId player_id, uint64_t tick_index);
    [[nodiscard]] bool ensure_progress_records(PlayerProgressMap& progress);
    [[nodiscard]] bool reconcile_player(std::string_view player_id,
                                        PlayerProgressMap& progress,
                                        uint64_t tick_index);
    [[nodiscard]] bool prerequisites_met(const PlayerProgressMap& progress,
                                         const QuestDefinition& definition) const;
    [[nodiscard]] bool objectives_satisfied(const QuestProgressRecord& progress,
                                            const QuestDefinition& definition) const;
    [[nodiscard]] bool update_tick_objectives(QuestProgressRecord& progress,
                                               const QuestDefinition& definition,
                                               uint64_t tick_index) const;
    bool transition(std::string_view player_id, QuestProgressRecord& progress,
                    QuestState state, uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> validate_reward_targets(
        const QuestDefinition& definition) const;
    void mark_progress_changed(std::string_view player_id);
    void log_definition_warnings() const;

    const GameContentRegistry* definitions_source_ = nullptr;
    IQuestLifecycleSink* lifecycle_sink_ = nullptr;
    IQuestRewardSink* reward_sink_ = nullptr;
    DefinitionMap definitions_;
    std::map<QuestPlayerId, PlayerProgressMap, std::less<>> players_;
    std::map<QuestPlayerId, uint64_t, std::less<>> player_progress_revisions_;
    uint64_t observed_content_revision_ = 0;
};

}  // namespace snt::game
