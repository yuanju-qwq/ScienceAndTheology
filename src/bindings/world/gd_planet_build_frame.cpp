#include "gd_planet_build_frame.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/universe/planet_build_frame.hpp"

namespace science_and_theology {
namespace {

static_assert(static_cast<int>(BuildMode::PlanetLocal) ==
              GDPlanetBuildFrame::BUILD_MODE_PLANET_LOCAL);
static_assert(static_cast<int>(BuildMode::GlobalAxes) ==
              GDPlanetBuildFrame::BUILD_MODE_GLOBAL_AXES);
static_assert(static_cast<int>(LocalBuildDirection::Up) ==
              GDPlanetBuildFrame::LOCAL_DIRECTION_UP);
static_assert(static_cast<int>(LocalBuildDirection::Down) ==
              GDPlanetBuildFrame::LOCAL_DIRECTION_DOWN);
static_assert(static_cast<int>(LocalBuildDirection::Horizontal) ==
              GDPlanetBuildFrame::LOCAL_DIRECTION_HORIZONTAL);

PlanetLocalBlockPos to_local_pos(const godot::Vector3i& value) {
    return PlanetLocalBlockPos{value.x, value.y, value.z};
}

BuildVector to_build_vector(const godot::Vector3& value) {
    return BuildVector{value.x, value.y, value.z};
}

Direction to_direction(const godot::Vector3i& value) {
    return PlanetBuildFrame::snap_global_axis(
        BuildVector{
            static_cast<double>(value.x),
            static_cast<double>(value.y),
            static_cast<double>(value.z)},
        Direction::COUNT);
}

godot::Vector3i to_vector3i(Direction direction) {
    const GlobalBlockPos offset = direction_offset(direction);
    return godot::Vector3i(
        static_cast<int32_t>(offset.x),
        static_cast<int32_t>(offset.y),
        static_cast<int32_t>(offset.z));
}

PlanetBuildFrame make_frame(const godot::Vector3& center) {
    return PlanetBuildFrame(center.x, center.y, center.z);
}

} // namespace

godot::Vector3i GDPlanetBuildFrame::local_up(
        const godot::Vector3i& anchor,
        const godot::Vector3& planet_center) {
    return to_vector3i(make_frame(planet_center).local_up(to_local_pos(anchor)));
}

godot::Vector3i GDPlanetBuildFrame::local_down(
        const godot::Vector3i& anchor,
        const godot::Vector3& planet_center) {
    return to_vector3i(make_frame(planet_center).local_down(to_local_pos(anchor)));
}

godot::Vector3i GDPlanetBuildFrame::local_horizontal(
        const godot::Vector3i& anchor,
        const godot::Vector3& planet_center,
        const godot::Vector3& requested_direction) {
    return to_vector3i(make_frame(planet_center).local_horizontal(
        to_local_pos(anchor), to_build_vector(requested_direction)));
}

godot::Vector3i GDPlanetBuildFrame::snap_global_axis(
        const godot::Vector3& requested_direction) {
    return to_vector3i(PlanetBuildFrame::snap_global_axis(
        to_build_vector(requested_direction)));
}

int64_t GDPlanetBuildFrame::classify_direction(
        const godot::Vector3i& anchor,
        const godot::Vector3& planet_center,
        const godot::Vector3i& direction) {
    if (!is_axis_direction(direction)) {
        return -1;
    }
    return static_cast<int64_t>(make_frame(planet_center).classify(
        to_local_pos(anchor), to_direction(direction)));
}

bool GDPlanetBuildFrame::is_axis_direction(
        const godot::Vector3i& direction) {
    const int32_t magnitude =
        std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
    return magnitude == 1;
}

void GDPlanetBuildFrame::_bind_methods() {
    using B = GDPlanetBuildFrame;
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("local_up", "anchor", "planet_center"), &B::local_up);
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("local_down", "anchor", "planet_center"), &B::local_down);
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("local_horizontal", "anchor", "planet_center",
                        "requested_direction"), &B::local_horizontal);
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("snap_global_axis", "requested_direction"),
        &B::snap_global_axis);
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("classify_direction", "anchor", "planet_center",
                        "direction"), &B::classify_direction);
    godot::ClassDB::bind_static_method("GDPlanetBuildFrame",
        godot::D_METHOD("is_axis_direction", "direction"),
        &B::is_axis_direction);

    BIND_ENUM_CONSTANT(BUILD_MODE_PLANET_LOCAL);
    BIND_ENUM_CONSTANT(BUILD_MODE_GLOBAL_AXES);
    BIND_ENUM_CONSTANT(LOCAL_DIRECTION_UP);
    BIND_ENUM_CONSTANT(LOCAL_DIRECTION_DOWN);
    BIND_ENUM_CONSTANT(LOCAL_DIRECTION_HORIZONTAL);
}

} // namespace science_and_theology
