// GameSaveManager-backed authoritative player-state provider implementation.

#include "game/world/save/player_state_persistence.h"

#include "game/world/save/save_manager.h"

#include <utility>

namespace snt::game {

GameSavePlayerStatePersistence::GameSavePlayerStatePersistence(std::string save_dir)
    : save_dir_(std::move(save_dir)) {}

snt::core::Expected<std::optional<GamePlayerPersistentState>>
GameSavePlayerStatePersistence::load_player_state(std::string_view account_id) {
    return GameSaveManager::load_player_state(save_dir_, account_id);
}

snt::core::Expected<void> GameSavePlayerStatePersistence::save_player_state(
    std::string_view account_id, const GamePlayerPersistentState& state) {
    return GameSaveManager::save_player_state(save_dir_, account_id, state);
}

}  // namespace snt::game
