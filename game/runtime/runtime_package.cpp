// Packaged game-runtime bootstrap implementation.

#define SNT_LOG_CHANNEL "game.runtime_package"
#include "runtime_package.h"

#include "core/log.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace snt::game {
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
    const std::filesystem::path fallback = argv0 != nullptr && argv0[0] != '\0'
        ? std::filesystem::path(argv0)
        : std::filesystem::path("science_and_theology");
    return std::filesystem::absolute(fallback).parent_path();
}

}  // namespace

snt::core::Expected<RuntimePackage> load_runtime_package(const char* argv0) {
    const auto package_root = executable_directory(argv0);
    snt::core::RuntimePaths paths{
        .engine_root = (package_root / "engine").string(),
        .game_root = (package_root / "game").string(),
        .user_root = (package_root / "user").string(),
    };

    const auto config_path = snt::core::path_utils::join(paths.game_root, "config/engine.json");
    auto runtime_config = snt::core::load_runtime_config(config_path);
    if (!runtime_config) {
        auto error = runtime_config.error();
        error.with_context("load_runtime_package(RuntimeConfig)");
        return error;
    }
    auto session_config = load_game_session_config(config_path);
    if (!session_config) {
        auto error = session_config.error();
        error.with_context("load_runtime_package(GameSessionConfig)");
        return error;
    }

    SNT_LOG_INFO("Resolved runtime package: engine='%s', game='%s', user='%s'",
                 paths.engine_root.c_str(), paths.game_root.c_str(), paths.user_root.c_str());
    return RuntimePackage{
        .paths = std::move(paths),
        .runtime_config = std::move(*runtime_config),
        .session_config = std::move(*session_config),
    };
}

}  // namespace snt::game
