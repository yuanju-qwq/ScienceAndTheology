#include "virtual_planet_simulator.hpp"

namespace science_and_theology {

// ============================================================
// 会话管理
// ============================================================

std::string VirtualPlanetSimulator::begin_session(SectorId sector,
                                                   int64_t start_tick,
                                                   SimulationLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 生成唯一 session_id
    std::string session_id = "session_" + std::to_string(next_session_seq_++);
    session_id += "_s" + std::to_string(sector.value);
    session_id += "_t" + std::to_string(start_tick);

    SimulationSession session;
    session.session_id = session_id;
    session.sector = sector;
    session.start_tick = start_tick;
    session.end_tick = start_tick;
    session.level = level;
    session.completed = false;
    session.replayed = false;

    sessions_.emplace(session_id, session);
    session_batch_seq_[session_id] = 0;

    return session_id;
}

bool VirtualPlanetSimulator::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    it->second.completed = true;
    return true;
}

bool VirtualPlanetSimulator::cancel_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    // 取消：标记为未完成，但保留已生成的批次
    it->second.completed = false;
    return true;
}

const SimulationSession* VirtualPlanetSimulator::find_session(
    const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    return &it->second;
}

size_t VirtualPlanetSimulator::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

// ============================================================
// 产物批次管理
// ============================================================

std::optional<ProductBatch> VirtualPlanetSimulator::simulate_batch(
    const std::string& session_id,
    int64_t simulated_tick,
    const std::vector<ProductEntry>& products) {

    std::lock_guard<std::mutex> lock(mutex_);

    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        return std::nullopt;
    }

    // 已完成的会话不再生成新批次
    if (session_it->second.completed) {
        return std::nullopt;
    }

    // 生成 batch_id
    int seq = session_batch_seq_[session_id]++;
    std::string batch_id = session_id + "_b" + std::to_string(seq);

    ProductBatch batch;
    batch.batch_id = batch_id;
    batch.session_id = session_id;
    batch.sector = session_it->second.sector;
    batch.simulated_tick = simulated_tick;
    batch.products = products;
    batch.replayed = false;

    // 更新会话的 end_tick
    if (simulated_tick > session_it->second.end_tick) {
        session_it->second.end_tick = simulated_tick;
    }

    batches_.emplace(batch_id, batch);
    return batch;
}

std::vector<ProductBatch> VirtualPlanetSimulator::batches_for_session(
    const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ProductBatch> result;
    for (const auto& [id, batch] : batches_) {
        if (batch.session_id == session_id) {
            result.push_back(batch);
        }
    }
    return result;
}

std::vector<ProductBatch> VirtualPlanetSimulator::pending_batches() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ProductBatch> result;
    for (const auto& [id, batch] : batches_) {
        if (!batch.replayed && replayed_batch_ids_.find(id) == replayed_batch_ids_.end()) {
            result.push_back(batch);
        }
    }
    return result;
}

// ============================================================
// 回灌（幂等）
// ============================================================

ReplayResult VirtualPlanetSimulator::replay_batch(const std::string& batch_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    ReplayResult result;
    result.batch_id = batch_id;

    // 幂等检查：已回灌的批次直接返回成功
    if (replayed_batch_ids_.find(batch_id) != replayed_batch_ids_.end()) {
        result.success = true;
        result.reason = "already replayed (idempotent)";
        return result;
    }

    auto it = batches_.find(batch_id);
    if (it == batches_.end()) {
        result.success = false;
        result.reason = "batch not found";
        return result;
    }

    // 标记为已回灌
    replayed_batch_ids_.insert(batch_id);
    it->second.replayed = true;

    result.success = true;
    result.applied_products = it->second.products;
    return result;
}

int VirtualPlanetSimulator::replay_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (auto& [id, batch] : batches_) {
        if (batch.session_id == session_id &&
            !batch.replayed &&
            replayed_batch_ids_.find(id) == replayed_batch_ids_.end()) {
            replayed_batch_ids_.insert(id);
            batch.replayed = true;
            ++count;
        }
    }

    // 标记会话为已回灌
    auto session_it = sessions_.find(session_id);
    if (session_it != sessions_.end()) {
        session_it->second.replayed = true;
    }

    return count;
}

int VirtualPlanetSimulator::replay_all_pending() {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (auto& [id, batch] : batches_) {
        if (!batch.replayed &&
            replayed_batch_ids_.find(id) == replayed_batch_ids_.end()) {
            replayed_batch_ids_.insert(id);
            batch.replayed = true;
            ++count;
        }
    }
    return count;
}

bool VirtualPlanetSimulator::is_batch_replayed(const std::string& batch_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replayed_batch_ids_.find(batch_id) != replayed_batch_ids_.end();
}

// ============================================================
// 回灌日志
// ============================================================

std::vector<std::string> VirtualPlanetSimulator::export_replayed_batch_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(replayed_batch_ids_.begin(),
                                     replayed_batch_ids_.end());
}

void VirtualPlanetSimulator::import_replayed_batch_ids(
    const std::vector<std::string>& ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& id : ids) {
        replayed_batch_ids_.insert(id);
        // 同步标记批次为已回灌
        auto it = batches_.find(id);
        if (it != batches_.end()) {
            it->second.replayed = true;
        }
    }
}

// ============================================================
// 统计
// ============================================================

size_t VirtualPlanetSimulator::total_batch_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batches_.size();
}

size_t VirtualPlanetSimulator::replayed_batch_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replayed_batch_ids_.size();
}

size_t VirtualPlanetSimulator::pending_batch_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = batches_.size();
    size_t replayed = 0;
    for (const auto& [id, batch] : batches_) {
        if (replayed_batch_ids_.find(id) != replayed_batch_ids_.end()) {
            ++replayed;
        }
    }
    return total - replayed;
}

// ============================================================
// 管理
// ============================================================

void VirtualPlanetSimulator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    batches_.clear();
    replayed_batch_ids_.clear();
    session_batch_seq_.clear();
    next_session_seq_ = 1;
}

} // namespace science_and_theology
