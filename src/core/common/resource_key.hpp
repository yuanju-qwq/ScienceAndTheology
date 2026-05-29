#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "resource_types.hpp"

namespace science_and_theology::gt {

struct Material;
enum class MaterialForm : uint16_t;

// ============================================================
// ResourceKeyType — type descriptor for resource identity
// ============================================================
//
// Each concrete resource type (item, fluid, gas, etc.) registers
// one instance of a ResourceKeyType subclass. The type descriptor
// owns type-specific metadata: formatting, unit symbol, storage
// efficiency, and factory methods for deserialization.
//
// This mirrors AE2's AEKeyType design.

class ResourceKey;
class ItemKey;
class FluidKey;

class ResourceKeyType {
public:
    virtual ~ResourceKeyType() = default;

    // Unique string identifier (e.g. "item", "fluid").
    virtual const char* id() const = 0;

    // Unit symbol for display, or nullptr if unitless (items).
    virtual const char* unit_symbol() const { return nullptr; }

    // How many of this resource fit in one display unit.
    // 1 for items, 1000 for fluids (mB → B).
    virtual int amount_per_unit() const { return 1; }

    // How many units per byte of digital storage.
    virtual int amount_per_byte() const { return 1; }

    // Format an amount for human-readable display.
    virtual std::string format_amount(int64_t amount) const;

    // Deserialize a key of this type from a generic tag.
    // virtual std::unique_ptr<ResourceKey> load_key(...) const = 0;

    // --- Registry ---

    // Returns all registered types.
    static const std::vector<const ResourceKeyType*>& all_types();

    // Look up by string id.
    static const ResourceKeyType* from_id(const char* id);

    // Convenience accessors.
    static const ResourceKeyType& item_type();
    static const ResourceKeyType& fluid_type();

protected:
    ResourceKeyType();
};

// ============================================================
// ItemKeyType
// ============================================================

class ItemKeyType : public ResourceKeyType {
public:
    static const ItemKeyType& instance();

    const char* id() const override { return "item"; }
    int amount_per_unit() const override { return 1; }
    int amount_per_byte() const override { return 1; }

private:
    ItemKeyType();
};

// ============================================================
// FluidKeyType
// ============================================================

class FluidKeyType : public ResourceKeyType {
public:
    static const FluidKeyType& instance();

    const char* id() const override { return "fluid"; }
    const char* unit_symbol() const override { return "mB"; }
    int amount_per_unit() const override { return 1000; }   // 1000 mB = 1 B
    int amount_per_byte() const override { return 8000; }   // AE2 standard
    std::string format_amount(int64_t amount) const override;

private:
    FluidKeyType();
};

// ============================================================
// ResourceKey — immutable resource identity
// ============================================================
//
// The identity of a stackable resource, without quantity.
// Immutable, hashable, suitable as a map key.
// Two keys are equal iff they are of the same type and have the
// same identity data (e.g. same ItemId or same FluidId).
//
// This mirrors AE2's AEKey design: identity is separated from
// quantity (which lives in ResourceStack).

class ResourceKey {
public:
    virtual ~ResourceKey() = default;

    // The type descriptor for this key.
    virtual const ResourceKeyType& get_type() const = 0;

    // Deep-copy this key.
    virtual std::unique_ptr<ResourceKey> clone() const = 0;

    // Remove secondary attributes (e.g. NBT for items) for fuzzy matching.
    virtual std::unique_ptr<ResourceKey> drop_secondary() const;

    // Equality: same type + same identity.
    virtual bool equals(const ResourceKey& other) const = 0;
    virtual size_t hash() const = 0;

    // Convenience type checks.
    bool is_item() const;
    bool is_fluid() const;

    // Downcast helpers.
    const ItemKey* as_item() const;
    const FluidKey* as_fluid() const;
};

// ============================================================
// ItemKey — identifies an item by its ItemId + secondary data
// ============================================================
//
// Secondary data (e.g. durability, enchantment type for magic books,
// spell affinity, etc.) is stored as an int32_t.
// -1 means "no secondary data" / default / full durability.
// The equals() method compares both item_id and secondary_id.
// Use drop_secondary() to ignore secondary attributes for fuzzy matching
// (e.g. recipe matching, crafting pattern lookup).

class ItemKey : public ResourceKey {
public:
    explicit ItemKey(ItemId item_id, int32_t secondary_id = -1)
        : item_id_(item_id), secondary_id_(secondary_id) {
        size_t h = static_cast<size_t>(item_id_) ^ 0xA5A5A5A5;
        h ^= static_cast<size_t>(static_cast<int32_t>(secondary_id_)) * 0x9e3779b9;
        hash_ = h;
    }

    const ResourceKeyType& get_type() const override {
        return ItemKeyType::instance();
    }

    std::unique_ptr<ResourceKey> clone() const override {
        return std::make_unique<ItemKey>(item_id_, secondary_id_);
    }

    std::unique_ptr<ResourceKey> drop_secondary() const override {
        return std::make_unique<ItemKey>(item_id_, -1);
    }

    bool equals(const ResourceKey& other) const override;
    size_t hash() const override { return hash_; }

    ItemId item_id() const { return item_id_; }
    int32_t secondary_id() const { return secondary_id_; }

private:
    ItemId item_id_;
    int32_t secondary_id_;
    size_t hash_;
};

// ============================================================
// FluidKey — identifies a fluid by its FluidId
// ============================================================

class FluidKey : public ResourceKey {
public:
    explicit FluidKey(FluidId fluid_id) : fluid_id_(fluid_id) {
        hash_ = static_cast<size_t>(fluid_id_) ^ 0xF1F1F1F1;
    }

    const ResourceKeyType& get_type() const override {
        return FluidKeyType::instance();
    }

    std::unique_ptr<ResourceKey> clone() const override {
        return std::make_unique<FluidKey>(fluid_id_);
    }

    std::unique_ptr<ResourceKey> drop_secondary() const override {
        return std::make_unique<FluidKey>(fluid_id_);
    }

    bool equals(const ResourceKey& other) const override;
    size_t hash() const override { return hash_; }

    FluidId fluid_id() const { return fluid_id_; }

private:
    FluidId fluid_id_;
    size_t hash_;
};

// ============================================================
// ResourceStack — an immutable resource key + quantity
// ============================================================
//
// The universal stack type. Replaces both ItemStack and FluidStack.
// Key is shared (std::shared_ptr) for cheap copy semantics.
//
// Usage:
//   ResourceStack stack = ResourceStack::item(iron_ingot_id, 64);
//   ResourceStack fluid = ResourceStack::fluid(water_id, 8000);
//
// This mirrors AE2's GenericStack(what: AEKey, amount: long) design.

struct ResourceStack {
    std::shared_ptr<const ResourceKey> what = nullptr;
    int64_t amount = 0;

    ResourceStack() = default;

    // Construct from an existing key.
    ResourceStack(std::shared_ptr<const ResourceKey> key, int64_t amt)
        : what(std::move(key)), amount(amt) {}

    // Factory: create an item stack.
    static ResourceStack item(ItemId item_id, int64_t count,
                              int32_t secondary_id = -1) {
        return {std::make_shared<ItemKey>(item_id, secondary_id), count};
    }

    // Factory: create a fluid stack.
    static ResourceStack fluid(FluidId fluid_id, int64_t mb) {
        return {std::make_shared<FluidKey>(fluid_id), mb};
    }

    // Convenience: create from material form + count.
    static ResourceStack material_item(const Material* material,
                                        MaterialForm form, int64_t count);

    // --- Validity ---

    bool is_valid() const {
        return what != nullptr && amount > 0;
    }
    bool is_empty() const {
        return what == nullptr || amount <= 0;
    }

    // --- Type checks ---

    bool is_item() const { return what != nullptr && what->is_item(); }
    bool is_fluid() const { return what != nullptr && what->is_fluid(); }

    // --- Accessors ---

    const ResourceKeyType& get_type() const { return what->get_type(); }

    ItemId item_id() const {
        auto* ik = what ? what->as_item() : nullptr;
        return ik ? ik->item_id() : kInvalidItemId;
    }
    FluidId fluid_id() const {
        auto* fk = what ? what->as_fluid() : nullptr;
        return fk ? fk->fluid_id() : kInvalidFluidId;
    }

    // --- Equality ---

    bool operator==(const ResourceStack& other) const {
        if (amount != other.amount) return false;
        if (what == other.what) return true;
        if (what == nullptr || other.what == nullptr) return false;
        return what->equals(*other.what);
    }
    bool operator!=(const ResourceStack& other) const {
        return !(*this == other);
    }

    // --- Hash for containers ---

    struct Hash {
        size_t operator()(const ResourceStack& s) const {
            size_t h = s.what ? s.what->hash() : 0;
            h ^= static_cast<size_t>(s.amount) + 0x9e3779b9 +
                 (h << 6) + (h >> 2);
            return h;
        }
    };

    // --- Key-only comparison for map lookups ---

    // Returns true if keys match (ignoring amount).
    bool has_same_key(const ResourceStack& other) const {
        if (what == other.what) return true;
        if (what == nullptr || other.what == nullptr) return false;
        return what->equals(*other.what);
    }
};

// ============================================================
// ResourceKeyList — a list of ResourceStacks with lookup helpers
// ============================================================

// Used for recipe input matching and inventory queries.
// Aggregates amounts by key for efficient "contains_enough" checks.

class ResourceKeyList {
public:
    ResourceKeyList() = default;

    // Build from a vector of ResourceStacks.
    static ResourceKeyList from_stacks(
            const std::vector<ResourceStack>& stacks);

    // Add a stack to the list (aggregates by key).
    void add(const ResourceStack& stack);

    // Check if the list contains at least `required` of the given key.
    bool contains_enough(const ResourceStack& required) const;

    // Consume `amount` of the given key. Returns true if enough was available.
    bool consume(const ResourceStack& stack);

    // Consume all inputs from a vector (recipe consumption).
    bool consume_all(const std::vector<ResourceStack>& inputs);

    // Returns total count across all entries.
    size_t entry_count() const { return entries_.size(); }

    // Iterate all entries.
    const std::vector<ResourceStack>& entries() const { return entries_; }

    void clear() { entries_.clear(); }

private:
    std::vector<ResourceStack> entries_;
};

} // namespace science_and_theology::gt