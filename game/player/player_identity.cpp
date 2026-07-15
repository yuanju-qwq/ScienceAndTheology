// Stable game-owned player account identity implementation.

#define SNT_LOG_CHANNEL "game.identity"
#include "game/player/player_identity.h"

#include "core/error.h"

#include <charconv>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool is_ascii_whitespace(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

[[nodiscard]] bool is_utf8_continuation(unsigned char value) noexcept {
    return (value & 0xC0u) == 0x80u;
}

[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept {
    for (size_t index = 0; index < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7Fu) {
            ++index;
            continue;
        }

        size_t continuation_count = 0;
        uint32_t codepoint = 0;
        if (first >= 0xC2u && first <= 0xDFu) {
            continuation_count = 1;
            codepoint = first & 0x1Fu;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            continuation_count = 2;
            codepoint = first & 0x0Fu;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            continuation_count = 3;
            codepoint = first & 0x07u;
        } else {
            return false;
        }

        if (value.size() - index <= continuation_count) return false;
        for (size_t offset = 1; offset <= continuation_count; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
            if (!is_utf8_continuation(continuation)) return false;
            codepoint = (codepoint << 6u) | (continuation & 0x3Fu);
        }
        if ((continuation_count == 2 && codepoint < 0x800u) ||
            (continuation_count == 3 && codepoint < 0x10000u) ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu) || codepoint > 0x10FFFFu) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

[[nodiscard]] bool is_valid_steam_account_id(std::string_view account_id) noexcept {
    constexpr std::string_view kPrefix = "steam:";
    if (!account_id.starts_with(kPrefix) || account_id.size() == kPrefix.size()) return false;

    uint64_t steam_id = 0;
    const char* begin = account_id.data() + kPrefix.size();
    const char* end = account_id.data() + account_id.size();
    const auto [parsed_end, parse_error] = std::from_chars(begin, end, steam_id);
    return parse_error == std::errc{} && parsed_end == end && steam_id != 0;
}

}  // namespace

bool is_valid_player_identity_provider(PlayerIdentityProvider provider) noexcept {
    switch (provider) {
        case PlayerIdentityProvider::kSteam:
        case PlayerIdentityProvider::kLocalName:
            return true;
    }
    return false;
}

const char* player_identity_provider_name(PlayerIdentityProvider provider) noexcept {
    switch (provider) {
        case PlayerIdentityProvider::kSteam: return "steam";
        case PlayerIdentityProvider::kLocalName: return "local_name";
    }
    return "unknown";
}

snt::core::Expected<void> validate_player_display_name(std::string_view display_name) {
    if (display_name.empty() || display_name.size() > kMaxPlayerDisplayNameBytes ||
        !is_valid_utf8(display_name)) {
        return invalid_argument("Player display name must be non-empty valid UTF-8 within 48 bytes");
    }
    const unsigned char first = static_cast<unsigned char>(display_name.front());
    const unsigned char last = static_cast<unsigned char>(display_name.back());
    if (is_ascii_whitespace(first) || is_ascii_whitespace(last)) {
        return invalid_argument("Player display name must not start or end with whitespace");
    }
    for (const unsigned char value : display_name) {
        if (value < 0x20u || value == 0x7Fu) {
            return invalid_argument("Player display name must not contain ASCII control bytes");
        }
    }
    return {};
}

snt::core::Expected<void> validate_player_identity(const PlayerIdentity& identity) {
    if (!is_valid_player_identity_provider(identity.provider)) {
        return invalid_argument("Player identity provider is invalid");
    }
    if (auto result = validate_player_display_name(identity.display_name); !result) {
        return result.error();
    }
    if (identity.account_id.empty() || identity.account_id.size() > kMaxPlayerAccountIdBytes) {
        return invalid_argument("Player account id is invalid");
    }

    switch (identity.provider) {
        case PlayerIdentityProvider::kSteam:
            if (!is_valid_steam_account_id(identity.account_id)) {
                return invalid_argument("Steam player account id is invalid");
            }
            break;
        case PlayerIdentityProvider::kLocalName:
            if (identity.account_id != "local-name:" + identity.display_name) {
                return invalid_argument("Local-name player account id does not match its display name");
            }
            break;
    }
    return {};
}

snt::core::Expected<PlayerIdentity> make_steam_player_identity(
    uint64_t steam_id, std::string display_name) {
    if (steam_id == 0) return invalid_argument("SteamID64 must not be zero");
    if (display_name.empty()) display_name = "Steam " + std::to_string(steam_id);

    PlayerIdentity identity{
        .provider = PlayerIdentityProvider::kSteam,
        .account_id = "steam:" + std::to_string(steam_id),
        .display_name = std::move(display_name),
    };
    if (auto result = validate_player_identity(identity); !result) return result.error();
    return identity;
}

snt::core::Expected<PlayerIdentity> make_local_name_player_identity(std::string display_name) {
    PlayerIdentity identity{
        .provider = PlayerIdentityProvider::kLocalName,
        .account_id = "local-name:" + display_name,
        .display_name = std::move(display_name),
    };
    if (auto result = validate_player_identity(identity); !result) return result.error();
    return identity;
}

}  // namespace snt::game
