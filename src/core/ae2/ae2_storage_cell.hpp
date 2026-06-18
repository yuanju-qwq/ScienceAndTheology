#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ae2_resource_id.hpp"
#include "common/resource_key.hpp"

namespace science_and_theology::gt {

// ============================================================
// IStorage — unified storage interface
// ============================================================
//
// Both StorageCell and ExternalStorage implement this.
// MENetwork uses IStorage* uniformly for query/extract/insert.

class IStorage {
public:
    virtual ~IStorage() = default;

    virtual std::string title_key() const = 0;

    virtual int64_t available(const ResourceId& key) const = 0;
    virtual int64_t insert(const ResourceId& key, int64_t amount) = 0;
    virtual int64_t extract(const ResourceId& key, int64_t amount) = 0;

    virtual std::vector<ResourceId> stored_types() const = 0;

    virtual int64_t total_bytes() const = 0;
    virtual int64_t used_bytes() const = 0;
    int64_t free_bytes() const { return total_bytes() - used_bytes(); }
};

// ============================================================
// StorageCell — digital storage unit (1k, 4k, 16k, 64k)
// ============================================================
//
// Mirrors AE2's Storage Cell (item cell, fluid cell).
// Stores items/fluids by ResourceId. Capacity measured in bytes,
// with per-type efficiency from ResourceKeyType::amount_per_byte().

class StorageCell : public IStorage {
public:
    StorageCell(int64_t byte_capacity, int max_types,
                int64_t bytes_per_type = 8);

    std::string title_key() const override;

    // --- Query ---

    int64_t available(const ResourceId& key) const override;
    int64_t available(const ResourceKey& key) const;

    int64_t total_bytes() const override { return byte_capacity_; }
    int64_t used_bytes() const override;
    int64_t type_count() const { return static_cast<int64_t>(items_.size()); }
    int64_t max_types() const { return max_types_; }

    bool is_empty() const { return items_.empty(); }
    bool is_full(const ResourceId& key) const;

    // --- Mutate ---

    int64_t insert(const ResourceId& key, int64_t amount) override;
    int64_t extract(const ResourceId& key, int64_t amount) override;

    void clear() { items_.clear(); }

    std::vector<ResourceId> stored_types() const override;

private:
    int64_t byte_capacity_;
    int max_types_;
    int64_t bytes_per_type_;

    std::unordered_map<ResourceId, int64_t, ResourceId::Hash> items_;

    int64_t units_per_byte(const ResourceId& key) const {
        return key.type ? key.type->amount_per_byte() : 1;
    }

    int64_t bytes_for(const ResourceId& key, int64_t count) const;
};

// ============================================================
// ExternalStorage — bridges an external inventory into ME network
// ============================================================
//
// Mirrors AE2's Storage Bus.
// Wraps an external inventory (chest, barrel, machine output, etc.)
// via callbacks, exposing it as an IStorage with the same interface
// as a StorageCell.

class ExternalStorage : public IStorage {
public:
    using CheckCallback  = std::function<int64_t(const ResourceId&)>;
    using ExtractCallback = std::function<int64_t(const ResourceId&, int64_t)>;
    using InsertCallback  = std::function<int64_t(const ResourceId&, int64_t)>;
    using TypesCallback   = std::function<std::vector<ResourceId>()>;

    ExternalStorage(std::string label,
                    int64_t byte_capacity,
                    CheckCallback check,
                    ExtractCallback take,
                    InsertCallback put,
                    TypesCallback types);

    std::string title_key() const override { return label_; }

    int64_t available(const ResourceId& key) const override;
    int64_t insert(const ResourceId& key, int64_t amount) override;
    int64_t extract(const ResourceId& key, int64_t amount) override;
    std::vector<ResourceId> stored_types() const override;

    int64_t total_bytes() const override { return byte_capacity_; }
    int64_t used_bytes() const override { return 0; }

private:
    std::string label_;
    int64_t byte_capacity_;
    CheckCallback check_;
    ExtractCallback extract_;
    InsertCallback insert_;
    TypesCallback types_;
};

} // namespace science_and_theology::gt
