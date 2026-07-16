// GameSaveManager-backed authoritative player-state provider.
//
// This adapter keeps the SNTP file format in the world-save module while the
// dedicated server lifecycle decides when it is safe to load or save. It has
// no transport, ECS, script, or UI ownership.

#pragma once

#include "game/player/player_state.h"

#include <string>

namespace snt::game {

class GameSavePlayerStatePersistence final : public IGamePlayerStatePersistence {
public:
    explicit GameSavePlayerStatePersistence(std::string save_dir);

    [[nodiscard]] snt::core::Expected<std::optional<GamePlayerPersistentState>>
    load_player_state(std::string_view account_id) override;
    [[nodiscard]] snt::core::Expected<void> save_player_state(
        std::string_view account_id, const GamePlayerPersistentState& state) override;

    [[nodiscard]] const std::string& save_dir() const noexcept { return save_dir_; }

private:
    std::string save_dir_;
};

}  // namespace snt::game
