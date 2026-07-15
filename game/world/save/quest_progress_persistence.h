// GameSaveManager-backed quest progress provider.
//
// The caller owns the explicit universe save root and invokes this provider at
// player join/leave or controlled save boundaries. It has no tick scheduling,
// content-script, ECS, or transport dependency.

#pragma once

#include "game/quest/quest_progress.h"

#include <string>

namespace snt::game {

class GameSaveQuestProgressPersistence final : public IQuestProgressPersistence {
public:
    explicit GameSaveQuestProgressPersistence(std::string save_dir);

    [[nodiscard]] const std::string& save_dir() const noexcept { return save_dir_; }

    [[nodiscard]] snt::core::Expected<std::vector<QuestProgressRecord>> load_player_progress(
        std::string_view player_id) override;
    [[nodiscard]] snt::core::Expected<void> save_player_progress(
        std::string_view player_id, std::span<const QuestProgressRecord> progress) override;

private:
    std::string save_dir_;
};

}  // namespace snt::game
