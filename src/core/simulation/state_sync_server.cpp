#include "state_sync_server.hpp"
#include <chrono>

namespace science_and_theology {

void StateSyncServer::mark_dirty(const ChunkKey& key, SyncFlags flags) {
    dirty_chunks_[key] |= flags;
}

void StateSyncServer::mark_dirty(
    const std::string& layer, int cx, int cy, SyncFlags flags) {
    ChunkKey key{layer, cx, cy};
    mark_dirty(key, flags);
}

StateDelta StateSyncServer::compute_delta(
    const std::vector<ChunkKey>& observed_chunks) {
    StateDelta delta;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    delta.timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    for (const auto& chunk_key : observed_chunks) {
        auto it = dirty_chunks_.find(chunk_key);
        if (it == dirty_chunks_.end()) continue;

        delta.flags |= it->second;
        delta.chunks_modified.push_back(chunk_key);

        // Read entity changes from the chunk if world_data_ is available.
        if (world_data_) {
            auto* chunk = world_data_->get_chunk(
                chunk_key.layer_id, chunk_key.chunk_x, chunk_key.chunk_y);
            if (chunk) {
                if ((it->second & SyncFlags::ENTITY) != SyncFlags::NONE) {
                    for (auto eid : chunk->entities) {
                        delta.entities_created.push_back(eid);
                    }
                }
            }
        }

        // Clear dirty after computing delta.
        it->second = SyncFlags::NONE;
    }

    // Remove fully clean entries.
    for (auto it = dirty_chunks_.begin(); it != dirty_chunks_.end(); ) {
        if (it->second == SyncFlags::NONE) {
            it = dirty_chunks_.erase(it);
        } else {
            ++it;
        }
    }

    return delta;
}

StateDelta StateSyncServer::create_snapshot(const ChunkKey& key) const {
    StateDelta delta;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    delta.timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    delta.flags = SyncFlags::ALL;
    delta.chunks_modified.push_back(key);

    if (world_data_) {
        auto* chunk = world_data_->get_chunk(
            key.layer_id, key.chunk_x, key.chunk_y);
        if (chunk) {
            for (auto eid : chunk->entities) {
                delta.entities_created.push_back(eid);
            }
        }
    }

    return delta;
}

void StateSyncServer::clear_dirty(const ChunkKey& key, SyncFlags flags) {
    auto it = dirty_chunks_.find(key);
    if (it != dirty_chunks_.end()) {
        it->second = it->second & static_cast<SyncFlags>(
            ~static_cast<uint32_t>(flags));
        if (it->second == SyncFlags::NONE) {
            dirty_chunks_.erase(it);
        }
    }
}

bool StateSyncServer::is_dirty(const ChunkKey& key) const {
    auto it = dirty_chunks_.find(key);
    return it != dirty_chunks_.end() && it->second != SyncFlags::NONE;
}

SyncFlags StateSyncServer::get_dirty_flags(const ChunkKey& key) const {
    auto it = dirty_chunks_.find(key);
    if (it != dirty_chunks_.end()) {
        return it->second;
    }
    return SyncFlags::NONE;
}

void StateSyncServer::clear_all() {
    dirty_chunks_.clear();
}

std::vector<ChunkKey> StateSyncServer::dirty_chunks() const {
    std::vector<ChunkKey> result;
    result.reserve(dirty_chunks_.size());
    for (const auto& pair : dirty_chunks_) {
        if (pair.second != SyncFlags::NONE) {
            result.push_back(pair.first);
        }
    }
    return result;
}

} // namespace science_and_theology
