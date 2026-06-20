#include "gravity_field_manager.hpp"

#include <cmath>
#include <algorithm>

namespace science_and_theology {

// ============================================================
// 配置
// ============================================================

void GravityFieldManager::set_hysteresis_ratio(double ratio) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 限制在 (0, 1] 范围内
    hysteresis_ratio_ = std::max(0.01, std::min(1.0, ratio));
}

double GravityFieldManager::hysteresis_ratio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hysteresis_ratio_;
}

// ============================================================
// 重力场管理
// ============================================================

void GravityFieldManager::register_field(const GravityField& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    fields_[field.celestial_id] = field;
}

void GravityFieldManager::unregister_field(const std::string& celestial_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    fields_.erase(celestial_id);
    // 清除引用此天体的玩家主导记录
    for (auto it = player_dominant_.begin(); it != player_dominant_.end(); ) {
        if (it->second == celestial_id) {
            it = player_dominant_.erase(it);
        } else {
            ++it;
        }
    }
}

void GravityFieldManager::register_from_environment(
    const std::string& celestial_id,
    const GlobalPos& center,
    double radius,
    const PlanetEnvironment& env) {

    GravityField field;
    field.celestial_id = celestial_id;
    field.center = center;
    field.radius = radius;
    field.surface_gravity = env.surface_gravity;
    field.influence_radius = env.gravity_influence_radius;
    field.falloff = env.gravity_falloff;
    register_field(field);
}

void GravityFieldManager::clear_fields() {
    std::lock_guard<std::mutex> lock(mutex_);
    fields_.clear();
}

// ============================================================
// 查询
// ============================================================

double GravityFieldManager::compute_field_magnitude(const GravityField& field,
                                                     const GlobalPos& pos) {
    if (!is_in_field(field, pos)) {
        return 0.0;
    }

    // 计算到天体中心的距离
    double dx = pos.x - field.center.x;
    double dy = pos.y - field.center.y;
    double dz = pos.z - field.center.z;
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // 距离表面（径向距离 - 半径）
    double surface_dist = dist - field.radius;

    switch (field.falloff) {
        case GravityFalloff::Constant:
            // 恒定重力：影响范围内重力大小不变
            return field.surface_gravity;

        case GravityFalloff::Linear: {
            // 线性衰减：表面为 surface_gravity，影响半径处为 0
            if (field.influence_radius <= field.radius) {
                return field.surface_gravity;
            }
            double max_height = field.influence_radius - field.radius;
            if (max_height <= 0.0) {
                return field.surface_gravity;
            }
            double t = std::max(0.0, surface_dist) / max_height;
            t = std::min(1.0, t);
            return field.surface_gravity * (1.0 - t);
        }

        case GravityFalloff::InverseSquare: {
            // 平方反比：g = g0 * (r / d)^2
            if (dist < 1.0) {
                return field.surface_gravity;
            }
            double ratio = field.radius / dist;
            return field.surface_gravity * ratio * ratio;
        }

        default:
            return 0.0;
    }
}

GlobalPos GravityFieldManager::compute_field_direction(const GravityField& field,
                                                        const GlobalPos& pos) {
    double dx = field.center.x - pos.x;
    double dy = field.center.y - pos.y;
    double dz = field.center.z - pos.z;
    double len = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (len < 0.001) {
        // 玩家在天体中心，返回默认向下
        return GlobalPos{0.0, -1.0, 0.0};
    }

    return GlobalPos{dx / len, dy / len, dz / len};
}

bool GravityFieldManager::is_in_field(const GravityField& field, const GlobalPos& pos) {
    double dx = pos.x - field.center.x;
    double dy = pos.y - field.center.y;
    double dz = pos.z - field.center.z;
    double dist_sq = dx * dx + dy * dy + dz * dz;
    double influence = field.influence_radius > 0.0 ? field.influence_radius : field.radius;
    return dist_sq <= influence * influence;
}

GravityFieldManager::BestFieldResult
GravityFieldManager::compute_best_field(const GlobalPos& pos) const {
    BestFieldResult best;
    for (const auto& [id, field] : fields_) {
        if (!is_in_field(field, pos)) {
            continue;
        }
        double mag = compute_field_magnitude(field, pos);
        if (mag > best.magnitude) {
            best.magnitude = mag;
            best.celestial_id = id;
            best.field = &field;
        }
    }
    return best;
}

GravityQueryResult GravityFieldManager::compute_gravity(uint64_t player_id,
                                                         const GlobalPos& pos) const {
    std::lock_guard<std::mutex> lock(mutex_);

    GravityQueryResult result;

    // 计算最优场（不考虑滞后）
    BestFieldResult best = compute_best_field(pos);

    if (best.field == nullptr) {
        // 不在任何重力场影响范围内
        // 清除玩家主导记录
        player_dominant_.erase(player_id);
        return result;
    }

    // 检查滞后机制
    auto it = player_dominant_.find(player_id);
    if (it != player_dominant_.end() && it->second != best.celestial_id) {
        // 玩家有当前主导场，且与最优场不同
        // 检查当前主导场是否仍在影响范围内
        auto current_it = fields_.find(it->second);
        if (current_it != fields_.end()) {
            const GravityField& current = current_it->second;
            if (is_in_field(current, pos)) {
                double current_mag = compute_field_magnitude(current, pos);
                // 滞后：当前主导场引力需降到最优场的 hysteresis_ratio 倍以下才切换
                if (current_mag >= best.magnitude * hysteresis_ratio_) {
                    // 保持当前主导场
                    result.has_gravity = true;
                    result.dominant_celestial_id = current.celestial_id;
                    result.direction = compute_field_direction(current, pos);
                    result.magnitude = current_mag;

                    double dx = pos.x - current.center.x;
                    double dy = pos.y - current.center.y;
                    double dz = pos.z - current.center.z;
                    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    result.distance_to_surface = dist - current.radius;
                    return result;
                }
            }
        }
        // 当前主导场不再有效，切换到最优场
    }

    // 使用最优场
    player_dominant_[player_id] = best.celestial_id;
    result.has_gravity = true;
    result.dominant_celestial_id = best.celestial_id;
    result.direction = compute_field_direction(*best.field, pos);
    result.magnitude = best.magnitude;

    double dx = pos.x - best.field->center.x;
    double dy = pos.y - best.field->center.y;
    double dz = pos.z - best.field->center.z;
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    result.distance_to_surface = dist - best.field->radius;

    return result;
}

// ============================================================
// 管理
// ============================================================

void GravityFieldManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    fields_.clear();
    player_dominant_.clear();
}

size_t GravityFieldManager::field_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fields_.size();
}

} // namespace science_and_theology
