// SNT engine main entry.
// P2.B1: reduced to Engine class init/run/shutdown. All subsystem setup
// + the per-frame loop live in engine/engine.cpp.

#include "engine/engine.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    snt::engine::Engine engine;
    if (!engine.init()) {
        std::fprintf(stderr, "[snt_engine] Engine init failed\n");
        return 1;
    }
    engine.run();
    engine.shutdown();
    return 0;
}
