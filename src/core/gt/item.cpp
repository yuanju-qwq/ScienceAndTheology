#include "item.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace science_and_theology::gt {

// Fixed-size registry for all material-based items.
// Indexed directly by item_id (minus base offset) for O(1) lookup.
static MaterialItem g_item_registry[kMaterialItemMax - kMaterialItemBase];
static size_t g_registered_item_count = 0;
static bool g_items_initialized = false;

void ItemRegistry::initialize() {
    if (g_items_initialized) return;
    g_items_initialized = true;

    // Ensure materials are initialized first.
    initialize_materials();

    g_registered_item_count = 0;
    size_t total_slots = kMaterialItemMax - kMaterialItemBase;

    // Initialize all slots to invalid.
    for (size_t i = 0; i < total_slots; ++i) {
        g_item_registry[i].item_id = kInvalidItemId;
        g_item_registry[i].material = nullptr;
        g_item_registry[i].form = MaterialForm::COUNT;
        g_item_registry[i].item_key[0] = '\0';
        g_item_registry[i].display_name[0] = '\0';
    }

    // Generate items for each material × form combination.
    for (uint16_t mat_id = 0; mat_id < materials::COUNT; ++mat_id) {
        const Material* mat = get_material_by_id(mat_id);
        if (mat == nullptr) continue;

        for (uint16_t f = 0; f < static_cast<uint16_t>(MaterialForm::COUNT); ++f) {
            MaterialForm form = static_cast<MaterialForm>(f);

            if (!mat->generates_form(form)) continue;

            ItemId item_id = make_material_item_id(mat_id, form);
            register_material_item(mat, form, item_id);
        }
    }
}

void ItemRegistry::register_material_item(const Material* material,
                                           MaterialForm form, ItemId item_id) {
    size_t idx = static_cast<size_t>(item_id - kMaterialItemBase);
    assert(idx < (kMaterialItemMax - kMaterialItemBase));

    MaterialItem& entry = g_item_registry[idx];
    entry.item_id = item_id;
    entry.material = material;
    entry.form = form;

    // Build item_key: "form_name.material_name" (GT5 ore-dict format)
    std::snprintf(entry.item_key, sizeof(entry.item_key), "%s.%s",
                  get_form_name(form), material->name);

    // Build display_name: "MaterialName FormName"
    std::snprintf(entry.display_name, sizeof(entry.display_name), "%s %s",
                  material->display_name, get_form_display_name(form));

    ++g_registered_item_count;
}

const MaterialItem* ItemRegistry::get_item(ItemId item_id) {
    if (!g_items_initialized || item_id < kMaterialItemBase ||
        item_id >= kMaterialItemMax) {
        return nullptr;
    }

    size_t idx = static_cast<size_t>(item_id - kMaterialItemBase);
    if (g_item_registry[idx].item_id == kInvalidItemId) return nullptr;
    return &g_item_registry[idx];
}

const MaterialItem* ItemRegistry::get_item(const Material* material,
                                             MaterialForm form) {
    if (!g_items_initialized || material == nullptr) return nullptr;
    return get_item(make_material_item_id(material->id, form));
}

const MaterialItem* ItemRegistry::get_item_by_key(const char* key) {
    if (!g_items_initialized || key == nullptr || key[0] == '\0') return nullptr;

    // Key lookups are for debug/serialization, not hot path.
    size_t total = kMaterialItemMax - kMaterialItemBase;
    for (size_t i = 0; i < total; ++i) {
        if (g_item_registry[i].item_id != kInvalidItemId &&
            std::strcmp(g_item_registry[i].item_key, key) == 0) {
            return &g_item_registry[i];
        }
    }
    return nullptr;
}

ItemId ItemRegistry::get_item_id(const Material* material, MaterialForm form) {
    if (!g_items_initialized || material == nullptr) return kInvalidItemId;

    ItemId id = make_material_item_id(material->id, form);
    const MaterialItem* item = get_item(id);
    return (item != nullptr) ? id : kInvalidItemId;
}

ItemId ItemRegistry::get_item_id_by_key(const char* key) {
    const MaterialItem* item = get_item_by_key(key);
    return (item != nullptr) ? item->item_id : kInvalidItemId;
}

bool ItemRegistry::is_valid_combination(const Material* material,
                                          MaterialForm form) {
    if (material == nullptr) return false;
    return material->generates_form(form);
}

size_t ItemRegistry::get_material_item_count() {
    return g_registered_item_count;
}

const char* ItemRegistry::get_item_display_name(ItemId item_id) {
    const MaterialItem* item = get_item(item_id);
    if (item == nullptr) return "Unknown";
    return item->display_name;
}

} // namespace science_and_theology::gt
