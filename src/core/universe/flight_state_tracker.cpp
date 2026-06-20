#include "flight_state_tracker.hpp"

#include <cmath>

namespace science_and_theology {

void FlightStateTracker::set_config(const FlightModeConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

const FlightModeConfig& FlightStateTracker::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void FlightStateTracker::register_flyer(uint64_t flyer_id, const FlightState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    flyers_[flyer_id] = state;
}

void FlightStateTracker::unregister_flyer(uint64_t flyer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    flyers_.erase(flyer_id);
}

std::optional<FlightModeChangeEvent> FlightStateTracker::update_flyer(
    uint64_t flyer_id,
    const GlobalPos& pos,
    const GlobalPos& velocity) {

    std::optional<FlightModeChangeEvent> result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flyers_.find(flyer_id);
        if (it == flyers_.end()) {
            return result;
        }

        FlightState& state = it->second;
        state.pos = pos;
        state.velocity = velocity;

        FlightMode target = compute_target_mode(state);
        if (target != state.mode) {
            FlightModeChangeEvent event;
            event.old_mode = state.mode;
            event.new_mode = target;
            event.pos = pos;

            switch (target) {
                case FlightMode::Cruise:
                    event.reason = "speed exceeded cruise threshold";
                    break;
                case FlightMode::LandingApproach:
                    event.reason = "approaching landing target";
                    break;
                case FlightMode::LocalVoxelFlight:
                    event.reason = "close enough for local flight";
                    break;
                case FlightMode::Warp:
                    event.reason = "speed exceeded warp threshold";
                    break;
                default:
                    event.reason = "mode change";
                    break;
            }

            state.mode = target;
            result = event;
        }
    }

    return result;
}

std::optional<FlightModeChangeEvent> FlightStateTracker::set_flight_mode(
    uint64_t flyer_id,
    FlightMode mode,
    const std::string& reason) {

    std::optional<FlightModeChangeEvent> result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flyers_.find(flyer_id);
        if (it == flyers_.end()) {
            return result;
        }

        FlightState& state = it->second;
        if (state.mode != mode) {
            FlightModeChangeEvent event;
            event.old_mode = state.mode;
            event.new_mode = mode;
            event.pos = state.pos;
            event.reason = reason.empty() ? "manual mode change" : reason;
            state.mode = mode;
            result = event;
        }
    }

    return result;
}

void FlightStateTracker::set_landing_target(uint64_t flyer_id,
                                             const std::string& celestial_id,
                                             double distance_to_surface) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flyers_.find(flyer_id);
    if (it == flyers_.end()) {
        return;
    }
    it->second.target_celestial_id = celestial_id;
    it->second.distance_to_target_surface = distance_to_surface;
}

void FlightStateTracker::clear_landing_target(uint64_t flyer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flyers_.find(flyer_id);
    if (it == flyers_.end()) {
        return;
    }
    it->second.target_celestial_id.clear();
    it->second.distance_to_target_surface = 0.0;
}

FlightMode FlightStateTracker::get_flight_mode(uint64_t flyer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flyers_.find(flyer_id);
    if (it == flyers_.end()) {
        return FlightMode::LocalVoxelFlight;
    }
    return it->second.mode;
}

FlightState FlightStateTracker::get_flight_state(uint64_t flyer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flyers_.find(flyer_id);
    if (it == flyers_.end()) {
        return FlightState{};
    }
    return it->second;
}

SectorId FlightStateTracker::get_flyer_sector(uint64_t flyer_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flyers_.find(flyer_id);
    if (it == flyers_.end()) {
        return SectorId{0};
    }
    return it->second.current_sector;
}

bool FlightStateTracker::is_high_speed_mode(uint64_t flyer_id) const {
    FlightMode mode = get_flight_mode(flyer_id);
    return mode == FlightMode::Cruise || mode == FlightMode::Warp;
}

bool FlightStateTracker::needs_real_voxels(uint64_t flyer_id) const {
    FlightMode mode = get_flight_mode(flyer_id);
    return mode == FlightMode::LocalVoxelFlight || mode == FlightMode::LandingApproach;
}

void FlightStateTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    flyers_.clear();
}

FlightMode FlightStateTracker::compute_target_mode(const FlightState& state) const {
    double speed = std::sqrt(state.velocity.x * state.velocity.x
                           + state.velocity.y * state.velocity.y
                           + state.velocity.z * state.velocity.z);

    // 优先检查着陆接近
    if (!state.target_celestial_id.empty()) {
        if (state.distance_to_target_surface <= config_.local_flight_distance) {
            // 足够近，切换到局部飞行
            return FlightMode::LocalVoxelFlight;
        }
        if (state.distance_to_target_surface <= config_.landing_approach_distance) {
            // 接近目标，开始预加载
            return FlightMode::LandingApproach;
        }
    }

    // 检查速度
    if (speed >= config_.warp_speed_threshold) {
        return FlightMode::Warp;
    }
    if (speed >= config_.cruise_speed_threshold) {
        return FlightMode::Cruise;
    }

    // 低速默认局部飞行
    return FlightMode::LocalVoxelFlight;
}

} // namespace science_and_theology
