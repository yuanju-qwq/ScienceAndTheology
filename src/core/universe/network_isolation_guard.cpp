#include "network_isolation_guard.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 节点注册
// ============================================================

void NetworkIsolationGuard::register_node(const NetworkNodeId& node,
                                          SectorId sector,
                                          NetworkType network_type) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 已存在则更新
    auto& entry = nodes_[node];
    entry.sector = sector;
    entry.network_type = network_type;
    entry.is_boundary = false;
    entry.peer_sector = SectorId{0};

    sector_nodes_[sector].push_back(node);
}

void NetworkIsolationGuard::register_boundary_node(const NetworkNodeId& node,
                                                    SectorId sector,
                                                    NetworkType network_type,
                                                    SectorId peer_sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& entry = nodes_[node];
    entry.sector = sector;
    entry.network_type = network_type;
    entry.is_boundary = true;
    entry.peer_sector = peer_sector;

    sector_nodes_[sector].push_back(node);
}

void NetworkIsolationGuard::unregister_node(const NetworkNodeId& node) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node);
    if (it == nodes_.end()) {
        return;
    }

    SectorId sector = it->second.sector;
    nodes_.erase(it);

    // 从反向索引中移除
    auto sit = sector_nodes_.find(sector);
    if (sit != sector_nodes_.end()) {
        auto& vec = sit->second;
        vec.erase(std::remove(vec.begin(), vec.end(), node), vec.end());
        if (vec.empty()) {
            sector_nodes_.erase(sit);
        }
    }
}

// ============================================================
// 遍历检查
// ============================================================

bool NetworkIsolationGuard::can_traverse(const NetworkNodeId& from,
                                          const NetworkNodeId& to,
                                          NetworkType network_type) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto from_it = nodes_.find(from);
    auto to_it = nodes_.find(to);

    // 目标节点未注册，拒绝
    if (to_it == nodes_.end()) {
        return false;
    }

    // 源节点未注册，拒绝
    if (from_it == nodes_.end()) {
        return false;
    }

    // 网络类型不匹配，拒绝
    if (from_it->second.network_type != network_type ||
        to_it->second.network_type != network_type) {
        return false;
    }

    // 同 Sector 允许遍历
    if (from_it->second.sector == to_it->second.sector) {
        return true;
    }

    // 跨 Sector 拒绝（即使目标是边界节点，也不允许直接遍历）
    return false;
}

void NetworkIsolationGuard::record_blocked_access(const NetworkNodeId& from,
                                                    const NetworkNodeId& to,
                                                    NetworkType network_type,
                                                    int64_t tick) {
    // 先在锁外查询 Sector（避免递归加锁导致死锁）
    SectorId from_sector = get_node_sector(from);
    SectorId to_sector = get_node_sector(to);

    std::lock_guard<std::mutex> lock(mutex_);

    // 超过上限时丢弃最旧的记录
    if (blocked_accesses_.size() >= kMaxBlockedAccessRecords) {
        blocked_accesses_.erase(blocked_accesses_.begin());
    }

    BlockedCrossSectorAccess record;
    record.source_node = from;
    record.source_sector = from_sector;
    record.target_node = to;
    record.target_sector = to_sector;
    record.network_type = network_type;
    record.tick_recorded = tick;
    blocked_accesses_.push_back(record);
}

// ============================================================
// 查询
// ============================================================

SectorId NetworkIsolationGuard::get_node_sector(const NetworkNodeId& node) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node);
    if (it == nodes_.end()) {
        return SectorId{0};
    }
    return it->second.sector;
}

bool NetworkIsolationGuard::is_boundary_node(const NetworkNodeId& node) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node);
    if (it == nodes_.end()) {
        return false;
    }
    return it->second.is_boundary;
}

SectorId NetworkIsolationGuard::get_boundary_peer_sector(const NetworkNodeId& node) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node);
    if (it == nodes_.end() || !it->second.is_boundary) {
        return SectorId{0};
    }
    return it->second.peer_sector;
}

std::vector<NetworkNodeId> NetworkIsolationGuard::nodes_in_sector(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sector_nodes_.find(sector);
    if (it == sector_nodes_.end()) {
        return {};
    }
    return it->second;
}

std::vector<NetworkNodeId> NetworkIsolationGuard::boundary_nodes_in_sector(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NetworkNodeId> result;
    auto it = sector_nodes_.find(sector);
    if (it == sector_nodes_.end()) {
        return result;
    }
    for (const auto& node : it->second) {
        auto nit = nodes_.find(node);
        if (nit != nodes_.end() && nit->second.is_boundary) {
            result.push_back(node);
        }
    }
    return result;
}

std::vector<BlockedCrossSectorAccess> NetworkIsolationGuard::blocked_accesses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocked_accesses_;
}

size_t NetworkIsolationGuard::blocked_access_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return blocked_accesses_.size();
}

// ============================================================
// 管理
// ============================================================

void NetworkIsolationGuard::clear_sector(SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto sit = sector_nodes_.find(sector);
    if (sit == sector_nodes_.end()) {
        return;
    }

    // 移除该 Sector 的所有节点
    for (const auto& node : sit->second) {
        nodes_.erase(node);
    }
    sector_nodes_.erase(sit);
}

void NetworkIsolationGuard::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
    sector_nodes_.clear();
    blocked_accesses_.clear();
}

size_t NetworkIsolationGuard::node_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

size_t NetworkIsolationGuard::boundary_node_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [node, entry] : nodes_) {
        if (entry.is_boundary) {
            ++count;
        }
    }
    return count;
}

} // namespace science_and_theology
