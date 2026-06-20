#include "cross_sector_relay.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// CrossSectorRelay
// ============================================================

CrossSectorRelay::CrossSectorRelay(const std::string& id,
                                   RelayType type,
                                   SectorId from_sector,
                                   SectorId to_sector)
    : id_(id), from_sector_(from_sector), to_sector_(to_sector) {
    config_.type = type;

    // 根据类型设置默认配置
    switch (type) {
        case RelayType::PowerBridge:
            config_.capacity_per_tick = 1000;   // 1000 EU/t
            config_.loss_ratio = 0.05;          // 5% 损耗
            config_.max_queue_size = 16;
            break;
        case RelayType::FreightRelay:
            config_.capacity_per_tick = 64;     // 64 物品/t
            config_.loss_ratio = 0.0;           // 物品不损耗
            config_.max_queue_size = 32;
            break;
        case RelayType::FluidRelay:
            config_.capacity_per_tick = 4000;   // 4000 mB/t
            config_.loss_ratio = 0.02;          // 2% 损耗
            config_.max_queue_size = 8;
            break;
        case RelayType::Ae2QuantumLink:
            config_.capacity_per_tick = 10000;  // 10000 字节/t
            config_.loss_ratio = 0.0;           // 量子链路无损耗
            config_.max_queue_size = 64;
            break;
        default:
            break;
    }
}

void CrossSectorRelay::set_config(const RelayConfig& config) {
    config_ = config;
}

// --- 连接状态 ---

void CrossSectorRelay::connect() {
    connection_ = RelayConnectionState::Connected;
}

void CrossSectorRelay::disconnect() {
    connection_ = RelayConnectionState::Disconnected;
    if (config_.drop_on_disconnect) {
        stats_.total_dropped += static_cast<int64_t>(queue_.size());
        queue_.clear();
    }
}

void CrossSectorRelay::suspend() {
    connection_ = RelayConnectionState::Suspended;
}

// --- 载荷入队 ---

bool CrossSectorRelay::enqueue(const RelayPayload& payload) {
    // 未连接或暂停时不接受新载荷
    if (connection_ != RelayConnectionState::Connected) {
        return false;
    }

    // 队列已满
    if (config_.max_queue_size > 0 && queue_.size() >= config_.max_queue_size) {
        ++stats_.total_dropped;
        return false;
    }

    queue_.push_back(payload);
    return true;
}

// --- tick 传输 ---

std::vector<RelayPayload> CrossSectorRelay::tick_transfer() {
    std::vector<RelayPayload> transferred;

    if (connection_ != RelayConnectionState::Connected) {
        ++stats_.total_ticks;
        return transferred;
    }

    int64_t remaining_capacity = config_.capacity_per_tick;

    while (!queue_.empty() && remaining_capacity > 0) {
        RelayPayload& front = queue_.front();

        // 本 tick 可传输的量
        int64_t transfer_amount = std::min(front.amount, remaining_capacity);

        // 扣除损耗
        int64_t actual_transferred = compute_transferred(transfer_amount);
        int64_t lost = compute_lost(transfer_amount);

        // 创建传输载荷
        RelayPayload out = front;
        out.amount = actual_transferred;
        transferred.push_back(out);

        // 更新统计
        stats_.total_transferred += actual_transferred;
        stats_.total_lost += lost;

        // 减去已传输量
        front.amount -= transfer_amount;
        remaining_capacity -= transfer_amount;

        if (front.amount <= 0) {
            queue_.pop_front();
        }
    }

    ++stats_.total_ticks;
    return transferred;
}

// --- 队列查询 ---

size_t CrossSectorRelay::queue_size() const {
    return queue_.size();
}

bool CrossSectorRelay::queue_empty() const {
    return queue_.empty();
}

int64_t CrossSectorRelay::pending_amount() const {
    int64_t total = 0;
    for (const auto& p : queue_) {
        total += p.amount;
    }
    return total;
}

// --- 统计 ---

RelayStats CrossSectorRelay::stats() const {
    return stats_;
}

void CrossSectorRelay::reset_stats() {
    stats_ = RelayStats{};
}

// --- 管理 ---

void CrossSectorRelay::clear_queue() {
    queue_.clear();
}

// --- 内部工具 ---

int64_t CrossSectorRelay::compute_transferred(int64_t amount) const {
    if (amount <= 0) return 0;
    double loss = config_.loss_ratio;
    if (loss <= 0.0) return amount;
    if (loss >= 1.0) return 0;
    int64_t lost = static_cast<int64_t>(static_cast<double>(amount) * loss);
    return amount - lost;
}

int64_t CrossSectorRelay::compute_lost(int64_t amount) const {
    if (amount <= 0) return 0;
    double loss = config_.loss_ratio;
    if (loss <= 0.0) return 0;
    if (loss >= 1.0) return amount;
    return static_cast<int64_t>(static_cast<double>(amount) * loss);
}

// ============================================================
// CrossSectorRelayManager
// ============================================================

bool CrossSectorRelayManager::register_relay(const CrossSectorRelay& relay) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (relays_.count(relay.id()) > 0) {
        return false;
    }
    relays_.emplace(relay.id(), relay);
    return true;
}

bool CrossSectorRelayManager::unregister_relay(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return relays_.erase(id) > 0;
}

CrossSectorRelay* CrossSectorRelayManager::find_relay(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = relays_.find(id);
    if (it == relays_.end()) {
        return nullptr;
    }
    return &it->second;
}

const CrossSectorRelay* CrossSectorRelayManager::find_relay(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = relays_.find(id);
    if (it == relays_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<CrossSectorRelay*> CrossSectorRelayManager::find_relays_between(
    SectorId from_sector, SectorId to_sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CrossSectorRelay*> result;
    for (auto& [id, relay] : relays_) {
        if (relay.from_sector() == from_sector && relay.to_sector() == to_sector) {
            result.push_back(&relay);
        }
    }
    return result;
}

std::vector<CrossSectorRelay*> CrossSectorRelayManager::all_relays() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CrossSectorRelay*> result;
    result.reserve(relays_.size());
    for (auto& [id, relay] : relays_) {
        result.push_back(&relay);
    }
    return result;
}

size_t CrossSectorRelayManager::relay_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return relays_.size();
}

std::vector<RelayPayload> CrossSectorRelayManager::tick_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RelayPayload> all_transferred;
    for (auto& [id, relay] : relays_) {
        auto transferred = relay.tick_transfer();
        all_transferred.insert(all_transferred.end(),
                               transferred.begin(), transferred.end());
    }
    return all_transferred;
}

void CrossSectorRelayManager::disconnect_sector(SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, relay] : relays_) {
        if (relay.from_sector() == sector || relay.to_sector() == sector) {
            relay.disconnect();
        }
    }
}

void CrossSectorRelayManager::reconnect_sector(SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, relay] : relays_) {
        if (relay.from_sector() == sector || relay.to_sector() == sector) {
            relay.connect();
        }
    }
}

void CrossSectorRelayManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    relays_.clear();
}

} // namespace science_and_theology
