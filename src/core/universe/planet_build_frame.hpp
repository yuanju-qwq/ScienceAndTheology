#pragma once

#include <cstdint>

#include "universe_types.hpp"

namespace science_and_theology {

enum class BuildMode : uint8_t {
    PlanetLocal = 0,
    GlobalAxes = 1,
};

enum class LocalBuildDirection : uint8_t {
    Up = 0,
    Down = 1,
    Horizontal = 2,
};

struct BuildVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Resolves continuous radial/tangent construction intent onto the six
// neighbors of the fixed axis-aligned voxel grid.
class PlanetBuildFrame {
public:
    PlanetBuildFrame(double center_x, double center_y, double center_z);

    Direction local_up(const PlanetLocalBlockPos& anchor) const;
    Direction local_down(const PlanetLocalBlockPos& anchor) const;
    Direction local_horizontal(const PlanetLocalBlockPos& anchor,
                               const BuildVector& requested) const;
    LocalBuildDirection classify(const PlanetLocalBlockPos& anchor,
                                 Direction direction) const;

    static Direction snap_global_axis(const BuildVector& requested,
                                      Direction fallback = Direction::PosY);
    static bool is_axis_direction(Direction direction);

private:
    BuildVector radial_up(const PlanetLocalBlockPos& anchor) const;

    BuildVector center_;
};

} // namespace science_and_theology
