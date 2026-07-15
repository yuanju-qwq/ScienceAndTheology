// Local non-Steam player-name profile persistence.
//
// This store owns only the installation-local display name under user_root.
// World state remains in GameSaveManager, and Steam identities are never
// copied into this local profile because they are re-verified every launch.

#pragma once

#include "game/player/player_identity.h"

#include <optional>
#include <string>
#include <string_view>

namespace snt::game {

class LocalPlayerProfileStore final {
public:
    explicit LocalPlayerProfileStore(std::string user_root);

    [[nodiscard]] snt::core::Expected<std::optional<PlayerIdentity>> load() const;
    [[nodiscard]] snt::core::Expected<void> save(const PlayerIdentity& identity) const;

    [[nodiscard]] const std::string& user_root() const noexcept { return user_root_; }

private:
    std::string user_root_;
};

// Steamworks integration implements this without exposing Steam headers to the
// core game identity target. A missing provider means the executable was not
// launched with an available Steam identity and should use the local profile.
struct SteamIdentityInfo {
    uint64_t steam_id = 0;
    std::string display_name;
};

class ISteamPlayerIdentityProvider {
public:
    virtual ~ISteamPlayerIdentityProvider() = default;
    virtual snt::core::Expected<std::optional<SteamIdentityInfo>> current_identity() = 0;
};

// The SDL startup dialog is only one implementation. Tests and other hosts
// can supply their own prompt without adding a presentation dependency here.
class ILocalPlayerNamePrompt {
public:
    virtual ~ILocalPlayerNamePrompt() = default;
    virtual snt::core::Expected<std::string> request_local_player_name() = 0;
};

// Steam takes precedence whenever its provider returns an authenticated local
// account. Otherwise a persisted local name is used; only a missing profile
// opens the prompt. A Steam provider failure is never silently downgraded to
// a local account because that could select the wrong persistent player data.
[[nodiscard]] snt::core::Expected<PlayerIdentity> resolve_client_player_identity(
    std::string_view user_root, ILocalPlayerNamePrompt& local_name_prompt,
    ISteamPlayerIdentityProvider* steam_provider = nullptr);

}  // namespace snt::game
