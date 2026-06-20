#include "chunk_streaming_system.hpp"

#include <cmath>
#include <algorithm>

namespace science_and_theology {

// ============================================================
// 配置
// ============================================================

void ChunkStreamingSystem::set_config(const StreamingConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

const StreamingConfig& ChunkStreamingSystem::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

// ============================================================
// 玩家状态
// ============================================================

void ChunkStreamingSystem::register_player(uint64_t player_id,
                                           const PlayerStreamingState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_[player_id] = state;
}

void ChunkStreamingSystem::unregister_player(uint64_t player_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.erase(player_id);
}

void ChunkStreamingSystem::update_player_position(uint64_t player_id,
                                                  const GlobalPos& pos,
                                                  const GlobalPos& velocity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }
    it->second.pos = pos;
    it->second.velocity = velocity;
}

void ChunkStreamingSystem::set_player_sector(uint64_t player_id, SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }
    it->second.current_sector = sector;
}

void ChunkStreamingSystem::set_high_speed_prediction(uint64_t player_id,
                                                     bool enabled,
                                                     double preload_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return;
    }
    it->second.high_speed_prediction = enabled;
    it->second.preload_seconds = preload_seconds;
}

// ============================================================
// 核心更新
// ============================================================

StreamingUpdateResult ChunkStreamingSystem::update_player(
    uint64_t player_id,
    const SectorManager& sector_manager) {
    StreamingUpdateResult result;

    PlayerStreamingState state;
    StreamingConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = players_.find(player_id);
        if (it == players_.end()) {
            return result;
        }
        state = it->second;
        cfg = config_;
    }

    // 高速预测：根据速度计算预测位置
    GlobalPos effective_pos = state.pos;
    if (state.high_speed_prediction && state.preload_seconds > 0.0) {
        effective_pos.x += state.velocity.x * state.preload_seconds;
        effective_pos.y += state.velocity.y * state.preload_seconds;
        effective_pos.z += state.velocity.z * state.preload_seconds;
    }

    // 选择 AOI 形状并计算目标 chunk 集合
    AoiShape shape = choose_aoi_shape(state);
    std::vector<SectorChunkKey> target_chunks;
    switch (shape) {
        case AoiShape::Sphere:
            target_chunks = compute_sphere_aoi(effective_pos, sector_manager);
            break;
        case AoiShape::ConeForward:
            target_chunks = compute_cone_aoi(effective_pos, state.velocity, sector_manager);
            break;
        case AoiShape::Cylinder:
            target_chunks = compute_cylinder_aoi(effective_pos, state.velocity, sector_manager);
            break;
    }

    // 收集涉及的 Sector
    std::unordered_set<SectorId, std::hash<SectorId>> touched;
    for (const auto& key : target_chunks) {
        touched.insert(key.sector);
    }
    result.touched_sectors.reserve(touched.size());
    for (const auto& s : touched) {
        result.touched_sectors.push_back(s);
    }

    // 计算需要加载的 chunk（target - loaded）
    std::unordered_set<SectorChunkKey> target_set(target_chunks.begin(),
                                                   target_chunks.end());
    for (const auto& key : target_chunks) {
        if (state.loaded_chunks.find(key) == state.loaded_chunks.end()) {
            result.chunks_to_load.push_back(key);
        }
    }

    // 计算需要卸载的 chunk（loaded - target，且超出卸载距离）
    // 卸载距离 = sphere_radius * unload_distance_multiplier
    const double unload_dist_chunks = static_cast<double>(cfg.sphere_radius_chunks)
                                    * cfg.unload_distance_multiplier;
    const int unload_dist_sq = static_cast<int>(unload_dist_chunks * unload_dist_chunks);

    ChunkCoord player_chunk = block_pos_to_chunk_coord(to_block_pos(effective_pos));
    for (const auto& key : state.loaded_chunks) {
        if (target_set.find(key) != target_set.end()) {
            continue;  // 仍在目标范围内
        }
        // 检查是否超出卸载距离
        int dist_sq = chunk_distance(key.coord, player_chunk);
        if (dist_sq > unload_dist_sq) {
            result.chunks_to_unload.push_back(key);
        }
    }

    // 按预算截断 chunks_to_load
    if (cfg.max_chunk_requests_per_frame > 0
        && static_cast<int>(result.chunks_to_load.size()) > cfg.max_chunk_requests_per_frame) {
        result.deferred_count = static_cast<int>(result.chunks_to_load.size())
                              - cfg.max_chunk_requests_per_frame;
        result.chunks_to_load.resize(cfg.max_chunk_requests_per_frame);
    }

    // 更新 loaded_chunks：移除卸载的，添加本次加载的
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = players_.find(player_id);
        if (it == players_.end()) {
            return result;
        }
        for (const auto& key : result.chunks_to_unload) {
            it->second.loaded_chunks.erase(key);
        }
        for (const auto& key : result.chunks_to_load) {
            it->second.loaded_chunks.insert(key);
        }
    }

    return result;
}

// ============================================================
// AOI 计算
// ============================================================

std::vector<SectorChunkKey> ChunkStreamingSystem::compute_sphere_aoi(
    const GlobalPos& pos,
    const SectorManager& sector_manager) const {

    std::vector<SectorChunkKey> result;
    GlobalBlockPos block_pos = to_block_pos(pos);
    ChunkCoord center = block_pos_to_chunk_coord(block_pos);

    int r = config_.sphere_radius_chunks;
    int r_sq = r * r;

    // 遍历球形范围内的所有 chunk 坐标
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dz = -r; dz <= r; ++dz) {
                int dist_sq = dx * dx + dy * dy + dz * dz;
                if (dist_sq > r_sq) {
                    continue;
                }
                ChunkCoord cc{center.cx + dx, center.cy + dy, center.cz + dz};

                // 计算该 chunk 的原点方块坐标
                GlobalBlockPos chunk_origin = chunk_coord_to_block_pos(cc);
                // chunk 中心
                GlobalBlockPos chunk_center{
                    chunk_origin.x + kUniverseChunkSize / 2,
                    chunk_origin.y + kUniverseChunkSize / 2,
                    chunk_origin.z + kUniverseChunkSize / 2};

                // 查询该坐标所属 Sector
                SectorQueryResult q = sector_manager.find_sector(chunk_center);
                if (!q.found()) {
                    continue;  // 不属于任何 Sector，跳过
                }

                result.push_back(SectorChunkKey{q.sector, cc});
            }
        }
    }

    return result;
}

std::vector<SectorChunkKey> ChunkStreamingSystem::compute_cone_aoi(
    const GlobalPos& pos,
    const GlobalPos& velocity,
    const SectorManager& sector_manager) const {

    std::vector<SectorChunkKey> result;

    // 计算速度方向单位向量
    double speed = std::sqrt(velocity.x * velocity.x
                           + velocity.y * velocity.y
                           + velocity.z * velocity.z);
    if (speed < 0.001) {
        // 速度太小，退化为球形
        return compute_sphere_aoi(pos, sector_manager);
    }

    double dir_x = velocity.x / speed;
    double dir_y = velocity.y / speed;
    double dir_z = velocity.z / speed;

    GlobalBlockPos block_pos = to_block_pos(pos);
    ChunkCoord center = block_pos_to_chunk_coord(block_pos);

    int forward = config_.cone_forward_chunks;
    int backward = config_.cone_backward_chunks;
    double cos_half_angle = std::cos(config_.cone_half_angle);

    // 遍历前向和后向范围内的 chunk
    int range = std::max(forward, backward);
    for (int dx = -range; dx <= range; ++dx) {
        for (int dy = -range; dy <= range; ++dy) {
            for (int dz = -range; dz <= range; ++dz) {
                // 计算与速度方向的角度
                double dot = dx * dir_x + dy * dir_y + dz * dir_z;
                double len = std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
                if (len < 0.001) {
                    // 中心点，始终包含
                } else {
                    double cos_angle = dot / len;
                    double proj = dot;  // 沿速度方向的投影（chunk 单位）

                    if (proj >= 0) {
                        // 前向：检查是否在锥角内且距离不超过 forward
                        if (proj > forward) continue;
                        if (cos_angle < cos_half_angle && len > 1.0) continue;
                    } else {
                        // 后向：检查距离不超过 backward
                        if (-proj > backward) continue;
                    }
                }

                ChunkCoord cc{center.cx + dx, center.cy + dy, center.cz + dz};
                GlobalBlockPos chunk_origin = chunk_coord_to_block_pos(cc);
                GlobalBlockPos chunk_center{
                    chunk_origin.x + kUniverseChunkSize / 2,
                    chunk_origin.y + kUniverseChunkSize / 2,
                    chunk_origin.z + kUniverseChunkSize / 2};

                SectorQueryResult q = sector_manager.find_sector(chunk_center);
                if (!q.found()) {
                    continue;
                }

                result.push_back(SectorChunkKey{q.sector, cc});
            }
        }
    }

    return result;
}

std::vector<SectorChunkKey> ChunkStreamingSystem::compute_cylinder_aoi(
    const GlobalPos& pos,
    const GlobalPos& velocity,
    const SectorManager& sector_manager) const {

    std::vector<SectorChunkKey> result;

    double speed = std::sqrt(velocity.x * velocity.x
                           + velocity.y * velocity.y
                           + velocity.z * velocity.z);
    if (speed < 0.001) {
        return compute_sphere_aoi(pos, sector_manager);
    }

    double dir_x = velocity.x / speed;
    double dir_y = velocity.y / speed;
    double dir_z = velocity.z / speed;

    GlobalBlockPos block_pos = to_block_pos(pos);
    ChunkCoord center = block_pos_to_chunk_coord(block_pos);

    int length = config_.cylinder_length_chunks;
    int radius = config_.cylinder_radius_chunks;
    int r_sq = radius * radius;

    // 沿速度方向前后各 length/2，半径 radius 的圆柱
    for (int along = -length / 2; along <= length / 2; ++along) {
        // 在垂直于速度方向的平面内遍历半径 radius 的圆
        // 简化：遍历立方体，过滤圆柱内
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    // 投影到速度方向
                    double proj = dx * dir_x + dy * dir_y + dz * dir_z;
                    // 垂直分量
                    double perp_x = dx - proj * dir_x;
                    double perp_y = dy - proj * dir_y;
                    double perp_z = dz - proj * dir_z;
                    double perp_sq = perp_x * perp_x + perp_y * perp_y + perp_z * perp_z;

                    if (perp_sq > r_sq) continue;
                    if (proj < -length / 2 || proj > length / 2) continue;

                    ChunkCoord cc{center.cx + dx, center.cy + dy, center.cz + dz};
                    GlobalBlockPos chunk_origin = chunk_coord_to_block_pos(cc);
                    GlobalBlockPos chunk_center{
                        chunk_origin.x + kUniverseChunkSize / 2,
                        chunk_origin.y + kUniverseChunkSize / 2,
                        chunk_origin.z + kUniverseChunkSize / 2};

                    SectorQueryResult q = sector_manager.find_sector(chunk_center);
                    if (!q.found()) {
                        continue;
                    }

                    result.push_back(SectorChunkKey{q.sector, cc});
                }
            }
        }
    }

    return result;
}

AoiShape ChunkStreamingSystem::choose_aoi_shape(const PlayerStreamingState& state) const {
    if (state.high_speed_prediction) {
        return AoiShape::ConeForward;
    }
    return config_.default_shape;
}

int ChunkStreamingSystem::chunk_distance(const ChunkCoord& a, const ChunkCoord& b) {
    int dx = static_cast<int>(a.cx - b.cx);
    int dy = static_cast<int>(a.cy - b.cy);
    int dz = static_cast<int>(a.cz - b.cz);
    return dx * dx + dy * dy + dz * dz;
}

// ============================================================
// 查询
// ============================================================

size_t ChunkStreamingSystem::get_loaded_chunk_count(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return 0;
    }
    return it->second.loaded_chunks.size();
}

SectorId ChunkStreamingSystem::get_player_sector(uint64_t player_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = players_.find(player_id);
    if (it == players_.end()) {
        return SectorId{0};
    }
    return it->second.current_sector;
}

// ============================================================
// 管理
// ============================================================

void ChunkStreamingSystem::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    players_.clear();
}

} // namespace science_and_theology
