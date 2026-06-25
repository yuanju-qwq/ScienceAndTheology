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

// Non-material items (tools, machine blocks, etc.) start here.
inline constexpr ItemId kNonMaterialItemBase = kMaterialItemMax + 1;

// Upper bound of the builtin non-material item range (256 reserved slots).
inline constexpr ItemId kNonMaterialItemMax = kNonMaterialItemBase + 256;

// Encoded patterns use dynamic IDs starting here.
inline constexpr ItemId ENCODED_PATTERN_BASE = kNonMaterialItemMax;

// ============================================================
// Mod-registered items
// ============================================================
//
// Mod items use a dedicated high ID range starting at kModItemBase
// to avoid colliding with builtin material/non-material items and
// encoded patterns. IDs are assigned sequentially on registration.
// This keeps builtin ID ranges stable across mod changes, preserving
// save compatibility when mods are added or removed.
inline constexpr ItemId kModItemBase = 0x80000000u;
inline constexpr ItemId kModItemMax  = 0xFFFFFFFEu;

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
// Non-material items (tools, machines) use sequentially assigned IDs.
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
    // such as "ingot.copper" and non-material keys such as "gt_hammer".
    static ItemId get_item_id_by_key(const char* key);

    // Returns the stable content key for any registered item, or "".
    static const char* get_item_key(ItemId item_id);

    // Returns true if a material generates the given form.
    static bool is_valid_combination(const Material* material, MaterialForm form);

    // Returns the total number of registered material items.
    static size_t get_material_item_count();

    // Get the title translation key for any item ID (material or non-material).
    static const char* get_item_title_key(ItemId item_id);

    // Returns true if this item ID refers to a valid (registered) item.
    static bool is_valid_item(ItemId item_id);

    // ============================================================
    // Non-material item registration (auto-assigned ID)
    // ============================================================
    //
    // Registers a non-material item with an auto-assigned ItemId from
    // the builtin range [kNonMaterialItemBase, kNonMaterialItemMax).
    // The item_key must be globally unique. The caller must keep
    // name_key and title_key strings alive for the lifetime of the
    // process.
    //
    // Returns the ItemId on success, or kInvalidItemId on failure
    // (duplicate key or range full). For content pack items with IDs
    // in the mod range, use register_mod_item() instead.
    static ItemId register_item(const char* item_key,
                                 const char* title_key);

    // ============================================================
    // Mod item registration (mod range, auto-assigned ID)
    // ============================================================
    static ItemId register_mod_item(const char* item_key,
                                     const char* title_key);

    // Returns true if this item ID is in the mod item range.
    static bool is_mod_item(ItemId item_id);

private:
    static void register_material_item(const Material* material, MaterialForm form,
                                        ItemId item_id);
};

} // namespace science_and_theology::gt
