// Current-only player source-law persistence codec.
//
// The game player-state envelope owns file framing. This codec owns only the
// versioned body payload stored in GamePlayerOrganState and deliberately
// rejects every legacy source-law schema.

#pragma once

#include "core/expected.h"
#include "game/player/player_state.h"
#include "game/source_law/source_law_body_state.h"

#include <string_view>

namespace snt::game::source_law {

inline constexpr std::string_view kSourceLawPlayerOrganSchemaId =
    "snt:source_law_body";
inline constexpr uint16_t kSourceLawPlayerOrganSchemaVersion = 1;

class SourceLawPersistenceCodec final : public IGamePlayerOrganStateCodec {
public:
    [[nodiscard]] snt::core::Expected<GamePlayerOrganState> encode(
        const PlayerSourceLawState& state) const;
    [[nodiscard]] snt::core::Expected<PlayerSourceLawState> decode(
        const GamePlayerOrganState& state) const;

    [[nodiscard]] snt::core::Expected<void> validate_organ_state(
        const GamePlayerOrganState& state) const override;
};

}  // namespace snt::game::source_law
