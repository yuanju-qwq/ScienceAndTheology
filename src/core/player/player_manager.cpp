#include "player_manager.hpp"

namespace science_and_theology {

bool PlayerManager::register_player(PlayerId id,
                                    gt::Inventory* inventory,
                                    gt::Equipment* equipment) {
    if (id == kInvalidPlayerId) return false;
    if (players_.find(id) != players_.end()) return false;

    auto state = std::make_unique<PlayerState>();
    state->id = id;
    state->inventory = inventory;
    state->equipment = equipment;
    players_[id] = std::move(state);
    return true;
}

bool PlayerManager::unregister_player(PlayerId id) {
    return players_.erase(id) > 0;
}

bool PlayerManager::bind_inventory(PlayerId id, gt::Inventory* inventory) {
    auto it = players_.find(id);
    if (it == players_.end()) return false;
    it->second->inventory = inventory;
    return true;
}

bool PlayerManager::bind_equipment(PlayerId id, gt::Equipment* equipment) {
    auto it = players_.find(id);
    if (it == players_.end()) return false;
    it->second->equipment = equipment;
    return true;
}

bool PlayerManager::set_player_chunk(PlayerId id,
                                     const std::string& dimension,
                                     int cx, int cy, int cz) {
    auto it = players_.find(id);
    if (it == players_.end()) return false;
    PlayerState& state = *it->second;
    state.current_dimension = dimension;
    state.current_cx = cx;
    state.current_cy = cy;
    state.current_cz = cz;
    return true;
}

bool PlayerManager::has_player(PlayerId id) const {
    return players_.find(id) != players_.end();
}

PlayerState* PlayerManager::get_player(PlayerId id) {
    auto it = players_.find(id);
    if (it == players_.end()) return nullptr;
    return it->second.get();
}

const PlayerState* PlayerManager::get_player(PlayerId id) const {
    auto it = players_.find(id);
    if (it == players_.end()) return nullptr;
    return it->second.get();
}

std::vector<PlayerId> PlayerManager::all_ids() const {
    std::vector<PlayerId> ids;
    ids.reserve(players_.size());
    for (const auto& pair : players_) {
        ids.push_back(pair.first);
    }
    return ids;
}

void PlayerManager::clear() {
    players_.clear();
}

} // namespace science_and_theology
