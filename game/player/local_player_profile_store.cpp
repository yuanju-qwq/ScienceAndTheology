// Local non-Steam player-name profile persistence implementation.

#define SNT_LOG_CHANNEL "game.identity"
#include "game/player/local_player_profile_store.h"

#include "core/error.h"
#include "core/log.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace snt::game {
namespace {

constexpr char kLocalProfileMagic[] = {'S', 'N', 'T', 'I'};
constexpr uint8_t kLocalProfileVersion = 1;

[[nodiscard]] snt::core::Error file_error(std::string message) {
    return {snt::core::ErrorCode::kFileOpenFailed, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_profile(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] fs::path profile_directory(std::string_view user_root) {
    return fs::u8path(std::string(user_root)) / "profile";
}

[[nodiscard]] fs::path profile_path(std::string_view user_root) {
    return profile_directory(user_root) / "local_player_identity.bin";
}

template <typename T>
bool write_value(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return file.good();
}

template <typename T>
bool read_value(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return file.good();
}

[[nodiscard]] snt::core::Expected<void> ensure_profile_directory(const fs::path& directory) {
    std::error_code error;
    if (fs::exists(directory, error)) {
        if (error || !fs::is_directory(directory, error)) {
            return file_error("Local player profile directory is not usable");
        }
        return {};
    }
    if (!fs::create_directories(directory, error) || error) {
        return file_error("Unable to create local player profile directory: " + error.message());
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> replace_profile_file(
    const fs::path& primary_path, const fs::path& temporary_path, const fs::path& backup_path) {
    std::error_code error;
    const bool has_primary = fs::exists(primary_path, error);
    if (error) return file_error("Unable to inspect local player profile: " + error.message());
    const bool has_backup = fs::exists(backup_path, error);
    if (error) return file_error("Unable to inspect local player profile backup: " + error.message());

    if (!has_primary && has_backup) {
        fs::rename(backup_path, primary_path, error);
        if (error) return file_error("Unable to recover local player profile: " + error.message());
    } else if (has_primary && has_backup) {
        fs::remove(backup_path, error);
        if (error) return file_error("Unable to remove stale local player profile backup: " + error.message());
    }

    if (fs::exists(primary_path, error)) {
        if (error) return file_error("Unable to inspect local player profile primary: " + error.message());
        fs::rename(primary_path, backup_path, error);
        if (error) return file_error("Unable to stage local player profile: " + error.message());
    } else if (error) {
        return file_error("Unable to inspect local player profile primary: " + error.message());
    }

    fs::rename(temporary_path, primary_path, error);
    if (!error) {
        fs::remove(backup_path, error);
        return {};
    }

    const std::string promotion_error = error.message();
    std::error_code restore_error;
    if (!fs::exists(primary_path, restore_error) && !restore_error &&
        fs::exists(backup_path, restore_error) && !restore_error) {
        fs::rename(backup_path, primary_path, restore_error);
    }
    return file_error("Unable to promote local player profile: " + promotion_error);
}

}  // namespace

LocalPlayerProfileStore::LocalPlayerProfileStore(std::string user_root)
    : user_root_(std::move(user_root)) {}

snt::core::Expected<std::optional<PlayerIdentity>> LocalPlayerProfileStore::load() const {
    if (user_root_.empty()) return invalid_profile("Local player profile user root must not be empty");

    const fs::path primary_path = profile_path(user_root_);
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code error;
    const bool has_primary = fs::exists(primary_path, error);
    if (error) return file_error("Unable to inspect local player profile: " + error.message());

    fs::path selected_path = primary_path;
    if (!has_primary) {
        const bool has_backup = fs::exists(backup_path, error);
        if (error) return file_error("Unable to inspect local player profile backup: " + error.message());
        if (!has_backup) return std::optional<PlayerIdentity>{};
        selected_path = backup_path;
        SNT_LOG_WARN("Local player profile primary file is missing; using recovery backup");
    }

    std::ifstream file(selected_path, std::ios::binary);
    if (!file.is_open()) return file_error("Unable to open local player profile");

    char magic[sizeof(kLocalProfileMagic)] = {};
    uint8_t version = 0;
    uint16_t name_length = 0;
    file.read(magic, sizeof(magic));
    if (!file.good() || std::memcmp(magic, kLocalProfileMagic, sizeof(magic)) != 0 ||
        !read_value(file, version) || version != kLocalProfileVersion ||
        !read_value(file, name_length) || name_length == 0 ||
        name_length > kMaxPlayerDisplayNameBytes) {
        return invalid_profile("Local player profile is corrupt or not the current format");
    }

    std::string display_name(name_length, '\0');
    file.read(display_name.data(), static_cast<std::streamsize>(name_length));
    if (!file.good() || file.peek() != std::char_traits<char>::eof() || file.bad()) {
        return invalid_profile("Local player profile has trailing or unreadable data");
    }

    auto identity = make_local_name_player_identity(std::move(display_name));
    if (!identity) {
        auto profile_error = identity.error();
        profile_error.with_context("LocalPlayerProfileStore::load");
        return profile_error;
    }
    SNT_LOG_INFO("Loaded persisted local player identity '%s'", identity->account_id.c_str());
    return std::optional<PlayerIdentity>{std::move(*identity)};
}

snt::core::Expected<void> LocalPlayerProfileStore::save(const PlayerIdentity& identity) const {
    if (user_root_.empty()) return invalid_profile("Local player profile user root must not be empty");
    if (identity.provider != PlayerIdentityProvider::kLocalName) {
        return invalid_profile("Only local-name identities may be saved in the local player profile");
    }
    if (auto result = validate_player_identity(identity); !result) return result.error();

    const fs::path directory = profile_directory(user_root_);
    if (auto result = ensure_profile_directory(directory); !result) return result.error();

    const fs::path primary_path = profile_path(user_root_);
    fs::path temporary_path = primary_path;
    temporary_path += ".tmp";
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code remove_error;
    fs::remove(temporary_path, remove_error);
    if (remove_error) return file_error("Unable to remove stale local player profile temporary file");

    {
        std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return file_error("Unable to create local player profile temporary file");

        const uint16_t name_length = static_cast<uint16_t>(identity.display_name.size());
        file.write(kLocalProfileMagic, sizeof(kLocalProfileMagic));
        const bool wrote = file.good() && write_value(file, kLocalProfileVersion) &&
                           write_value(file, name_length) &&
                           (name_length == 0 || (file.write(identity.display_name.data(), name_length), file.good()));
        file.flush();
        const bool flushed = file.good();
        file.close();
        if (!wrote || !flushed || file.fail()) {
            std::error_code cleanup_error;
            fs::remove(temporary_path, cleanup_error);
            return file_error("Unable to write complete local player profile");
        }
    }

    if (auto result = replace_profile_file(primary_path, temporary_path, backup_path); !result) {
        std::error_code cleanup_error;
        fs::remove(temporary_path, cleanup_error);
        return result.error();
    }
    SNT_LOG_INFO("Persisted local player identity '%s'", identity.account_id.c_str());
    return {};
}

snt::core::Expected<PlayerIdentity> resolve_client_player_identity(
    std::string_view user_root, ILocalPlayerNamePrompt& local_name_prompt,
    ISteamPlayerIdentityProvider* steam_provider) {
    if (user_root.empty()) return invalid_profile("Client identity user root must not be empty");

    if (steam_provider != nullptr) {
        auto steam_identity = steam_provider->current_identity();
        if (!steam_identity) {
            auto error = steam_identity.error();
            error.with_context("resolve_client_player_identity(Steam)");
            return error;
        }
        if (steam_identity->has_value()) {
            auto identity = make_steam_player_identity((*steam_identity)->steam_id,
                                                       std::move((*steam_identity)->display_name));
            if (!identity) {
                auto error = identity.error();
                error.with_context("resolve_client_player_identity(Steam identity)");
                return error;
            }
            SNT_LOG_INFO("Resolved Steam player identity '%s'", identity->account_id.c_str());
            return identity;
        }
    }

    LocalPlayerProfileStore profile{std::string(user_root)};
    auto loaded = profile.load();
    if (!loaded) {
        auto error = loaded.error();
        error.with_context("resolve_client_player_identity(local profile)");
        return error;
    }
    if (loaded->has_value()) return std::move(**loaded);

    auto display_name = local_name_prompt.request_local_player_name();
    if (!display_name) {
        auto error = display_name.error();
        error.with_context("resolve_client_player_identity(local name prompt)");
        return error;
    }
    auto identity = make_local_name_player_identity(std::move(*display_name));
    if (!identity) {
        auto error = identity.error();
        error.with_context("resolve_client_player_identity(local name)");
        return error;
    }
    if (auto result = profile.save(*identity); !result) {
        auto error = result.error();
        error.with_context("resolve_client_player_identity(save local profile)");
        return error;
    }
    return identity;
}

}  // namespace snt::game
