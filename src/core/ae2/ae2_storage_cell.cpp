#include "ae2_storage_cell.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace science_and_theology::gt {

// ============================================================
// StorageCell
// ============================================================

StorageCell::StorageCell(int64_t byte_capacity, int max_types,
                         int64_t bytes_per_type)
    : byte_capacity_(byte_capacity)
    , max_types_(max_types)
    , bytes_per_type_(bytes_per_type) {}

std::string StorageCell::title_key() const {
    return "storage_cell." + std::to_string(byte_capacity_) + "." + std::to_string(max_types_);
}

int64_t StorageCell::bytes_for(const ResourceId& key, int64_t count) const {
    if (count <= 0) return 0;
    int64_t upb = units_per_byte(key);
    if (upb <= 0) upb = 1;
    return (count + upb - 1) / upb;
}

int64_t StorageCell::available(const ResourceId& key) const {
    auto it = items_.find(key);
    return it != items_.end() ? it->second : 0;
}

int64_t StorageCell::available(const ResourceKey& key) const {
    return available(ResourceId::from_key(key));
}

int64_t StorageCell::used_bytes() const {
    int64_t bytes = 0;
    for (const auto& [rid, count] : items_) {
        bytes += bytes_for(rid, count);
        bytes += bytes_per_type_;
    }
    return bytes;
}

bool StorageCell::is_full(const ResourceId& key) const {
    int64_t current_used = used_bytes();
    if (current_used >= byte_capacity_) return true;

    auto it = items_.find(key);
    if (it == items_.end()) {
        // New type: need room for type overhead.
        if (static_cast<int>(type_count()) >= max_types_) return true;
        return current_used + bytes_per_type_ >= byte_capacity_;
    }
    return false;
}

int64_t StorageCell::insert(const ResourceId& key, int64_t amount) {
    if (amount <= 0) return 0;

    auto it = items_.find(key);
    bool is_new = (it == items_.end());

    if (is_new && static_cast<int>(type_count()) >= max_types_) {
        return amount; // Full — reject.
    }

    int64_t current_used = used_bytes();
    int64_t free = byte_capacity_ - current_used;
    if (is_new) free -= bytes_per_type_;
    if (free <= 0) return amount;

    int64_t upb = units_per_byte(key);
    if (upb <= 0) upb = 1;
    int64_t max_by_capacity = free * upb;
    int64_t accepted = std::min(amount, max_by_capacity);

    if (accepted > 0) {
        items_[key] += accepted;
    }

    return amount - accepted;
}

int64_t StorageCell::extract(const ResourceId& key, int64_t amount) {
    if (amount <= 0) return 0;

    auto it = items_.find(key);
    if (it == items_.end()) return 0;

    int64_t taken = std::min(amount, it->second);
    it->second -= taken;
    if (it->second <= 0) {
        items_.erase(it);
    }

    return taken;
}

std::vector<ResourceId> StorageCell::stored_types() const {
    std::vector<ResourceId> result;
    for (const auto& [rid, _] : items_) {
        result.push_back(rid);
    }
    return result;
}

// ============================================================
// ExternalStorage
// ============================================================

ExternalStorage::ExternalStorage(
        std::string label,
        int64_t byte_capacity,
        CheckCallback check,
        ExtractCallback take,
        InsertCallback put,
        TypesCallback types)
    : label_(std::move(label))
    , byte_capacity_(byte_capacity)
    , check_(std::move(check))
    , extract_(std::move(take))
    , insert_(std::move(put))
    , types_(std::move(types)) {}

int64_t ExternalStorage::available(const ResourceId& key) const {
    if (check_) return check_(key);
    return 0;
}

int64_t ExternalStorage::insert(const ResourceId& key, int64_t amount) {
    if (insert_) return insert_(key, amount);
    return amount; // Reject all.
}

int64_t ExternalStorage::extract(const ResourceId& key, int64_t amount) {
    if (extract_) return extract_(key, amount);
    return 0;
}

std::vector<ResourceId> ExternalStorage::stored_types() const {
    if (types_) return types_();
    return {};
}

} // namespace science_and_theology::gt
