// ScienceAndTheology game executable.
//
// This is the host boundary between game content and the independently
// versioned engine. It owns package discovery and passes explicit roots to the
// engine; no engine API depends on this repository's source-tree layout.

#define SNT_LOG_CHANNEL "game_host"
#include "core/engine_config.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "engine/engine.h"

#include <filesystem>
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
    auto config_result = snt::core::load_engine_config(config_path);
    if (!config_result) {
        SNT_LOG_ERROR("Game config load failed: %s", config_result.error().format().c_str());
        return 1;
    }

    snt::engine::Engine engine;
    if (auto result = engine.init(*config_result, runtime_paths); !result) {
        SNT_LOG_ERROR("Engine startup failed: %s", result.error().format().c_str());
        return 1;
    }
    engine.run();
    engine.shutdown();
    return 0;
}