// ScienceAndTheology graphical game host.
//
// Package discovery is shared with the dedicated server. This host adds only
// the ClientRuntime and graphical-session composition layer.

#define SNT_LOG_CHANNEL "game_host"
#include "core/log.h"
#include "engine/client_runtime.h"
#include "game/runtime/runtime_package.h"
#include "science_and_theology_session.h"

#include <memory>

int main(int argc, char* argv[]) {
    auto package = snt::game::load_runtime_package(
        argc > 0 ? argv[0] : "science_and_theology");
    if (!package) {
        SNT_LOG_ERROR("Runtime package load failed: %s", package.error().format().c_str());
        return 1;
    }

    snt::engine::ClientRuntime runtime;
    auto session = std::make_unique<snt::game::ScienceAndTheologyClientSession>(
        std::move(package->session_config));
    if (auto result = runtime.init(package->runtime_config, package->paths, std::move(session)); !result) {
        SNT_LOG_ERROR("Runtime startup failed: %s", result.error().format().c_str());
        return 1;
    }
    runtime.run();
    runtime.shutdown();
    return 0;
}
