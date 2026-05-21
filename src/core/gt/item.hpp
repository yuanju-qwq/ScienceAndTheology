#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "material.hpp"
#include "material_form.hpp"

namespace science_and_theology::gt {

// Unique item identifier. Every distinct item in the game gets one.
// For material-based items, the ID is computed from (material_id, form).
// Non-material items (tools, machines, etc.) use IDs above the material range.
using ItemId = uint32_t;
inline constexpr ItemId kInvalidItemId = 0;

// Number of material forms, used for ID computation.
inline constexpr uint32_t kFormCount = static_cast<uint32_t>(MaterialForm::COUNT);

// Material-based item IDs start at this offset (reserve 0 as invalid).
inline constexpr ItemId kMaterialItemBase = 1;

// Maximum possible material-based item ID.
inline constexpr ItemId kMaterialItemMax =
    kMaterialItemBase + static_cast<ItemId>(materials::COUNT) * kFormCount;

// Non-material items (tools, machine blocks, etc.) start here.
inline constexpr ItemId kNonMaterialItemBase = kMaterialItemMax + 1;

// Compute a deterministic item ID from a material and form.
// This mirrors GT5's approach: item ID = base + material_id * form_count + form_id.
inline constexpr ItemId make_material_item_id(uint16_t material_id,
                                               MaterialForm form) {
    return kMaterialItemBase +
           static_cast<ItemId>(material_id) * kFormCount +
           static_cast<ItemId>(form);
}

// Reverse: extract material ID from a material item ID.
inline constexpr uint16_t material_id_from_item(ItemId id) {
    if (id < kMaterialItemBase || id >= kMaterialItemMax) return 0xFFFF;
    return static_cast<uint16_t>((id - kMaterialItemBase) / kFormCount);
}

// Reverse: extract form from a material item ID.
inline constexpr MaterialForm form_from_item(ItemId id) {
    if (id < kMaterialItemBase || id >= kMaterialItemMax) return MaterialForm::COUNT;
    return static_cast<MaterialForm>((id - kMaterialItemBase) % kFormCount);
}

// Identifies a specific material-based item.
// Produced by Material × Form intersection.
struct MaterialItem {
    ItemId item_id;              // computed item ID
    const Material* material;    // source material (null if not registered)
    MaterialForm form;            // physical form
    char item_key[64];           // e.g. "ingot.copper" (GT5 ore-dict key format)
    char display_name[64];       // e.g. "Copper Ingot"

    // Material units this item contains (144 per ingot, etc.).
    int64_t material_amount() const {
        return get_material_amount(form);
    }
};

// Represents a quantity of a specific item type.
// This is the fundamental unit of inventory / recipe I/O.
struct ItemStack {
    ItemId item_id = kInvalidItemId;
    int64_t count = 0;

    ItemStack() = default;
    ItemStack(ItemId id, int64_t c) : item_id(id), count(c) {}

    bool is_valid() const { return item_id != kInvalidItemId && count > 0; }
    bool is_empty() const { return item_id == kInvalidItemId || count <= 0; }

    bool operator==(const ItemStack& other) const {
        return item_id == other.item_id && count == other.count;
    }
    bool operator!=(const ItemStack& other) const {
        return !(*this == other);
    }
};

// Central item registry. Manages item lookup and lifecycle.
//
// Pattern (mirrors GT5's GTOreDictUnificator):
//   1. Initialize materials first (initialize_materials())
//   2. Then initialize items (initialize_items())
//   3. Query items by ID, or look up by material+form combination
//
// Material items use deterministic IDs: f(material_id, form) → ItemId.
// Non-material items (tools, machines) use sequentially assigned IDs.
class ItemRegistry {
public:
    ItemRegistry() = default;
    ~ItemRegistry() = default;

    // Initialize the global registry. Must be called after initialize_materials().
    static void initialize();

    // Look up a material-based item.
    static const MaterialItem* get_item(ItemId item_id);
    static const MaterialItem* get_item(const Material* material, MaterialForm form);

    // Look up a material-based item by string key, e.g. "ingot.copper".
    static const MaterialItem* get_item_by_key(const char* key);

    // Returns the ItemId for a material+form combination, or kInvalidItemId
    // if this material does not generate this form.
    static ItemId get_item_id(const Material* material, MaterialForm form);
    static ItemId get_item_id_by_key(const char* key);

    // Returns true if a material generates the given form.
    static bool is_valid_combination(const Material* material, MaterialForm form);

    // Returns the total number of registered material items.
    static size_t get_material_item_count();

    // Get the localized display name for an item ID.
    static const char* get_item_display_name(ItemId item_id);

private:
    static void register_material_item(const Material* material, MaterialForm form,
                                        ItemId item_id);
};

// Convenience: create an ItemStack from material+form+count.
inline ItemStack make_item_stack(const Material* material, MaterialForm form,
                                  int64_t count) {
    return ItemStack(ItemRegistry::get_item_id(material, form), count);
}

} // namespace science_and_theology::gt
