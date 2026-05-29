#include "resource_key.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "material/material_item.hpp"

namespace science_and_theology::gt {

// ============================================================
// ResourceKeyType — registry
// ============================================================

namespace {
std::vector<const ResourceKeyType*> g_key_types;
} // namespace

ResourceKeyType::ResourceKeyType() {
    g_key_types.push_back(this);
}

const std::vector<const ResourceKeyType*>& ResourceKeyType::all_types() {
    return g_key_types;
}

const ResourceKeyType* ResourceKeyType::from_id(const char* id) {
    for (const auto* t : g_key_types) {
        if (std::strcmp(t->id(), id) == 0) {
            return t;
        }
    }
    return nullptr;
}

const ResourceKeyType& ResourceKeyType::item_type() {
    return ItemKeyType::instance();
}

const ResourceKeyType& ResourceKeyType::fluid_type() {
    return FluidKeyType::instance();
}

std::string ResourceKeyType::format_amount(int64_t amount) const {
    return std::to_string(amount);
}

// ============================================================
// ItemKeyType
// ============================================================

ItemKeyType::ItemKeyType() = default;

const ItemKeyType& ItemKeyType::instance() {
    static ItemKeyType inst;
    return inst;
}

// ============================================================
// FluidKeyType
// ============================================================

FluidKeyType::FluidKeyType() = default;

const FluidKeyType& FluidKeyType::instance() {
    static FluidKeyType inst;
    return inst;
}

std::string FluidKeyType::format_amount(int64_t amount) const {
    if (amount >= 1000) {
        int64_t buckets = amount / 1000;
        int64_t mb = amount % 1000;
        if (mb == 0) {
            return std::to_string(buckets) + " B";
        }
        // Format as X.Y B
        std::string result = std::to_string(buckets) + ".";
        result += std::to_string(mb / 100);
        result += " B";
        return result;
    }
    return std::to_string(amount) + " mB";
}

// ============================================================
// ResourceKey — convenience methods
// ============================================================

std::unique_ptr<ResourceKey> ResourceKey::drop_secondary() const {
    return clone();
}

bool ResourceKey::is_item() const {
    return &get_type() == &ItemKeyType::instance();
}

bool ResourceKey::is_fluid() const {
    return &get_type() == &FluidKeyType::instance();
}

const ItemKey* ResourceKey::as_item() const {
    return is_item() ? static_cast<const ItemKey*>(this) : nullptr;
}

const FluidKey* ResourceKey::as_fluid() const {
    return is_fluid() ? static_cast<const FluidKey*>(this) : nullptr;
}

// ============================================================
// ItemKey — equality
// ============================================================

bool ItemKey::equals(const ResourceKey& other) const {
    auto* ok = other.as_item();
    if (ok == nullptr) return false;
    return item_id_ == ok->item_id_ && secondary_id_ == ok->secondary_id_;
}

// ============================================================
// FluidKey — equality
// ============================================================

bool FluidKey::equals(const ResourceKey& other) const {
    auto* ok = other.as_fluid();
    if (ok == nullptr) return false;
    return fluid_id_ == ok->fluid_id_;
}

// ============================================================
// ResourceStack — material_item helper
// ============================================================

ResourceStack ResourceStack::material_item(const Material* material,
                                            MaterialForm form,
                                            int64_t count) {
    ItemId id = ItemRegistry::get_item_id(material, form);
    return item(id, count);
}

// ============================================================
// ResourceKeyList
// ============================================================

ResourceKeyList ResourceKeyList::from_stacks(
        const std::vector<ResourceStack>& stacks) {
    ResourceKeyList list;
    for (const auto& stack : stacks) {
        if (stack.is_valid()) {
            list.add(stack);
        }
    }
    return list;
}

void ResourceKeyList::add(const ResourceStack& stack) {
    if (!stack.is_valid()) return;

    // Merge with existing entry if key matches.
    for (auto& entry : entries_) {
        if (entry.has_same_key(stack)) {
            entry.amount += stack.amount;
            return;
        }
    }

    // New key: make a copy to own the key.
    entries_.push_back(ResourceStack(stack.what, stack.amount));
}

bool ResourceKeyList::contains_enough(const ResourceStack& required) const {
    if (!required.is_valid()) return false;

    int64_t total = 0;
    for (const auto& entry : entries_) {
        if (entry.is_valid() && entry.has_same_key(required)) {
            total += entry.amount;
        }
    }
    return total >= required.amount;
}

bool ResourceKeyList::consume(const ResourceStack& stack) {
    if (!stack.is_valid()) return false;

    int64_t remaining = stack.amount;
    for (auto& entry : entries_) {
        if (!entry.is_valid() || !entry.has_same_key(stack)) continue;

        int64_t take = std::min(remaining, entry.amount);
        entry.amount -= take;
        remaining -= take;

        if (entry.amount <= 0) {
            entry = ResourceStack{};
        }

        if (remaining <= 0) break;
    }

    return remaining <= 0;
}

bool ResourceKeyList::consume_all(
        const std::vector<ResourceStack>& inputs) {
    for (const auto& input : inputs) {
        if (!consume(input)) {
            return false;
        }
    }
    return true;
}

} // namespace science_and_theology::gt