#include "multi_sector_sync_coordinator.hpp"

#include "interest_manager.hpp"
#include "../simulation/state_sync_server.hpp"
#include "sector_manager.hpp"
#include "universe_world_core.hpp"
#include "celestial_lod_system.hpp"
#include "flight_state_tracker.hpp"
#include "../simulation/state_sync_common.hpp"
#include "../world/chunk_data.hpp"

#include <algorithm>
#include <sstream>

namespace science_and_theology {

// ============================================================
// 工具：SectorId → dimension_id 转换
// ============================================================

std::string MultiSectorSyncCoordinator::sector_to_dimension_id(uint64_t sector_value) {
    std::ostringstream oss;
    oss << "sector_" << sector_value;
    return oss.str();
}

std::string MultiSectorSyncCoordinator::sector_to_dimension_id(SectorId sector) {
    return sector_to_dimension_id(sector.value);
}

// ============================================================
// 玩家生命周期
// ============================================================

void MultiSectorSyncCoordinator::register_player(PlayerHandle id,
                                                  const GlobalPos& pos,
                                                  const GlobalPos& velocity,
                                                  SectorId current_sector) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 避免重复注册
    if (std::find(registered_players_.begin(),
                  registered_players_.end(), id) != registered_players_.end()) {
        return;
    }
    registered_players_.push_back(id);

    if (interest_manager_) {
        interest_manager_->register_player(id, pos, velocity, current_sector);
    }
    if (observer_map_) {
        observer_map_->register_player(id);
        observer_map_->set_player_sector(id, current_sector);
    }
    if (budget_manager_) {
        budget_manager_->register_observer(id);
    }
    if (state_sync_server_) {
        state_sync_server_->register_observer(id);
    }
}

void MultiSectorSyncCoordinator::unregister_player(PlayerHandle id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(registered_players_.begin(),
                        registered_players_.end(), id);
    if (it == registered_players_.end()) {
        return;
    }
    registered_players_.erase(it);

    if (interest_manager_) {
        interest_manager_->unregister_player(id);
    }
    if (observer_map_) {
        observer_map_->unregister_player(id);
    }
    if (budget_manager_) {
        budget_manager_->unregister_observer(id);
    }
    if (state_sync_server_) {
        state_sync_server_->unregister_observer(id);
    }

    last_batches_.erase(id);
}

std::optional<FlightModeChangeEvent> MultiSectorSyncCoordinator::update_player(
    PlayerHandle id,
    const GlobalPos& pos,
    const GlobalPos& velocity) {

    // 不需要加自己的锁：InterestManager 内部加锁
    if (!interest_manager_) {
        return std::nullopt;
    }
    return interest_manager_->update_player(id, pos, velocity);
}

void MultiSectorSyncCoordinator::set_landing_target(
    PlayerHandle id,
    const std::string& celestial_id,
    double distance_to_surface) {

    if (interest_manager_) {
        interest_manager_->set_landing_target(id, celestial_id, distance_to_surface);
    }
}

void MultiSectorSyncCoordinator::clear_landing_target(PlayerHandle id) {
    if (interest_manager_) {
        interest_manager_->clear_landing_target(id);
    }
}

// ============================================================
// 主 tick：计算并收集所有 delta，应用预算
// ============================================================

std::vector<ObserverSendBatch> MultiSectorSyncCoordinator::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ObserverSendBatch> batches;
    if (registered_players_.empty()) {
        // 仍要推进预算管理器的 tick
        if (budget_manager_) {
            budget_manager_->tick();
        }
        return batches;
    }

    // --- 阶段 1：对每个玩家计算兴趣集合，更新 SectorObserverMap ---

    struct PlayerInterestInfo {
        PlayerHandle player_handle;
        SectorId sector;
        InterestSet interest;
    };

    std::vector<PlayerInterestInfo> interests;
    interests.reserve(registered_players_.size());

    for (PlayerHandle pid : registered_players_) {
        PlayerInterestInfo info;
        info.player_handle = pid;

        if (interest_manager_ && sector_manager_ && universe_core_) {
            info.interest = interest_manager_->compute_interest(
                pid, *sector_manager_, *universe_core_);
        }

        // 从 SectorObserverMap 获取当前 Sector（compute_interest 不返回 Sector）
        if (observer_map_) {
            info.sector = observer_map_->get_player_sector(pid);
        }

        // 更新 SectorObserverMap 的观察 chunk 集合
        // 只包含属于当前 Sector 的 chunk（SectorObserverMap 内部会过滤）
        if (observer_map_ && !info.sector.is_valid()) {
            // 玩家未进入任何 Sector，跳过
            interests.push_back(std::move(info));
            continue;
        }

        if (observer_map_) {
            observer_map_->set_observed_chunks(pid, info.interest.chunks);
        }

        interests.push_back(std::move(info));
    }

    // --- 阶段 2：构建 observer_views，批量计算 delta ---

    std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>> observer_views;
    observer_views.reserve(interests.size());

    for (const auto& info : interests) {
        if (!info.sector.is_valid()) {
            continue;
        }

        // 将 SectorChunkKey 转换为 ChunkKey
        std::vector<ChunkKey> chunks;
        chunks.reserve(info.interest.chunks.size());
        std::string dim_id = sector_to_dimension_id(info.sector);
        for (const auto& sck : info.interest.chunks) {
            // 只包含属于当前 Sector 的 chunk（双重保险）
            if (sck.sector != info.sector) {
                continue;
            }
            chunks.emplace_back(dim_id,
                                static_cast<int>(sck.coord.cx),
                                static_cast<int>(sck.coord.cy),
                                static_cast<int>(sck.coord.cz));
        }
        observer_views.emplace_back(info.player_handle, std::move(chunks));
    }

    // 批量计算 delta
    std::vector<std::pair<PlayerHandle, StateDelta>> deltas;
    if (state_sync_server_ && !observer_views.empty()) {
        deltas = state_sync_server_->compute_deltas_batch(observer_views);
    }

    // --- 阶段 3：应用 per-observer、per-channel 预算，构建发送批次 ---

    // 构建 player_handle → delta 的映射
    std::unordered_map<PlayerHandle, StateDelta> delta_map;
    for (auto& [pid, delta] : deltas) {
        delta_map[pid] = std::move(delta);
    }

    for (const auto& info : interests) {
        ObserverSendBatch batch = build_batch_for(
            info.player_handle, info.sector, info.interest,
            delta_map.count(info.player_handle) ? delta_map[info.player_handle] : StateDelta{});

        batches.push_back(std::move(batch));
    }

    // 保存 last_batches_
    last_batches_.clear();
    for (const auto& batch : batches) {
        last_batches_[batch.observer] = batch;
    }

    // --- 阶段 4：推进预算管理器的 tick（重置 per-tick 预算） ---
    if (budget_manager_) {
        budget_manager_->tick();
    }

    return batches;
}

// ============================================================
// 构建单个玩家的发送批次（应用预算）
// ============================================================

ObserverSendBatch MultiSectorSyncCoordinator::build_batch_for(
    PlayerHandle observer,
    SectorId sector,
    const InterestSet& interest,
    const StateDelta& delta) {

    ObserverSendBatch batch;
    batch.observer = observer;
    batch.sector = sector;

    // --- Chunk 通道：初始 chunk 加载（来自 InterestSet.chunks） ---
    if (!interest.chunks.empty()) {
        int total_chunks = static_cast<int>(interest.chunks.size());
        SendResult chunk_result;
        if (budget_manager_) {
            chunk_result = budget_manager_->try_send(
                observer, SyncChannel::Chunk, total_chunks);
        } else {
            chunk_result.allowed = total_chunks;
        }

        int to_send = std::min(chunk_result.allowed,
                               static_cast<int>(interest.chunks.size()));
        for (int i = 0; i < to_send; ++i) {
            batch.chunks.push_back(interest.chunks[i]);
        }
        batch.chunks_deferred = chunk_result.deferred;
    }

    // --- Entity 通道：实体创建/销毁（来自 StateDelta） ---
    int total_entities = static_cast<int>(
        delta.entities_created.size() + delta.entities_destroyed.size());
    if (total_entities > 0) {
        SendResult entity_result;
        if (budget_manager_) {
            entity_result = budget_manager_->try_send(
                observer, SyncChannel::Entity, total_entities);
        } else {
            entity_result.allowed = total_entities;
        }

        int remaining = entity_result.allowed;
        // 先发送 entities_created
        for (size_t i = 0; i < delta.entities_created.size() && remaining > 0; ++i) {
            batch.entities_created.push_back(delta.entities_created[i]);
            --remaining;
        }
        // 再发送 entities_destroyed
        for (size_t i = 0; i < delta.entities_destroyed.size() && remaining > 0; ++i) {
            batch.entities_destroyed.push_back(delta.entities_destroyed[i]);
            --remaining;
        }
        batch.entities_deferred = entity_result.deferred;
    }

    // --- BlockDelta 通道：方块增量（来自 StateDelta.chunks_modified） ---
    if (!delta.chunks_modified.empty()) {
        int total_deltas = static_cast<int>(delta.chunks_modified.size());
        SendResult delta_result;
        if (budget_manager_) {
            delta_result = budget_manager_->try_send(
                observer, SyncChannel::BlockDelta, total_deltas);
        } else {
            delta_result.allowed = total_deltas;
        }

        // 将 ChunkKey 转换回 SectorChunkKey
        int to_send = std::min(delta_result.allowed,
                               static_cast<int>(delta.chunks_modified.size()));
        for (int i = 0; i < to_send; ++i) {
            const auto& ck = delta.chunks_modified[i];
            SectorChunkKey sck;
            sck.sector = sector;
            sck.coord = ChunkCoord(
                static_cast<int64_t>(ck.chunk_x),
                static_cast<int64_t>(ck.chunk_y),
                static_cast<int64_t>(ck.chunk_z));
            batch.block_deltas.push_back(sck);
        }
        batch.block_deltas_deferred = delta_result.deferred;
    }

    // --- MachineDetail 通道：机器详情（来自 StateDelta.machine_state_changes） ---
    if (!delta.machine_state_changes.empty()) {
        int total_machines = static_cast<int>(delta.machine_state_changes.size());
        SendResult machine_result;
        if (budget_manager_) {
            machine_result = budget_manager_->try_send(
                observer, SyncChannel::MachineDetail, total_machines);
        } else {
            machine_result.allowed = total_machines;
        }

        int to_send = std::min(machine_result.allowed,
                               static_cast<int>(delta.machine_state_changes.size()));
        for (int i = 0; i < to_send; ++i) {
            batch.machine_details.push_back(delta.machine_state_changes[i]);
        }
        // MachineDetail 通道默认 drop_on_overflow=false（不推迟，直接丢弃）
        batch.machine_details_dropped = machine_result.dropped;
    }

    // --- CelestialLod 通道：天体 LOD（来自 InterestSet.celestial_lods） ---
    if (!interest.celestial_lods.empty()) {
        int total_lods = static_cast<int>(interest.celestial_lods.size());
        SendResult lod_result;
        if (budget_manager_) {
            lod_result = budget_manager_->try_send(
                observer, SyncChannel::CelestialLod, total_lods);
        } else {
            lod_result.allowed = total_lods;
        }

        int to_send = std::min(lod_result.allowed,
                               static_cast<int>(interest.celestial_lods.size()));
        for (int i = 0; i < to_send; ++i) {
            batch.celestial_lods.push_back(interest.celestial_lods[i]);
        }
        batch.celestial_lods_dropped = lod_result.dropped;
    }

    return batch;
}

// ============================================================
// 查询
// ============================================================

ObserverSendBatch MultiSectorSyncCoordinator::get_last_batch(PlayerHandle id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_batches_.find(id);
    if (it == last_batches_.end()) {
        return ObserverSendBatch{};
    }
    return it->second;
}

bool MultiSectorSyncCoordinator::is_chunk_visible_to(
    PlayerHandle observer,
    const SectorChunkKey& chunk) const {

    if (!observer_map_) {
        return false;
    }

    SectorId player_sector = observer_map_->get_player_sector(observer);
    if (!player_sector.is_valid()) {
        return false;
    }

    // 跨 Sector 隔离：chunk 必须属于玩家当前 Sector
    return chunk.sector == player_sector;
}

bool MultiSectorSyncCoordinator::are_converged(PlayerHandle a, PlayerHandle b) const {
    if (!observer_map_) {
        return false;
    }

    // 会合条件：两人在同一 Sector
    if (!observer_map_->are_in_same_sector(a, b)) {
        return false;
    }

    // 且观察集有重叠（至少一个共同 chunk）
    auto chunks_a = observer_map_->get_observed_chunks(a);
    auto chunks_b = observer_map_->get_observed_chunks(b);

    if (chunks_a.empty() || chunks_b.empty()) {
        return false;
    }

    // 检查是否有重叠
    for (const auto& ca : chunks_a) {
        for (const auto& cb : chunks_b) {
            if (ca == cb) {
                return true;
            }
        }
    }

    return false;
}

std::vector<PlayerHandle> MultiSectorSyncCoordinator::all_player_handles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registered_players_;
}

size_t MultiSectorSyncCoordinator::player_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registered_players_.size();
}

// ============================================================
// 管理
// ============================================================

void MultiSectorSyncCoordinator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    registered_players_.clear();
    last_batches_.clear();
}

} // namespace science_and_theology
