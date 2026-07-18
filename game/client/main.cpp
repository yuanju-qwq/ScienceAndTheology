// ScienceAndTheology graphical game host.
//
// Package discovery is shared with the dedicated server. This host adds only
// the ClientRuntime and graphical-session composition layer.

#define SNT_LOG_CHANNEL "game_host"
#include "core/log.h"
#include "core/path_utils.h"
#include "engine/client_runtime.h"
#include "game/localization/localization.h"
#include "game/network/game_replication_protocol.h"
#include "game/runtime/local_player_name_prompt.h"
#include "game/runtime/runtime_package.h"
#include "science_and_theology_session.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct ClientOptions {
    bool show_help = false;
    bool lan_browser = false;
    std::optional<std::string> server_password;
};

snt::core::Expected<ClientOptions> parse_client_options(int argc, char* argv[]) {
    ClientOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }
        if (argument == "--lan") {
            options.lan_browser = true;
            continue;
        }
        if (argument != "--server-password") {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "Unknown client argument: " + std::string(argument)};
        }
        if (index + 1 >= argc) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "--server-password requires a non-empty value"};
        }
        std::string password = argv[++index];
        if (password.empty() ||
            password.size() > snt::game::replication::kMaxGameServerPasswordBytes) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "--server-password must contain 1-256 bytes"};
        }
        options.server_password = std::move(password);
    }
    return options;
}

void print_usage() {
    std::puts("Usage: science_and_theology [--lan] [--server-password <password>]");
    std::puts("--lan opens the configured LAN server browser.");
    std::puts("Without --lan, a supplied password enables the configured direct TCP+UDP connection.");
}

}  // namespace

int main(int argc, char* argv[]) {
    auto options = parse_client_options(argc, argv);
    if (!options) {
        SNT_LOG_ERROR("Client arguments are invalid: %s", options.error().format().c_str());
        print_usage();
        return 2;
    }
    if (options->show_help) {
        print_usage();
        return 0;
    }

    auto package = snt::game::load_runtime_package(
        argc > 0 ? argv[0] : "science_and_theology");
    if (!package) {
        SNT_LOG_ERROR("Runtime package load failed: %s", package.error().format().c_str());
        return 1;
    }

    auto localization = snt::game::localization::LocalizationService::load(
        std::make_shared<snt::game::localization::JsonFileLocalizationCatalogSource>(
            snt::core::path_utils::join(package->paths.game_root, "locales")),
        {
            .locale = package->runtime_config.ui.locale,
            .fallback_locale = "en",
        });
    if (!localization) {
        SNT_LOG_ERROR("Localization startup failed: %s", localization.error().format().c_str());
        return 1;
    }

    // Steamworks integration will inject an ISteamPlayerIdentityProvider here.
    // Without an authenticated Steam provider, the first local launch opens
    // the SDL name window and persists the resulting local-name identity.
    snt::game::SdlLocalPlayerNamePrompt local_name_prompt;
    auto player_identity = snt::game::resolve_client_player_identity(
        package->paths.user_root, local_name_prompt);
    if (!player_identity) {
        SNT_LOG_ERROR("Player identity resolution failed: %s", player_identity.error().format().c_str());
        return 1;
    }

    if (options->lan_browser) {
        package->session_config.client_network.lan_discovery_enabled = true;
        package->session_config.client_network.enabled = false;
    }
    if (options->server_password && !package->session_config.client_network.lan_discovery_enabled) {
        package->session_config.client_network.enabled = true;
    }
    std::string server_password;
    if (options->server_password) server_password = std::move(*options->server_password);

    snt::engine::ClientRuntime runtime;
    auto session = std::make_unique<snt::game::ScienceAndTheologyClientSession>(
        std::move(package->session_config),
        std::move(*localization),
        snt::game::replication::GameClientAuthentication{
            .local_identity = std::move(*player_identity),
            .server_password = std::move(server_password),
        });
    if (auto result = runtime.init(package->runtime_config, package->paths, std::move(session)); !result) {
        SNT_LOG_ERROR("Runtime startup failed: %s", result.error().format().c_str());
        return 1;
    }
    runtime.run();
    runtime.shutdown();
    return 0;
}
