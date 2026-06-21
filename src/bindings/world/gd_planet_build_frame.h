#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

namespace science_and_theology {

class GDPlanetBuildFrame : public godot::RefCounted {
    GDCLASS(GDPlanetBuildFrame, godot::RefCounted)

public:
    enum BuildMode {
        BUILD_MODE_PLANET_LOCAL = 0,
        BUILD_MODE_GLOBAL_AXES = 1,
    };

    enum LocalDirection {
        LOCAL_DIRECTION_UP = 0,
        LOCAL_DIRECTION_DOWN = 1,
        LOCAL_DIRECTION_HORIZONTAL = 2,
    };

    static godot::Vector3i local_up(
        const godot::Vector3i& anchor, const godot::Vector3& planet_center);
    static godot::Vector3i local_down(
        const godot::Vector3i& anchor, const godot::Vector3& planet_center);
    static godot::Vector3i local_horizontal(
        const godot::Vector3i& anchor, const godot::Vector3& planet_center,
        const godot::Vector3& requested_direction);
    static godot::Vector3i snap_global_axis(
        const godot::Vector3& requested_direction);
    static int64_t classify_direction(
        const godot::Vector3i& anchor, const godot::Vector3& planet_center,
        const godot::Vector3i& direction);
    static bool is_axis_direction(const godot::Vector3i& direction);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology

VARIANT_ENUM_CAST(science_and_theology::GDPlanetBuildFrame::BuildMode)
VARIANT_ENUM_CAST(science_and_theology::GDPlanetBuildFrame::LocalDirection)
