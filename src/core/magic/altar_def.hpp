#pragma once

#include <cstdint>
#include <vector>

namespace science_and_theology::magic {

struct AltarOffsets {
    int x = 0;
    int y = 0;
};

struct AltarDef {
    const char* name = "";
    int core_x = 0;
    int core_y = 0;
    std::vector<AltarOffsets> pedestals;
    int max_pedestals = 4;
};

inline AltarDef basic_altar() {
    AltarDef altar;
    altar.name = "basic_altar";
    altar.core_x = 0;
    altar.core_y = 0;
    altar.max_pedestals = 4;
    // Pedestal positions: top, right, bottom, left
    altar.pedestals.push_back({0, -1});   // top
    altar.pedestals.push_back({1, 0});   // right
    altar.pedestals.push_back({0, 1});   // bottom
    altar.pedestals.push_back({-1, 0});  // left
    return altar;
}

inline AltarDef advanced_altar() {
    AltarDef altar;
    altar.name = "advanced_altar";
    altar.core_x = 0;
    altar.core_y = 0;
    altar.max_pedestals = 8;
    altar.pedestals.push_back({0, -1});   // top
    altar.pedestals.push_back({1, -1});   // top-right
    altar.pedestals.push_back({1, 0});   // right
    altar.pedestals.push_back({1, 1});   // bottom-right
    altar.pedestals.push_back({0, 1});   // bottom
    altar.pedestals.push_back({-1, 1});  // bottom-left
    altar.pedestals.push_back({-1, 0});  // left
    altar.pedestals.push_back({-1, -1}); // top-left
    return altar;
}

} // namespace science_and_theology::magic
