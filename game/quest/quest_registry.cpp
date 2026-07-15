// Game-owned quest lifecycle and persistent-value progress implementation.

#define SNT_LOG_CHANNEL "game.quest"
#include "game/quest/quest_registry.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool is_valid_state(QuestState state) noexcept {
    switch (state) {
        case QuestState::kLocked:
        case QuestState::kAvailable:
        case QuestState::kInProgress:
        case QuestState::kCompleted:
            return true;
    }
    return false;
}

[[nodiscard]] const char* state_name(QuestState state) noexcept {
    switch (state) {
        case QuestState::kLocked: return "locked";
        case QuestState::kAvailable: return "available";
        case QuestState::kInProgress: return "in_progress";
        case QuestState::kCompleted: return "completed";
    }
    return "unknown";
}

}  // namespace

QuestRegistry::QuestRegistry(const GameContentRegistry& definitions,
                             IQuestLifecycleSink* lifecycle_sink)
    : definitions_source_(&definitions), lifecycle_sink_(lifecycle_sink) {}

snt::core::Expected<void> QuestRegistry::refresh_definitions() {
    if (definitions_source_ == nullptr) {
        return invalid_state("QuestRegistry has no GameContentRegistry");
    }

    const uint64_t revision = definitions_source_->quest_content_revision();
    if (revision == observed_content_revision_) return {};

    DefinitionMap updated;
    for (QuestDefinition definition : definitions_source_->quest_definitions()) {
        updated.emplace(definition.id, std::move(definition));
    }
    definitions_ = std::move(updated);
    observed_content_revision_ = revision;
    log_definition_warnings();
    for (auto& [player_id, progress] : players_) {
        (void)player_id;
        ensure_progress_records(progress);
    }
    return {};
}

snt::core::Expected<void> QuestRegistry::tick(uint64_t tick_index) {
    if (auto result = refresh_definitions(); !result) return result.error();
    for (auto& [player_id, progress] : players_) {
        reconcile_player(player_id, progress, tick_index);
    }
    return {};
}

snt::core::Expected<void> QuestRegistry::accept(QuestPlayerId player_id,
                                                 std::string_view quest_id,
                                                 uint64_t tick_index) {
    if (auto result = validate_player_id(player_id); !result) return result.error();
    if (quest_id.empty()) return invalid_argument("Quest id must not be empty");
    if (auto result = synchronize_player(player_id, tick_index); !result) return result.error();

    auto definition = definitions_.find(quest_id);
    if (definition == definitions_.end()) return invalid_argument("Quest definition is not registered");
    auto& progress = players_.at(player_id).at(std::string(quest_id));
    if (progress.state != QuestState::kAvailable) {
        return invalid_state("Quest can only be accepted while available");
    }
    transition(player_id, progress, QuestState::kInProgress, tick_index);
    reconcile_player(player_id, players_.at(player_id), tick_index);
    return {};
}

snt::core::Expected<void> QuestRegistry::record_progress(
    QuestPlayerId player_id, const QuestProgressEvent& event, uint64_t tick_index) {
    if (auto result = validate_player_id(player_id); !result) return result.error();
    if (event.amount <= 0) return invalid_argument("Quest progress event amount must be positive");
    if (event.kind == QuestObjectiveKind::kReachTick) {
        return invalid_argument("Reach-tick quest objectives are updated by QuestRegistry::tick");
    }
    if (event.target_id.empty()) return invalid_argument("Quest progress event target_id must not be empty");
    if (auto result = synchronize_player(player_id, tick_index); !result) return result.error();

    auto& progress = players_.at(player_id);
    for (const auto& [quest_id, definition] : definitions_) {
        auto progress_it = progress.find(quest_id);
        if (progress_it == progress.end() || progress_it->second.state != QuestState::kInProgress) {
            continue;
        }
        for (const QuestObjectiveDefinition& objective : definition.objectives) {
            if (objective.kind != event.kind || objective.target_id != event.target_id) continue;
            const int64_t next = static_cast<int64_t>(progress_it->second.objective_counts[objective.id]) +
                                 static_cast<int64_t>(event.amount);
            progress_it->second.objective_counts[objective.id] = static_cast<int32_t>(
                std::min<int64_t>(next, objective.required_count));
        }
    }
    reconcile_player(player_id, progress, tick_index);
    return {};
}

snt::core::Expected<void> QuestRegistry::update_inventory(
    QuestPlayerId player_id, const InventoryCountQuery& item_count, uint64_t tick_index) {
    if (auto result = validate_player_id(player_id); !result) return result.error();
    if (!item_count) return invalid_argument("Quest inventory count query must not be empty");
    if (auto result = synchronize_player(player_id, tick_index); !result) return result.error();

    auto& progress = players_.at(player_id);
    for (const auto& [quest_id, definition] : definitions_) {
        auto progress_it = progress.find(quest_id);
        if (progress_it == progress.end() || progress_it->second.state != QuestState::kInProgress) {
            continue;
        }
        for (const QuestObjectiveDefinition& objective : definition.objectives) {
            if (objective.kind != QuestObjectiveKind::kAcquireItem) continue;
            const int32_t count = std::max(0, item_count(objective.target_id));
            progress_it->second.objective_counts[objective.id] =
                std::min(count, objective.required_count);
        }
    }
    reconcile_player(player_id, progress, tick_index);
    return {};
}

snt::core::Expected<void> QuestRegistry::reset_repeatable(
    QuestPlayerId player_id, std::string_view quest_id, uint64_t tick_index) {
    if (auto result = validate_player_id(player_id); !result) return result.error();
    if (quest_id.empty()) return invalid_argument("Quest id must not be empty");
    if (auto result = synchronize_player(player_id, tick_index); !result) return result.error();

    const auto definition = definitions_.find(quest_id);
    if (definition == definitions_.end()) return invalid_argument("Quest definition is not registered");
    if (!definition->second.repeatable) return invalid_state("Quest is not repeatable");

    auto& progress = players_.at(player_id).at(std::string(quest_id));
    if (progress.state != QuestState::kCompleted) {
        return invalid_state("Only completed quests can be reset");
    }
    for (auto& [objective_id, count] : progress.objective_counts) {
        (void)objective_id;
        count = 0;
    }
    progress.reward_claimed = false;
    progress.completed_tick = 0;
    transition(player_id, progress, QuestState::kAvailable, tick_index);
    reconcile_player(player_id, players_.at(player_id), tick_index);
    return {};
}

const QuestProgressRecord* QuestRegistry::find_progress(std::string_view player_id,
                                                         std::string_view quest_id) const {
    const auto player = players_.find(std::string(player_id));
    if (player == players_.end()) return nullptr;
    const auto progress = player->second.find(quest_id);
    return progress == player->second.end() ? nullptr : &progress->second;
}

bool QuestRegistry::is_visible(std::string_view player_id, std::string_view quest_id) const {
    const auto definition = definitions_.find(quest_id);
    if (definition == definitions_.end()) return false;
    if (!definition->second.hidden) return true;
    const QuestProgressRecord* progress = find_progress(player_id, quest_id);
    return progress && progress->state != QuestState::kLocked;
}

std::vector<QuestProgressRecord> QuestRegistry::snapshot_progress(
    std::string_view player_id) const {
    std::vector<QuestProgressRecord> snapshot;
    const auto player = players_.find(std::string(player_id));
    if (player == players_.end()) return snapshot;
    snapshot.reserve(player->second.size());
    for (const auto& [quest_id, progress] : player->second) {
        (void)quest_id;
        snapshot.push_back(progress);
    }
    return snapshot;
}

snt::core::Expected<void> QuestRegistry::restore_progress(
    QuestPlayerId player_id, std::vector<QuestProgressRecord> progress) {
    if (auto result = validate_player_id(player_id); !result) return result.error();
    if (auto result = refresh_definitions(); !result) return result.error();

    PlayerProgressMap restored;
    for (QuestProgressRecord& record : progress) {
        if (record.quest_id.empty()) return invalid_argument("Restored quest id must not be empty");
        if (!is_valid_state(record.state)) return invalid_argument("Restored quest state is invalid");
        for (const auto& [objective_id, count] : record.objective_counts) {
            if (objective_id.empty() || count < 0) {
                return invalid_argument("Restored quest objective progress is invalid");
            }
        }
        if (!restored.emplace(record.quest_id, std::move(record)).second) {
            return invalid_argument("Restored quest progress contains duplicate quest ids");
        }
    }
    players_[std::move(player_id)] = std::move(restored);
    return {};
}

snt::core::Expected<void> QuestRegistry::load_player_progress(
    QuestPlayerId player_id, IQuestProgressPersistence& persistence) {
    if (auto result = validate_player_id(player_id); !result) return result.error();

    auto loaded = persistence.load_player_progress(player_id);
    if (!loaded) {
        auto error = loaded.error();
        error.with_context("QuestRegistry::load_player_progress");
        return error;
    }
    if (auto result = restore_progress(std::move(player_id), std::move(*loaded)); !result) {
        auto error = result.error();
        error.with_context("QuestRegistry::load_player_progress");
        return error;
    }
    return {};
}

snt::core::Expected<void> QuestRegistry::save_player_progress(
    std::string_view player_id, IQuestProgressPersistence& persistence) const {
    if (auto result = validate_player_id(player_id); !result) return result.error();

    if (auto result = persistence.save_player_progress(player_id, snapshot_progress(player_id));
        !result) {
        auto error = result.error();
        error.with_context("QuestRegistry::save_player_progress");
        return error;
    }
    return {};
}

void QuestRegistry::clear() noexcept {
    definitions_.clear();
    players_.clear();
    observed_content_revision_ = 0;
}

snt::core::Expected<void> QuestRegistry::validate_player_id(std::string_view player_id) const {
    if (player_id.empty()) return invalid_argument("Quest player id must not be empty");
    return {};
}

snt::core::Expected<void> QuestRegistry::synchronize_player(
    QuestPlayerId player_id, uint64_t tick_index) {
    if (auto result = refresh_definitions(); !result) return result.error();
    auto& progress = players_[player_id];
    ensure_progress_records(progress);
    reconcile_player(player_id, progress, tick_index);
    return {};
}

void QuestRegistry::ensure_progress_records(PlayerProgressMap& progress) {
    for (const auto& [quest_id, definition] : definitions_) {
        auto [record, inserted] = progress.try_emplace(quest_id);
        if (inserted) record->second.quest_id = quest_id;
        for (const QuestObjectiveDefinition& objective : definition.objectives) {
            record->second.objective_counts.try_emplace(objective.id, 0);
        }
    }
}

void QuestRegistry::reconcile_player(std::string_view player_id, PlayerProgressMap& progress,
                                     uint64_t tick_index) {
    ensure_progress_records(progress);
    for (size_t pass = 0; pass <= definitions_.size(); ++pass) {
        bool changed = false;
        for (const auto& [quest_id, definition] : definitions_) {
            QuestProgressRecord& record = progress.at(quest_id);
            if (record.state == QuestState::kLocked && prerequisites_met(progress, definition)) {
                changed |= transition(player_id, record, QuestState::kAvailable, tick_index);
            }
            if (record.state == QuestState::kAvailable && definition.auto_start) {
                changed |= transition(player_id, record, QuestState::kInProgress, tick_index);
            }
            if (record.state != QuestState::kInProgress) continue;

            update_tick_objectives(record, definition, tick_index);
            if (objectives_satisfied(record, definition)) {
                changed |= transition(player_id, record, QuestState::kCompleted, tick_index);
            }
        }
        if (!changed) return;
    }
}

bool QuestRegistry::prerequisites_met(const PlayerProgressMap& progress,
                                      const QuestDefinition& definition) const {
    for (const std::string& prerequisite_id : definition.prerequisites) {
        const auto prerequisite = progress.find(prerequisite_id);
        if (prerequisite == progress.end() || prerequisite->second.state != QuestState::kCompleted) {
            return false;
        }
    }
    return true;
}

bool QuestRegistry::objectives_satisfied(const QuestProgressRecord& progress,
                                         const QuestDefinition& definition) const {
    if (definition.objectives.empty()) return false;
    return std::all_of(definition.objectives.begin(), definition.objectives.end(),
                       [&progress](const QuestObjectiveDefinition& objective) {
                           const auto current = progress.objective_counts.find(objective.id);
                           return current != progress.objective_counts.end() &&
                                  current->second >= objective.required_count;
                       });
}

void QuestRegistry::update_tick_objectives(QuestProgressRecord& progress,
                                           const QuestDefinition& definition,
                                           uint64_t tick_index) const {
    const int32_t current_tick = static_cast<int32_t>(std::min<uint64_t>(
        tick_index, static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
    for (const QuestObjectiveDefinition& objective : definition.objectives) {
        if (objective.kind != QuestObjectiveKind::kReachTick) continue;
        int32_t& count = progress.objective_counts[objective.id];
        count = std::max(count, std::min(current_tick, objective.required_count));
    }
}

bool QuestRegistry::transition(std::string_view player_id, QuestProgressRecord& progress,
                               QuestState state, uint64_t tick_index) {
    if (progress.state == state) return false;

    const QuestState previous = progress.state;
    progress.state = state;
    if (state == QuestState::kCompleted) {
        progress.completed_tick = tick_index;
        ++progress.completion_count;
    }
    SNT_LOG_INFO("Quest '%s' for player '%.*s' transitioned %s -> %s",
                 progress.quest_id.c_str(), static_cast<int>(player_id.size()), player_id.data(),
                 state_name(previous), state_name(state));
    if (lifecycle_sink_) {
        lifecycle_sink_->on_quest_lifecycle_event({
            .player_id = std::string(player_id),
            .quest_id = progress.quest_id,
            .previous_state = previous,
            .state = state,
            .tick_index = tick_index,
        });
    }
    return true;
}

void QuestRegistry::log_definition_warnings() const {
    for (const auto& [quest_id, definition] : definitions_) {
        for (const std::string& prerequisite_id : definition.prerequisites) {
            if (!definitions_.contains(prerequisite_id)) {
                SNT_LOG_WARN("Quest '%s' references missing prerequisite '%s'; it remains locked",
                             quest_id.c_str(), prerequisite_id.c_str());
            }
        }
    }
}

}  // namespace snt::game
