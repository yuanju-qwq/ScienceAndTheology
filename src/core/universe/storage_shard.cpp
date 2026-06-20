#include "storage_shard.hpp"

namespace science_and_theology {

void StorageShardMap::register_shard(const std::string& dimension_id, SectorId sector) {
    std::lock_guard<std::mutex> lock(mutex_);
    shard_to_sector_[dimension_id] = sector;
    sector_to_shard_[sector] = dimension_id;
}

std::optional<SectorId> StorageShardMap::find_sector(const std::string& dimension_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shard_to_sector_.find(dimension_id);
    if (it == shard_to_sector_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string StorageShardMap::find_legacy_dimension_id(SectorId sector) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sector_to_shard_.find(sector);
    if (it == sector_to_shard_.end()) {
        return {};
    }
    return it->second;
}

size_t StorageShardMap::shard_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shard_to_sector_.size();
}

void StorageShardMap::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    shard_to_sector_.clear();
    sector_to_shard_.clear();
}

StorageShardMap& global_storage_shard_map() {
    static StorageShardMap instance;
    return instance;
}

} // namespace science_and_theology
