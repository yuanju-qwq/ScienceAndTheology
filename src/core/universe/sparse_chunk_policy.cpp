#include "sparse_chunk_policy.hpp"

namespace science_and_theology {

bool SparseChunkPolicy::should_generate_chunk(const SectorDesc& sector_desc,
                                              const ChunkCoord& coord) const {
    switch (sector_desc.kind) {
        case SectorKind::PlanetSurface:
            // 地表总是有地形，总是生成
            return true;

        case SectorKind::SpaceBridge:
            // 太空桥沿桥方向生成；当前简化为总是生成
            // 后续可根据桥的轴向过滤
            return true;

        case SectorKind::PlanetOrbit:
        case SectorKind::DeepSpace:
        case SectorKind::SpaceStation:
        case SectorKind::AsteroidField:
            // 稀疏：只在被标记有内容时生成
            return is_chunk_marked(sector_desc.id, coord);

        default:
            return false;
    }
}

void SparseChunkPolicy::mark_chunk_has_content(SectorId sector,
                                               const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(mutex_);
    marked_chunks_.insert(SectorChunkKey{sector, coord});
}

void SparseChunkPolicy::unmark_chunk_has_content(SectorId sector,
                                                 const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(mutex_);
    marked_chunks_.erase(SectorChunkKey{sector, coord});
}

bool SparseChunkPolicy::is_chunk_marked(SectorId sector,
                                        const ChunkCoord& coord) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return marked_chunks_.find(SectorChunkKey{sector, coord}) != marked_chunks_.end();
}

void SparseChunkPolicy::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    marked_chunks_.clear();
}

size_t SparseChunkPolicy::marked_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return marked_chunks_.size();
}

} // namespace science_and_theology
