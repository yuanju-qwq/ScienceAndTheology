#include "block_space.hpp"

namespace science_and_theology {

BlockSpace::BlockSpace(SectorManager& sector_manager,
                       SparseChunkPolicy& sparse_policy)
    : sector_manager_(sector_manager)
    , sparse_policy_(sparse_policy) {}

// ============================================================
// 方块查询
// ============================================================

SectorQueryResult BlockSpace::find_sector_for_pos(const GlobalBlockPos& pos) const {
    return sector_manager_.find_sector(pos);
}

BlockQueryResult BlockSpace::get_block(const GlobalBlockPos& pos) const {
    BlockQueryResult result;

    SectorQueryResult q = find_sector_for_pos(pos);
    if (!q.found()) {
        return result;
    }

    result.sector = q.sector;
    result.is_buildable = q.is_buildable();

    // 计算 chunk key
    ChunkCoord cc = block_pos_to_chunk_coord(pos);
    SectorChunkKey key{q.sector, cc};

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        // chunk 不存在，返回空气
        result.found = false;
        return result;
    }

    uint32_t local_index = block_pos_to_chunk_local_index(pos);
    result.found = true;
    result.block = it->second.get(local_index);
    return result;
}

BlockQueryResult BlockSpace::get_neighbor(const GlobalBlockPos& pos,
                                           Direction dir) const {
    GlobalBlockPos neighbor = neighbor_pos(pos, dir);
    return get_block(neighbor);
}

GlobalBlockPos BlockSpace::neighbor_pos(const GlobalBlockPos& pos, Direction dir) {
    return pos + direction_offset(dir);
}

// ============================================================
// 方块操作
// ============================================================

bool BlockSpace::can_build_at(const GlobalBlockPos& pos) const {
    SectorQueryResult q = find_sector_for_pos(pos);
    return q.is_buildable();
}

bool BlockSpace::set_block(const GlobalBlockPos& pos, BlockId block) {
    // 检查是否允许建造
    if (!can_build_at(pos)) {
        return false;
    }

    SectorQueryResult q = find_sector_for_pos(pos);
    ChunkCoord cc = block_pos_to_chunk_coord(pos);
    SectorChunkKey key{q.sector, cc};

    std::lock_guard<std::mutex> lock(mutex_);

    // 确保 chunk 存在
    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        // 按需创建 chunk
        // 对于稀疏 Sector，标记 chunk 有内容
        const SectorDesc* desc = sector_manager_.get_sector_desc(q.sector);
        if (desc != nullptr) {
            if (!sparse_policy_.should_generate_chunk(*desc, cc)) {
                sparse_policy_.mark_chunk_has_content(q.sector, cc);
            }
        }
        it = chunks_.emplace(key, ChunkBlocks{}).first;
    }

    uint32_t local_index = block_pos_to_chunk_local_index(pos);
    it->second.set(local_index, block);
    dirty_chunks_.insert(key);
    return true;
}

bool BlockSpace::dig_block(const GlobalBlockPos& pos) {
    SectorQueryResult q = find_sector_for_pos(pos);
    if (!q.found()) {
        return false;
    }

    ChunkCoord cc = block_pos_to_chunk_coord(pos);
    SectorChunkKey key{q.sector, cc};

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        return false;  // chunk 不存在，无需挖掘
    }

    uint32_t local_index = block_pos_to_chunk_local_index(pos);
    if (it->second.get(local_index) == kBlockAir) {
        return false;  // 已经是空气
    }

    it->second.set(local_index, kBlockAir);
    dirty_chunks_.insert(key);

    // 若 chunk 变空，取消稀疏标记（由 StructureAnchorManager 决定是否回收）
    if (it->second.is_empty()) {
        sparse_policy_.unmark_chunk_has_content(q.sector, cc);
    }

    return true;
}

// ============================================================
// Chunk 管理
// ============================================================

bool BlockSpace::has_chunk(const SectorChunkKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.find(key) != chunks_.end();
}

ChunkBlocks* BlockSpace::ensure_chunk(const SectorChunkKey& key) {
    const SectorDesc* desc = sector_manager_.get_sector_desc(key.sector);
    if (desc == nullptr || !desc->allow_voxel_building) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = chunks_.find(key);
    if (it != chunks_.end()) {
        return &it->second;
    }

    // 标记稀疏 chunk 有内容
    if (!sparse_policy_.should_generate_chunk(*desc, key.coord)) {
        sparse_policy_.mark_chunk_has_content(key.sector, key.coord);
    }

    it = chunks_.emplace(key, ChunkBlocks{}).first;
    return &it->second;
}

const ChunkBlocks* BlockSpace::get_chunk(const SectorChunkKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        return nullptr;
    }
    return &it->second;
}

void BlockSpace::remove_chunk(const SectorChunkKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.erase(key);
    dirty_chunks_.erase(key);
    sparse_policy_.unmark_chunk_has_content(key.sector, key.coord);
}

size_t BlockSpace::chunk_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

std::vector<SectorChunkKey> BlockSpace::all_chunk_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SectorChunkKey> result;
    result.reserve(chunks_.size());
    for (const auto& [key, _] : chunks_) {
        result.push_back(key);
    }
    return result;
}

// ============================================================
// Dirty 标记
// ============================================================

void BlockSpace::mark_dirty(const SectorChunkKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_chunks_.insert(key);
}

void BlockSpace::clear_dirty(const SectorChunkKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_chunks_.erase(key);
}

bool BlockSpace::is_dirty(const SectorChunkKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dirty_chunks_.find(key) != dirty_chunks_.end();
}

std::vector<SectorChunkKey> BlockSpace::get_dirty_chunks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SectorChunkKey> result;
    result.reserve(dirty_chunks_.size());
    for (const auto& key : dirty_chunks_) {
        result.push_back(key);
    }
    return result;
}

size_t BlockSpace::dirty_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dirty_chunks_.size();
}

// ============================================================
// 管理
// ============================================================

void BlockSpace::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
    dirty_chunks_.clear();
}

} // namespace science_and_theology
