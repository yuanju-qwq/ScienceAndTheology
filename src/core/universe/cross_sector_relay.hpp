#pragma once

// ============================================================
// cross_sector_relay.hpp — U6 跨 Sector 工业中继
// ============================================================
//
// 设计依据：docs/unified_universe_world_design.md 第 21.2 (U6) 节。
//
// U6 工作项：为跨 Sector 传输增加显式 PowerBridge、货运节点、流体中继
// 和 AE2 量子链路；中继具备容量、损耗、队列和断线状态。
//
// 验收条件：跨 Sector 资源只能经过中继，并受吞吐与损耗约束。
//
// 中继模型：
//   - 每个中继连接两个 Sector（from_sector → to_sector）。
//   - 中继有容量上限（每 tick 最大吞吐量）。
//   - 中继有损耗率（0.0~1.0，传输时按比例损失）。
//   - 中继有队列（超容量时排队，队列满则丢弃或拒绝）。
//   - 中继有连接状态（Connected/Disconnected）。
//   - 断线时队列保留，重连后继续传输。
//
// 中继类型：
//   - PowerBridge：电力中继（单位：EU/t 或类似）
//   - FreightRelay：货运中继（物品栈）
//   - FluidRelay：流体中继（mB/t）
//   - Ae2QuantumLink：AE2 量子链路（网络通道，无损耗但需量子配对）

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <variant>
#include <optional>
#include <mutex>

#include "universe_types.hpp"

namespace science_and_theology {

// 中继类型。
enum class RelayType : uint8_t {
    PowerBridge     = 0,  // 电力中继
    FreightRelay    = 1,  // 货运中继
    FluidRelay      = 2,  // 流体中继
    Ae2QuantumLink  = 3,  // AE2 量子链路
    COUNT           = 4,
};

constexpr const char* kRelayTypeNames[] = {
    "PowerBridge", "FreightRelay", "FluidRelay", "Ae2QuantumLink",
};

inline const char* relay_type_name(RelayType t) {
    uint8_t i = static_cast<uint8_t>(t);
    if (i >= static_cast<uint8_t>(RelayType::COUNT)) return "Unknown";
    return kRelayTypeNames[i];
}

// 中继连接状态。
enum class RelayConnectionState : uint8_t {
    Disconnected = 0,  // 断线（不传输，队列保留）
    Connected    = 1,  // 连接中（正常传输）
    Suspended    = 2,  // 暂停（手动暂停，不传输）
    COUNT        = 3,
};

// 传输载荷（变体）。
// 不同中继类型携带不同载荷：
//   - PowerBridge: power_amount（EU）
//   - FreightRelay: item_id + item_count
//   - FluidRelay: fluid_id + fluid_amount（mB）
//   - Ae2QuantumLink: channel_id + data_amount（字节）
struct RelayPayload {
    // 通用字段
    std::string resource_id;  // 资源标识（item_id / fluid_id / channel_id）
    int64_t amount = 0;       // 数量（EU / 个 / mB / 字节）

    // 可选：源 block 位置（用于追溯）
    GlobalBlockPos source_block;

    // 可选：目标 block 位置
    GlobalBlockPos target_block;
};

// 中继配置。
struct RelayConfig {
    // 中继类型。
    RelayType type = RelayType::PowerBridge;

    // 容量上限（每 tick 最大传输量）。
    int64_t capacity_per_tick = 1000;

    // 损耗率（0.0~1.0）。
    // 0.0 = 无损耗，0.1 = 损耗 10%。
    // AE2 量子链路通常为 0.0。
    double loss_ratio = 0.0;

    // 队列上限（排队中的最大载荷数）。
    // 0 = 不排队（超容量直接拒绝）。
    size_t max_queue_size = 16;

    // 是否在断线时丢弃队列。
    bool drop_on_disconnect = true;
};

// 中继统计。
struct RelayStats {
    int64_t total_transferred = 0;  // 累计传输量
    int64_t total_lost = 0;         // 累计损耗量
    int64_t total_dropped = 0;      // 累计丢弃量（队列满）
    int64_t total_ticks = 0;        // 累计 tick 次数
};

// 跨 Sector 中继。
// 连接两个 Sector，按容量、损耗、队列约束传输资源。
class CrossSectorRelay {
public:
    CrossSectorRelay() = default;
    CrossSectorRelay(const std::string& id,
                     RelayType type,
                     SectorId from_sector,
                     SectorId to_sector);

    // --- 配置 ---

    const std::string& id() const { return id_; }
    RelayType type() const { return config_.type; }
    SectorId from_sector() const { return from_sector_; }
    SectorId to_sector() const { return to_sector_; }

    void set_config(const RelayConfig& config);
    const RelayConfig& config() const { return config_; }

    // --- 连接状态 ---

    void connect();
    void disconnect();
    void suspend();
    RelayConnectionState connection_state() const { return connection_; }
    bool is_connected() const { return connection_ == RelayConnectionState::Connected; }

    // --- 载荷入队 ---

    // 尝试将载荷加入传输队列。
    // 返回 true 表示入队成功（或直接传输）。
    // 返回 false 表示队列已满或中继未连接。
    // 注意：调用方需通过 CrossSectorRelayManager 加锁或自行保证线程安全。
    bool enqueue(const RelayPayload& payload);

    // --- tick 传输 ---

    // 推进一个 tick，处理队列传输。
    // 返回本 tick 实际传输的载荷列表（已扣除损耗）。
    std::vector<RelayPayload> tick_transfer();

    // --- 队列查询 ---

    size_t queue_size() const;
    bool queue_empty() const;
    int64_t pending_amount() const;  // 队列中待传输的总量

    // --- 统计 ---

    RelayStats stats() const;
    void reset_stats();

    // --- 管理 ---

    void clear_queue();

private:
    std::string id_;
    RelayConfig config_;
    SectorId from_sector_{0};
    SectorId to_sector_{0};
    RelayConnectionState connection_ = RelayConnectionState::Disconnected;

    std::deque<RelayPayload> queue_;
    RelayStats stats_;

    // 计算实际传输量（扣除损耗）
    int64_t compute_transferred(int64_t amount) const;
    int64_t compute_lost(int64_t amount) const;
};

// CrossSectorRelayManager — 管理所有跨 Sector 中继。
//
// 职责：
//   1. 注册/注销中继。
//   2. 查询两个 Sector 之间的中继。
//   3. 批量 tick 所有中继。
//   4. 提供中继断线/重连的批量操作。
class CrossSectorRelayManager {
public:
    CrossSectorRelayManager() = default;
    ~CrossSectorRelayManager() = default;

    CrossSectorRelayManager(const CrossSectorRelayManager&) = delete;
    CrossSectorRelayManager& operator=(const CrossSectorRelayManager&) = delete;

    // --- 注册 ---

    // 注册中继。返回 false 表示 id 已存在。
    bool register_relay(const CrossSectorRelay& relay);

    // 注销中继。
    bool unregister_relay(const std::string& id);

    // --- 查询 ---

    CrossSectorRelay* find_relay(const std::string& id);
    const CrossSectorRelay* find_relay(const std::string& id) const;

    // 查询从 from_sector 到 to_sector 的所有中继。
    std::vector<CrossSectorRelay*> find_relays_between(SectorId from_sector,
                                                        SectorId to_sector);

    // 返回所有中继。
    std::vector<CrossSectorRelay*> all_relays();
    size_t relay_count() const;

    // --- 批量操作 ---

    // 推进所有中继一个 tick。
    // 返回所有中继本 tick 传输的载荷列表。
    std::vector<RelayPayload> tick_all();

    // 断开连接指定 Sector 对的所有中继（用于 Sector 卸载）。
    void disconnect_sector(SectorId sector);

    // 重连指定 Sector 对的所有中继（用于 Sector 恢复）。
    void reconnect_sector(SectorId sector);

    // --- 管理 ---

    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CrossSectorRelay> relays_;
};

} // namespace science_and_theology
