#pragma once

// ============================================================
// network_isolation_guard.hpp — U6 网络隔离守卫
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U6) 节。
//
// U6 工作项：网络图 dirty 与重建不得递归加载另一 Sector 的普通节点。
//
// 验收条件：一个 Sector 内的网络变化不会遍历或强制加载另一 Sector。
//
// NetworkIsolationGuard 的职责：
//   1. 标记网络节点的所属 Sector。
//   2. 在网络图遍历/重建时，提供边界检查：遇到跨 Sector 边界时停止递归。
//   3. 记录被阻止的跨 Sector 访问尝试（用于诊断）。
//   4. 允许显式注册"跨 Sector 边界节点"（即中继端点），这些节点
//      是唯一允许被跨 Sector 引用的节点。
//
// 模型：
//   - 每个网络节点（机器、管道端点等）属于一个 Sector。
//   - 网络图遍历时，从某节点出发，只能遍历同 Sector 的节点。
//   - 跨 Sector 的连接只能通过注册的"边界节点"（中继端点）。
//   - 边界节点本身属于本 Sector，但其对端属于另一 Sector。
//   - 遍历到边界节点时，不递归到对端 Sector，而是记录"有待通过中继传输"。

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 网络节点标识（用 GlobalBlockPos 标识，因为统一宇宙中方块位置全局唯一）。
using NetworkNodeId = GlobalBlockPos;

// 网络类型（与现有网络系统对应）。
enum class NetworkType : uint8_t {
    Power        = 0,  // 电网
    Fluid        = 1,  // 流体网络
    ItemPipe     = 2,  // 物品管道
    Ae2Me        = 3,  // AE2 ME 网络
    COUNT        = 4,
};

// 跨 Sector 访问被阻止的记录（用于诊断）。
struct BlockedCrossSectorAccess {
    NetworkNodeId source_node;       // 发起遍历的节点
    SectorId source_sector;          // 发起遍历的 Sector
    NetworkNodeId target_node;       // 试图访问的目标节点
    SectorId target_sector;          // 目标 Sector
    NetworkType network_type;        // 网络类型
    int64_t tick_recorded;           // 记录时的 tick
};

// NetworkIsolationGuard — 网络隔离守卫。
//
// 使用方式：
//   1. 节点注册时调用 register_node(node, sector, network_type)。
//   2. 中继端点注册时调用 register_boundary_node(node, sector, network_type, peer_sector)。
//   3. 网络图遍历时，调用 can_traverse(from, to, network_type) 检查是否允许。
//   4. 遇到跨 Sector 边界时，调用 record_blocked_access 记录。
class NetworkIsolationGuard {
public:
    NetworkIsolationGuard() = default;
    ~NetworkIsolationGuard() = default;

    NetworkIsolationGuard(const NetworkIsolationGuard&) = delete;
    NetworkIsolationGuard& operator=(const NetworkIsolationGuard&) = delete;

    // --- 节点注册 ---

    // 注册普通网络节点。
    // 普通节点只能被同 Sector 的遍历访问。
    void register_node(const NetworkNodeId& node,
                       SectorId sector,
                       NetworkType network_type);

    // 注册边界节点（中继端点）。
    // 边界节点属于本 Sector，但其对端属于 peer_sector。
    // 边界节点可以被跨 Sector 引用，但遍历到边界节点时不应递归到对端。
    void register_boundary_node(const NetworkNodeId& node,
                                 SectorId sector,
                                 NetworkType network_type,
                                 SectorId peer_sector);

    // 注销节点。
    void unregister_node(const NetworkNodeId& node);

    // --- 遍历检查 ---

    // 检查是否允许从 from 节点遍历到 to 节点。
    // 规则：
    //   1. 若两节点属于同一 Sector，允许。
    //   2. 若两节点属于不同 Sector，拒绝（返回 false）。
    //   3. 若 to 节点未注册，拒绝。
    bool can_traverse(const NetworkNodeId& from,
                      const NetworkNodeId& to,
                      NetworkType network_type) const;

    // 记录一次被阻止的跨 Sector 访问（用于诊断）。
    void record_blocked_access(const NetworkNodeId& from,
                                const NetworkNodeId& to,
                                NetworkType network_type,
                                int64_t tick);

    // --- 查询 ---

    // 查询节点所属 Sector。
    // 返回 SectorId{0} 表示未注册。
    SectorId get_node_sector(const NetworkNodeId& node) const;

    // 查询节点是否为边界节点。
    bool is_boundary_node(const NetworkNodeId& node) const;

    // 查询边界节点的对端 Sector。
    SectorId get_boundary_peer_sector(const NetworkNodeId& node) const;

    // 返回指定 Sector 内的所有节点。
    std::vector<NetworkNodeId> nodes_in_sector(SectorId sector) const;

    // 返回指定 Sector 内的边界节点。
    std::vector<NetworkNodeId> boundary_nodes_in_sector(SectorId sector) const;

    // 返回被阻止的跨 Sector 访问记录（用于诊断）。
    std::vector<BlockedCrossSectorAccess> blocked_accesses() const;
    size_t blocked_access_count() const;

    // --- 管理 ---

    // 清除指定 Sector 的所有节点（用于 Sector 卸载）。
    void clear_sector(SectorId sector);

    void clear();

    // 节点总数。
    size_t node_count() const;

    // 边界节点总数。
    size_t boundary_node_count() const;

private:
    struct NodeEntry {
        SectorId sector;
        NetworkType network_type;
        bool is_boundary = false;
        SectorId peer_sector{0};  // 仅边界节点有效
    };

    mutable std::mutex mutex_;

    // 节点表。
    std::unordered_map<NetworkNodeId, NodeEntry> nodes_;

    // Sector → 节点列表的反向索引。
    std::unordered_map<SectorId, std::vector<NetworkNodeId>> sector_nodes_;

    // 被阻止的跨 Sector 访问记录。
    std::vector<BlockedCrossSectorAccess> blocked_accesses_;

    // 阻止记录上限（避免无限增长）。
    static constexpr size_t kMaxBlockedAccessRecords = 1024;
};

} // namespace science_and_theology
