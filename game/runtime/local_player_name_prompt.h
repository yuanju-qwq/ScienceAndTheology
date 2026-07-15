// SDL startup window for first-run local player-name selection.
//
// This presentation adapter is intentionally outside snt_game_identity. The
// identity core can be used by headless hosts and tests through the prompt
// interface without pulling SDL into game account or server code.

#pragma once

#include "game/player/local_player_profile_store.h"

namespace snt::game {

class SdlLocalPlayerNamePrompt final : public ILocalPlayerNamePrompt {
public:
    [[nodiscard]] snt::core::Expected<std::string> request_local_player_name() override;
};

}  // namespace snt::game
