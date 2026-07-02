#include "interest_manager.hpp"

namespace science_and_theology {

InterestSet InterestManager::compute_interest(uint64_t player_handle,
                                              const SectorManager& sector_manager,
                                              const UniverseWorldCore& core) {
    InterestSet result;

    // 获取当前飞行模式
    FlightMode mode = flight_tracker_.get_flight_mode(player_handle);
    result.flight_mode = mode;
    result.needs_real_voxels = flight_tracker_.needs_real_voxels(player_handle);

    // 根据飞行模式设置高速预测
    bool high_speed = flight_tracker_.is_high_speed_mode(player_handle);
    chunk_streaming_.set_high_speed_prediction(player_handle, high_speed, 2.0);

    // 只有需要真实体素时才计算 chunk
    if (result.needs_real_voxels) {
        StreamingUpdateResult stream_result = chunk_streaming_.update_player(
            player_handle, sector_manager);
        result.chunks = std::move(stream_result.chunks_to_load);
        result.deferred_chunks = stream_result.deferred_count;
    }

    // 计算所有天体 LOD（始终计算，远处天体用 LOD 表示）
    result.celestial_lods = celestial_lod_.compute_all_lods(
        flight_tracker_.get_flight_state(player_handle).pos, core);

    return result;
}

void InterestManager::register_player(uint64_t player_handle,
                                      const GlobalPos& pos,
                                      const GlobalPos& velocity,
                                      SectorId current_sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    PlayerStreamingState stream_state;
    stream_state.pos = pos;
    stream_state.velocity = velocity;
    stream_state.current_sector = current_sector;
    chunk_streaming_.register_player(player_handle, stream_state);

    FlightState flight_state;
    flight_state.pos = pos;
    flight_state.velocity = velocity;
    flight_state.current_sector = current_sector;
    flight_tracker_.register_flyer(player_handle, flight_state);
}

void InterestManager::unregister_player(uint64_t player_handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    chunk_streaming_.unregister_player(player_handle);
    flight_tracker_.unregister_flyer(player_handle);
}

std::optional<FlightModeChangeEvent> InterestManager::update_player(
    uint64_t player_handle,
    const GlobalPos& pos,
    const GlobalPos& velocity) {

    std::lock_guard<std::mutex> lock(mutex_);

    // 更新 chunk streaming 位置
    chunk_streaming_.update_player_position(player_handle, pos, velocity);

    // 更新飞行状态并检测模式切换
    return flight_tracker_.update_flyer(player_handle, pos, velocity);
}

void InterestManager::set_landing_target(uint64_t player_handle,
                                         const std::string& celestial_id,
                                         double distance_to_surface) {
    std::lock_guard<std::mutex> lock(mutex_);
    flight_tracker_.set_landing_target(player_handle, celestial_id, distance_to_surface);
}

void InterestManager::clear_landing_target(uint64_t player_handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    flight_tracker_.clear_landing_target(player_handle);
}

void InterestManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunk_streaming_.clear();
    flight_tracker_.clear();
}

} // namespace science_and_theology
