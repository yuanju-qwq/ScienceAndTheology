// SNT engine main entry.
// P2.B1: reduced to Engine class init/run/shutdown. All subsystem setup
// + the per-frame loop live in engine/engine.cpp.

#define SNT_LOG_CHANNEL "app"
#include "core/log.h"

#include "engine/engine.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    snt::engine::Engine engine;
    if (auto r = engine.init(); !r) {
        SNT_LOG_ERROR("Engine init failed: %s", r.error().format().c_str());
        return 1;
    }
    engine.run();
    engine.shutdown();
    return 0;
}
