#include "celestial_lod_pipeline.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 玩家管理
// ============================================================

void CelestialLodPipeline::register_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[player_id];  // 创建空 map
}

void CelestialLodPipeline::unregister_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.erase(player_id);
}

// ============================================================
// 链路更新
// ============================================================

std::vector<LodPipelineTransition> CelestialLodPipeline::update(
    uint64_t player_id,
    const GlobalPos& player_pos,
    const std::vector<CelestialLodResult>& lod_results,
    FlightMode flight_mode,
    const std::string& landing_target_id) {

    std::vector<LodPipelineTransition> transitions;

    std::lock_guard<std::mutex> lock(mutex_);
    auto player_it = states_.find(player_id);
    if (player_it == states_.end()) {
        return transitions;
    }

    auto& celestial_states = player_it->second;

    // LandingApproach 模式下，目标天体至少需要 Approach 状态
    bool is_landing = (flight_mode == FlightMode::LandingApproach);

    for (const auto& lod : lod_results) {
        LodPipelineState lod_target = lod_to_pipeline_state(lod.lod);

        // 如果处于 LandingApproach 模式且这是目标天体，
        // 目标状态至少为 Approach（即使 LOD 还是 Proxy）
        LodPipelineState target = lod_target;
        if (is_landing && lod.celestial_id == landing_target_id) {
            if (static_cast<uint8_t>(target) < static_cast<uint8_t>(LodPipelineState::Approach)) {
                target = LodPipelineState::Approach;
            }
        }

        auto state_it = celestial_states.find(lod.celestial_id);
        if (state_it == celestial_states.end()) {
            // 首次记录，初始化为 Distant
            PlayerCelestialState ps;
            ps.state = LodPipelineState::Distant;
            ps.last_distance_to_surface = lod.distance_to_surface;
            celestial_states[lod.celestial_id] = ps;
            state_it = celestial_states.find(lod.celestial_id);
        }

        PlayerCelestialState& ps = state_it->second;
        LodPipelineState old_state = ps.state;

        // 计算单步转换
        LodPipelineState next = compute_next_step(old_state, target);

        if (next != old_state) {
            LodPipelineTransition t;
            t.player_id = player_id;
            t.celestial_id = lod.celestial_id;
            t.old_state = old_state;
            t.new_state = next;
            t.pos = player_pos;
            t.distance_to_surface = lod.distance_to_surface;

            if (static_cast<uint8_t>(next) > static_cast<uint8_t>(old_state)) {
                if (next == LodPipelineState::Approach) {
                    t.reason = "landing approach triggered";
                } else {
                    t.reason = "approaching celestial body";
                }
            } else {
                t.reason = "leaving celestial body";
            }

            transitions.push_back(t);
            ps.state = next;
        }

        ps.last_distance_to_surface = lod.distance_to_surface;
    }

    return transitions;
}

// ============================================================
// 查询
// ============================================================

LodPipelineState CelestialLodPipeline::get_state(
    uint64_t player_id,
    const std::string& celestial_id) const {

    std::lock_guard<std::mutex> lock(mutex_);
    auto player_it = states_.find(player_id);
    if (player_it == states_.end()) {
        return LodPipelineState::Distant;
    }
    auto state_it = player_it->second.find(celestial_id);
    if (state_it == player_it->second.end()) {
        return LodPipelineState::Distant;
    }
    return state_it->second.state;
}

std::vector<std::pair<std::string, LodPipelineState>>
CelestialLodPipeline::get_all_states(uint64_t player_id) const {
    std::vector<std::pair<std::string, LodPipelineState>> result;

    std::lock_guard<std::mutex> lock(mutex_);
    auto player_it = states_.find(player_id);
    if (player_it == states_.end()) {
        return result;
    }

    for (const auto& [id, ps] : player_it->second) {
        result.emplace_back(id, ps.state);
    }
    return result;
}

bool CelestialLodPipeline::needs_real_chunks(LodPipelineState state) {
    return state == LodPipelineState::Approach
        || state == LodPipelineState::Simplified
        || state == LodPipelineState::Real;
}

bool CelestialLodPipeline::is_preloading_or_higher(LodPipelineState state) {
    return static_cast<uint8_t>(state) >= static_cast<uint8_t>(LodPipelineState::Approach);
}

// ============================================================
// 管理
// ============================================================

void CelestialLodPipeline::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();
}

// ============================================================
// 内部
// ============================================================

LodPipelineState CelestialLodPipeline::lod_to_pipeline_state(LodLevel lod) {
    switch (lod) {
        case LodLevel::Real:
            return LodPipelineState::Real;
        case LodLevel::Simplified:
            return LodPipelineState::Simplified;
        case LodLevel::PlanetProxy:
            return LodPipelineState::Proxy;
        case LodLevel::LowPoly:
        case LodLevel::Billboard:
        default:
            return LodPipelineState::Distant;
    }
}

LodPipelineState CelestialLodPipeline::compute_next_step(LodPipelineState current,
                                                          LodPipelineState target) {
    if (current == target) {
        return current;
    }

    uint8_t cur = static_cast<uint8_t>(current);
    uint8_t tgt = static_cast<uint8_t>(target);

    // 单步前进或后退，避免跳级
    if (tgt > cur) {
        return static_cast<LodPipelineState>(cur + 1);
    } else {
        return static_cast<LodPipelineState>(cur - 1);
    }
}

} // namespace science_and_theology
