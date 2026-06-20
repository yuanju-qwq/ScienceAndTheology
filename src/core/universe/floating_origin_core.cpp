#include "floating_origin_core.hpp"

#include <cmath>

namespace science_and_theology {

void FloatingOriginCore::set_config(const FloatingOriginConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

const FloatingOriginConfig& FloatingOriginCore::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void FloatingOriginCore::set_origin(const GlobalPos& origin) {
    std::lock_guard<std::mutex> lock(mutex_);
    origin_ = origin;
}

GlobalPos FloatingOriginCore::origin() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return origin_;
}

std::optional<RebasingEvent> FloatingOriginCore::set_player_position(const GlobalPos& pos) {
    std::lock_guard<std::mutex> lock(mutex_);

    GlobalPos old_origin = origin_;
    player_pos_ = pos;

    // 计算玩家渲染坐标
    GlobalPos render_pos{
        pos.x - origin_.x,
        pos.y - origin_.y,
        pos.z - origin_.z};

    double dist = std::sqrt(render_pos.x * render_pos.x
                          + render_pos.y * render_pos.y
                          + render_pos.z * render_pos.z);

    if (dist > config_.rebase_threshold) {
        // 触发重基准：将渲染原点设置为当前玩家全局坐标
        origin_ = pos;
        RebasingEvent event;
        event.old_origin = old_origin;
        event.new_origin = origin_;
        event.player_pos = pos;
        return event;
    }

    return std::nullopt;
}

GlobalPos FloatingOriginCore::player_position() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return player_pos_;
}

GlobalPos FloatingOriginCore::universe_to_render(const GlobalPos& universe_pos) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return GlobalPos{
        universe_pos.x - origin_.x,
        universe_pos.y - origin_.y,
        universe_pos.z - origin_.z};
}

GlobalBlockPos FloatingOriginCore::universe_block_to_render(
    const GlobalBlockPos& block_pos) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // 渲染原点取整后的方块坐标
    int64_t ox = static_cast<int64_t>(std::floor(origin_.x));
    int64_t oy = static_cast<int64_t>(std::floor(origin_.y));
    int64_t oz = static_cast<int64_t>(std::floor(origin_.z));
    return GlobalBlockPos{
        block_pos.x - ox,
        block_pos.y - oy,
        block_pos.z - oz};
}

GlobalPos FloatingOriginCore::render_to_universe(const GlobalPos& render_pos) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return GlobalPos{
        render_pos.x + origin_.x,
        render_pos.y + origin_.y,
        render_pos.z + origin_.z};
}

GlobalPos FloatingOriginCore::player_render_position() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return GlobalPos{
        player_pos_.x - origin_.x,
        player_pos_.y - origin_.y,
        player_pos_.z - origin_.z};
}

double FloatingOriginCore::player_render_distance() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double dx = player_pos_.x - origin_.x;
    double dy = player_pos_.y - origin_.y;
    double dz = player_pos_.z - origin_.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void FloatingOriginCore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    origin_ = GlobalPos{0.0, 0.0, 0.0};
    player_pos_ = GlobalPos{0.0, 0.0, 0.0};
}

} // namespace science_and_theology
