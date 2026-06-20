#include "celestial_lod_system.hpp"

#include <cmath>
#include <algorithm>

namespace science_and_theology {

void CelestialLodSystem::set_ratios(const LodDistanceRatios& ratios) {
    std::lock_guard<std::mutex> lock(mutex_);
    ratios_ = ratios;
}

const LodDistanceRatios& CelestialLodSystem::ratios() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ratios_;
}

LodLevel CelestialLodSystem::choose_lod(const GlobalPos& player_pos,
                                        const CelestialBodyDesc& body) const {
    double dist = compute_center_distance(player_pos, body);
    double r = body.radius;

    LodDistanceRatios r_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        r_copy = ratios_;
    }

    if (dist <= r_copy.lod0_max * r) {
        return LodLevel::Real;
    }
    if (dist <= r_copy.lod1_max * r) {
        return LodLevel::Simplified;
    }
    if (dist <= r_copy.lod2_max * r) {
        return LodLevel::PlanetProxy;
    }
    if (dist <= r_copy.lod3_max * r) {
        return LodLevel::LowPoly;
    }
    return LodLevel::Billboard;
}

CelestialLodResult CelestialLodSystem::compute_lod_result(
    const GlobalPos& player_pos,
    const CelestialBodyDesc& body) const {
    CelestialLodResult result;
    result.celestial_id = body.id;
    result.lod = choose_lod(player_pos, body);
    result.distance_to_center = compute_center_distance(player_pos, body);
    result.distance_to_surface = compute_surface_distance(player_pos, body);
    result.planet_radius = body.radius;
    return result;
}

std::vector<CelestialLodResult> CelestialLodSystem::compute_all_lods(
    const GlobalPos& player_pos,
    const UniverseWorldCore& core) const {
    std::vector<CelestialLodResult> results;
    auto bodies = core.all_celestial_bodies();
    results.reserve(bodies.size());
    for (const auto* body : bodies) {
        if (body == nullptr) continue;
        results.push_back(compute_lod_result(player_pos, *body));
    }
    return results;
}

std::vector<double> CelestialLodSystem::compute_lod_distances(
    double planet_radius) const {
    LodDistanceRatios r_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        r_copy = ratios_;
    }
    return {
        r_copy.lod0_max * planet_radius,
        r_copy.lod1_max * planet_radius,
        r_copy.lod2_max * planet_radius,
        r_copy.lod3_max * planet_radius,
    };
}

double CelestialLodSystem::compute_surface_distance(
    const GlobalPos& player_pos,
    const CelestialBodyDesc& body) {
    double center_dist = compute_center_distance(player_pos, body);
    return center_dist - body.radius;
}

double CelestialLodSystem::compute_center_distance(
    const GlobalPos& player_pos,
    const CelestialBodyDesc& body) {
    double dx = player_pos.x - body.center.x;
    double dy = player_pos.y - body.center.y;
    double dz = player_pos.z - body.center.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool CelestialLodSystem::needs_real_voxels(LodLevel lod) {
    return lod == LodLevel::Real;
}

bool CelestialLodSystem::needs_visual(LodLevel lod) {
    return lod != LodLevel::COUNT;
}

} // namespace science_and_theology
