// Stable game-owned player account identity.
//
// Account ids, display names, and authentication evidence are deliberately
// separated. A Steam account is identified by a verified SteamID64, while a
// local-name account is identified by the exact validated UTF-8 name. The two
// namespaces never overlap, even when their display names are identical.

#pragma once

#include "core/expected.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

inline constexpr size_t kMaxPlayerDisplayNameBytes = 48;
inline constexpr size_t kMaxPlayerAccountIdBytes = 128;

enum class PlayerIdentityProvider : uint8_t {
    kSteam = 1,
    kLocalName = 2,
};

struct PlayerIdentity {
    PlayerIdentityProvider provider = PlayerIdentityProvider::kLocalName;
    std::string account_id;
    std::string display_name;
};

[[nodiscard]] bool is_valid_player_identity_provider(PlayerIdentityProvider provider) noexcept;
[[nodiscard]] const char* player_identity_provider_name(PlayerIdentityProvider provider) noexcept;

// Display names are UTF-8, non-empty, limited to the game protocol's name
// size, and cannot have leading/trailing ASCII whitespace or control bytes.
[[nodiscard]] snt::core::Expected<void> validate_player_display_name(
    std::string_view display_name);
[[nodiscard]] snt::core::Expected<void> validate_player_identity(
    const PlayerIdentity& identity);

[[nodiscard]] snt::core::Expected<PlayerIdentity> make_steam_player_identity(
    uint64_t steam_id, std::string display_name);
[[nodiscard]] snt::core::Expected<PlayerIdentity> make_local_name_player_identity(
    std::string display_name);

}  // namespace snt::game
