#include "planet_build_frame.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace science_and_theology {
namespace {

constexpr double kDirectionEpsilon = 1.0e-12;

BuildVector direction_vector(Direction direction) {
    const GlobalBlockPos offset = direction_offset(direction);
    return BuildVector{
        static_cast<double>(offset.x),
        static_cast<double>(offset.y),
        static_cast<double>(offset.z)};
}

double dot(const BuildVector& a, const BuildVector& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length_squared(const BuildVector& value) {
    return dot(value, value);
}

BuildVector normalized(const BuildVector& value) {
    const double length_sq = length_squared(value);
    if (length_sq <= kDirectionEpsilon) {
        return BuildVector{};
    }
    const double inverse_length = 1.0 / std::sqrt(length_sq);
    return BuildVector{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length};
}

const std::array<Direction, 6> kAxisDirections{
    Direction::PosX, Direction::NegX,
    Direction::PosY, Direction::NegY,
    Direction::PosZ, Direction::NegZ,
};

} // namespace

PlanetBuildFrame::PlanetBuildFrame(
        double center_x, double center_y, double center_z)
    : center_{center_x, center_y, center_z} {}

BuildVector PlanetBuildFrame::radial_up(
        const PlanetLocalBlockPos& anchor) const {
    const BuildVector from_center{
        static_cast<double>(anchor.x) + 0.5 - center_.x,
        static_cast<double>(anchor.y) + 0.5 - center_.y,
        static_cast<double>(anchor.z) + 0.5 - center_.z};
    const BuildVector result = normalized(from_center);
    if (length_squared(result) <= kDirectionEpsilon) {
        return direction_vector(Direction::PosY);
    }
    return result;
}

Direction PlanetBuildFrame::local_up(
        const PlanetLocalBlockPos& anchor) const {
    return snap_global_axis(radial_up(anchor));
}

Direction PlanetBuildFrame::local_down(
        const PlanetLocalBlockPos& anchor) const {
    return opposite_direction(local_up(anchor));
}

Direction PlanetBuildFrame::local_horizontal(
        const PlanetLocalBlockPos& anchor,
        const BuildVector& requested) const {
    const BuildVector up = radial_up(anchor);
    const double vertical = dot(requested, up);
    BuildVector tangent{
        requested.x - up.x * vertical,
        requested.y - up.y * vertical,
        requested.z - up.z * vertical};
    tangent = normalized(tangent);

    const Direction up_axis = local_up(anchor);
    const Direction down_axis = opposite_direction(up_axis);
    if (length_squared(tangent) <= kDirectionEpsilon) {
        for (Direction candidate : kAxisDirections) {
            if (candidate != up_axis && candidate != down_axis) {
                return candidate;
            }
        }
        return Direction::PosX;
    }

    Direction best = Direction::COUNT;
    double best_dot = -std::numeric_limits<double>::infinity();
    for (Direction candidate : kAxisDirections) {
        if (candidate == up_axis || candidate == down_axis) {
            continue;
        }
        const double candidate_dot = dot(tangent, direction_vector(candidate));
        if (candidate_dot > best_dot) {
            best_dot = candidate_dot;
            best = candidate;
        }
    }
    return best;
}

LocalBuildDirection PlanetBuildFrame::classify(
        const PlanetLocalBlockPos& anchor,
        Direction direction) const {
    const Direction up = local_up(anchor);
    if (direction == up) {
        return LocalBuildDirection::Up;
    }
    if (direction == opposite_direction(up)) {
        return LocalBuildDirection::Down;
    }
    return LocalBuildDirection::Horizontal;
}

Direction PlanetBuildFrame::snap_global_axis(
        const BuildVector& requested,
        Direction fallback) {
    if (length_squared(requested) <= kDirectionEpsilon) {
        return is_axis_direction(fallback) ? fallback : Direction::PosY;
    }

    Direction best = fallback;
    double best_dot = -std::numeric_limits<double>::infinity();
    for (Direction candidate : kAxisDirections) {
        const double candidate_dot = dot(requested, direction_vector(candidate));
        if (candidate_dot > best_dot) {
            best_dot = candidate_dot;
            best = candidate;
        }
    }
    return best;
}

bool PlanetBuildFrame::is_axis_direction(Direction direction) {
    return static_cast<uint8_t>(direction) <
        static_cast<uint8_t>(Direction::COUNT);
}

} // namespace science_and_theology
