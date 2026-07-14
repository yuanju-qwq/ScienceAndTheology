// ScienceAndTheology game executable.
//
// This is the host boundary between game content and the independently
// versioned engine. It owns package discovery and passes explicit roots to the
// engine; no engine API depends on this repository's source-tree layout.

#define SNT_LOG_CHANNEL "game_host"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/runtime_config.h"
#include "engine/client_runtime.h"
#include "game_session_config.h"
#include "science_and_theology_session.h"

#include <filesystem>
#include <memory>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path executable_directory(const char* argv0) {
#if defined(_WIN32)
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length > 0 && length < 32768) {
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__linux__)
    char buffer[PATH_MAX]{};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::absolute(std::filesystem::path(argv0)).parent_path();
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto package_root = executable_directory(argc > 0 ? argv[0] : "science_and_theology");
    snt::core::RuntimePaths runtime_paths{
        .engine_root = (package_root / "engine").string(),
        .game_root = (package_root / "game").string(),
        .user_root = (package_root / "user").string(),
    };

    const auto config_path = snt::core::path_utils::join(
        runtime_paths.game_root, "config/engine.json");
    auto runtime_config = snt::core::load_runtime_config(config_path);
    if (!runtime_config) {
        SNT_LOG_ERROR("Runtime config load failed: %s", runtime_config.error().format().c_str());
        return 1;
    }
    auto session_config = snt::game::load_game_session_config(config_path);
    if (!session_config) {
        SNT_LOG_ERROR("Game session config load failed: %s", session_config.error().format().c_str());
        return 1;
    }

    snt::engine::ClientRuntime runtime;
    auto session = std::make_unique<snt::game::ScienceAndTheologySession>(
        std::move(*session_config));
    if (auto result = runtime.init(*runtime_config, runtime_paths, std::move(session)); !result) {
        SNT_LOG_ERROR("Runtime startup failed: %s", result.error().format().c_str());
        return 1;
    }
    runtime.run();
    runtime.shutdown();
    return 0;
}
