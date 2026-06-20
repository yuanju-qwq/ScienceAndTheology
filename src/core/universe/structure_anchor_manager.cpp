#include "structure_anchor_manager.hpp"

#include <algorithm>

namespace science_and_theology {

// ============================================================
// 配置
// ============================================================

void StructureAnchorManager::set_config(const BuildBudgetConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

const BuildBudgetConfig& StructureAnchorManager::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

// ============================================================
// 锚点管理
// ============================================================

uint64_t StructureAnchorManager::register_anchor(
    const std::string& name,
    const std::string& owner_id,
    const std::vector<SectorChunkKey>& chunks) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t id = next_anchor_id_++;
    StructureAnchor anchor;
    anchor.id = id;
    anchor.name = name;
    anchor.owner_id = owner_id;
    anchor.anchored_chunks = chunks;

    anchors_.emplace(id, std::move(anchor));

    // 建立反向索引
    for (const auto& key : chunks) {
        chunk_anchors_[key].push_back(id);
    }

    return id;
}

bool StructureAnchorManager::unregister_anchor(uint64_t anchor_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = anchors_.find(anchor_id);
    if (it == anchors_.end()) {
        return false;
    }

    // 移除反向索引
    for (const auto& key : it->second.anchored_chunks) {
        auto ca_it = chunk_anchors_.find(key);
        if (ca_it != chunk_anchors_.end()) {
            auto& ids = ca_it->second;
            ids.erase(std::remove(ids.begin(), ids.end(), anchor_id), ids.end());
            if (ids.empty()) {
                chunk_anchors_.erase(ca_it);
            }
        }
    }

    anchors_.erase(it);
    return true;
}

const StructureAnchor* StructureAnchorManager::get_anchor(uint64_t anchor_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = anchors_.find(anchor_id);
    if (it == anchors_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const StructureAnchor*> StructureAnchorManager::all_anchors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const StructureAnchor*> result;
    result.reserve(anchors_.size());
    for (const auto& [id, anchor] : anchors_) {
        result.push_back(&anchor);
    }
    return result;
}

bool StructureAnchorManager::is_chunk_anchored(const SectorChunkKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunk_anchors_.find(key) != chunk_anchors_.end();
}

// ============================================================
// 空 chunk 回收
// ============================================================

bool StructureAnchorManager::can_reclaim_chunk(const SectorChunkKey& key,
                                                 const BlockSpace& block_space) const {
    // 被锚定的 chunk 不可回收
    if (is_chunk_anchored(key)) {
        return false;
    }

    // chunk 必须存在
    const ChunkBlocks* chunk = block_space.get_chunk(key);
    if (chunk == nullptr) {
        return false;
    }

    // chunk 必须为空
    return chunk->is_empty();
}

int StructureAnchorManager::reclaim_empty_chunks(BlockSpace& block_space) {
    auto all_keys = block_space.all_chunk_keys();

    int reclaimed = 0;
    for (const auto& key : all_keys) {
        if (can_reclaim_chunk(key, block_space)) {
            block_space.remove_chunk(key);
            ++reclaimed;
        }
    }
    return reclaimed;
}

// ============================================================
// 建造预算
// ============================================================

bool StructureAnchorManager::can_create_chunk(
    SectorId sector,
    const BlockSpace& block_space) const {

    std::lock_guard<std::mutex> lock(mutex_);

    // 检查全局预算
    if (static_cast<int>(block_space.chunk_count()) >= config_.max_total_chunks) {
        return false;
    }

    // 检查单 Sector 预算
    // 统计该 Sector 的 chunk 数
    int sector_count = 0;
    for (const auto& key : block_space.all_chunk_keys()) {
        if (key.sector == sector) {
            ++sector_count;
        }
    }
    if (sector_count >= config_.max_chunks_per_sector) {
        return false;
    }

    return true;
}

bool StructureAnchorManager::can_create_chunks(int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_operation_chunks_ + count <= config_.max_chunks_per_operation;
}

void StructureAnchorManager::record_chunk_creation(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_operation_chunks_ += count;
}

void StructureAnchorManager::reset_operation_budget() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_operation_chunks_ = 0;
}

// ============================================================
// 管理
// ============================================================

void StructureAnchorManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    anchors_.clear();
    chunk_anchors_.clear();
    next_anchor_id_ = 1;
    current_operation_chunks_ = 0;
}

size_t StructureAnchorManager::anchor_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return anchors_.size();
}

} // namespace science_and_theology
