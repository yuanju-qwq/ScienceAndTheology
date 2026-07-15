// GameSaveManager-backed quest persistence implementation.

#include "game/world/save/quest_progress_persistence.h"

#include "game/world/save/save_manager.h"

#include <utility>

namespace snt::game {

GameSaveQuestProgressPersistence::GameSaveQuestProgressPersistence(std::string save_dir)
    : save_dir_(std::move(save_dir)) {}

snt::core::Expected<std::vector<QuestProgressRecord>>
GameSaveQuestProgressPersistence::load_player_progress(std::string_view player_id) {
    return GameSaveManager::load_quest_progress(save_dir_, player_id);
}

snt::core::Expected<void> GameSaveQuestProgressPersistence::save_player_progress(
    std::string_view player_id, std::span<const QuestProgressRecord> progress) {
    return GameSaveManager::save_quest_progress(save_dir_, player_id, progress);
}

}  // namespace snt::game
