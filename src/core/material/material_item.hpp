#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "common/resource_types.hpp"
#include "item_def.hpp"
#include "material.hpp"
#include "material_form.hpp"

namespace science_and_theology::gt {

// Number of material forms, used for ID computation.
inline constexpr uint32_t kFormCount = static_cast<uint32_t>(MaterialForm::COUNT);

// Material-based item IDs start at this offset (reserve 0 as invalid).
inline constexpr ItemId kMaterialItemBase = 1;

// Maximum possible material-based item ID (based on hard cap, not current count).
inline constexpr ItemId kMaterialItemMax =
    kMaterialItemBase + static_cast<ItemId>(kMaxMaterials) * kFormCount;

// ============================================================
// Dynamic (non-material) item registry
// ============================================================
//
// All legacy non-material items share this historical explicit-ID range.
// Current game content uses stable ResourceContentKey values and receives
// compact IDs only from ResourceRuntimeIndex snapshots.
inline constexpr ItemId kDynamicItemBase = 0x80000000u;
inline constexpr ItemId kDynamicItemMax  = 0xFFFFFFFEu;

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
// Extends ItemDefinition with GT-specific material metadata.
struct MaterialItem : ItemDefinition {
    const Material* material = nullptr;   // source material (null if not registered)
    MaterialForm form = MaterialForm::COUNT;

    // Internal storage for ItemDefinition's string pointers.
    char _name_key_buf[64] = {};
    char _title_key_buf[64] = {};

    MaterialItem() {
        name_key = _name_key_buf;
        title_key = _title_key_buf;
    }

    // Material units this item contains (144 per ingot, etc.).
    int64_t material_amount() const {
        return get_material_amount(form);
    }
};

// Central item registry. Manages item lookup and lifecycle.
//
// Pattern:
//   1. Register materials via MaterialRegistry (from GDScript)
//   2. Call MaterialRegistry::finalize() which triggers this initialize()
//   3. Query items by ID, or look up by material+form combination
//
// Material items use deterministic IDs: f(material_id, form) → ItemId.
// Dynamic (non-material) items use explicit historical IDs in the unified
// [kDynamicItemBase, kDynamicItemMax) range. Current game code does not use
// this registry.
class ItemRegistry {
public:
    ItemRegistry() = default;
    ~ItemRegistry() = default;

    // Initialize the global registry. Called automatically from
    // MaterialRegistry::finalize(). IDEMPOTENT — safe to call multiple times.
    static void initialize();

    // Look up a material-based item.
    static const MaterialItem* get_item(ItemId item_id);
    static const MaterialItem* get_item(const Material* material, MaterialForm form);

    // Look up a material-based item by string key, e.g. "ingot.copper".
    static const MaterialItem* get_item_by_key(const char* key);

    // Returns the ItemId for a material+form combination, or kInvalidItemId
    // if this material does not generate this form.
    static ItemId get_item_id(const Material* material, MaterialForm form);

    // Look up any item by stable content key. Supports material item keys
    // such as "ingot.copper" and dynamic keys such as "gt_hammer" or
    // "my_mod:custom_widget".
    static ItemId get_item_id_by_key(const char* key);

    // Returns the stable content key for any registered item, or "".
    static const char* get_item_key(ItemId item_id);

    // Returns true if a material generates the given form.
    static bool is_valid_combination(const Material* material, MaterialForm form);

    // Returns the total number of registered material items.
    static size_t get_material_item_count();

    // Get the title translation key for any item ID (material or dynamic).
    static const char* get_item_title_key(ItemId item_id);

    // Returns true if this item ID refers to a valid (registered) item.
    static bool is_valid_item(ItemId item_id);

    // ============================================================
    // Historical dynamic item registration (explicit key→id registry)
    // ============================================================
    //
    // The caller must provide the historical ID explicitly. Automatic
    // allocation was removed with the retired persistent incrementer.
    static ItemId register_item_with_id(ItemId item_id,
                                        const char* item_key,
                                        const char* title_key);

    // Returns true if this item ID is in the dynamic (non-material) item range.
    static bool is_dynamic_item(ItemId item_id);

    // 重置整个 ItemRegistry 到初始状态：清空固定数组与所有动态映射、
    // 复位 initialized 标志。之后可重新调用 initialize()。
    static void reset();

private:
    static void register_material_item(const Material* material, MaterialForm form,
                                        ItemId item_id);
};

} // namespace science_and_theology::gt
