#include "material_item.hpp"

#include "ae2/ae2_pattern_cache.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace science_and_theology::gt {

// Fixed-size registry for all material-based items.
// Indexed directly by item_id (minus base offset) for O(1) lookup.
static MaterialItem g_item_registry[kMaterialItemMax - kMaterialItemBase];
static size_t g_registered_item_count = 0;
static bool g_items_initialized = false;

// ============================================================
// Mod item registry (dynamic)
// ============================================================
//
// Mod items use dynamic storage because their count is not known at
// compile time. Lookups by id and by key are O(1) via maps.
namespace {
struct ModItemEntry {
    ItemId id = kInvalidItemId;
    const char* name_key = "";
    const char* title_key = "";
};

std::vector<ModItemEntry> g_mod_items;
std::unordered_map<std::string, ItemId> g_mod_item_key_to_id;
std::unordered_map<ItemId, size_t> g_mod_item_id_to_index;
ItemId g_next_mod_item_id = kModItemBase;
} // namespace

// ============================================================
// Builtin non-material item registry (dynamic)
// ============================================================
//
// Non-material items (tools, machines, food, campfire, etc.) use
// pre-determined IDs in [kNonMaterialItemBase, kNonMaterialItemMax).
// Storage is dynamic; lookups by id and key are O(1) via maps.
namespace {
struct NonMaterialEntry {
    ItemId id = kInvalidItemId;
    const char* name_key = "";
    const char* title_key = "";
};

std::unordered_map<std::string, ItemId> g_non_mat_key_to_id;
std::unordered_map<ItemId, NonMaterialEntry> g_non_mat_id_to_entry;
ItemId g_next_non_material_item_id = kNonMaterialItemBase;
} // namespace

void ItemRegistry::initialize() {
    if (g_items_initialized) return;
    g_items_initialized = true;

    g_registered_item_count = 0;
    size_t total_slots = kMaterialItemMax - kMaterialItemBase;

    // Initialize all slots to invalid.
    for (size_t i = 0; i < total_slots; ++i) {
        g_item_registry[i].id = kInvalidItemId;
        g_item_registry[i].material = nullptr;
        g_item_registry[i].form = MaterialForm::COUNT;
        g_item_registry[i]._name_key_buf[0] = '\0';
        g_item_registry[i]._title_key_buf[0] = '\0';
    }

    // Generate items for each material × form combination.
    // Material IDs are sequential (0, 1, 2, ...). Iterate up to the
    // highest assigned ID and skip gaps (shouldn't exist with auto-assign).
    uint16_t max_id = get_max_material_id();
    for (uint16_t mat_id = 0; mat_id < max_id; ++mat_id) {
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
    entry.id = item_id;
    entry.material = material;
    entry.form = form;

    // Build name_key: "form_name.material_name" (GT5 ore-dict format)
    std::snprintf(entry._name_key_buf, sizeof(entry._name_key_buf), "%s.%s",
                  get_form_name(form), material->name);

    // title_key is the same as name_key for material items
    entry.title_key = entry._name_key_buf;

    ++g_registered_item_count;
}

const MaterialItem* ItemRegistry::get_item(ItemId item_id) {
    if (!g_items_initialized || item_id < kMaterialItemBase ||
        item_id >= kMaterialItemMax) {
        return nullptr;
    }

    size_t idx = static_cast<size_t>(item_id - kMaterialItemBase);
    if (g_item_registry[idx].id == kInvalidItemId) return nullptr;
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
        if (g_item_registry[i].id != kInvalidItemId &&
            std::strcmp(g_item_registry[i].name_key, key) == 0) {
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
    if (key == nullptr || key[0] == '\0') return kInvalidItemId;

    const MaterialItem* item = get_item_by_key(key);
    if (item != nullptr) return item->id;

    // Check non-material items.
    auto it = g_non_mat_key_to_id.find(key);
    if (it != g_non_mat_key_to_id.end()) return it->second;

    // Check mod items.
    auto mit = g_mod_item_key_to_id.find(key);
    if (mit != g_mod_item_key_to_id.end()) return mit->second;

    return kInvalidItemId;
}

const char* ItemRegistry::get_item_key(ItemId item_id) {
    const MaterialItem* item = get_item(item_id);
    if (item != nullptr) return item->name_key;

    // Check non-material items.
    auto it = g_non_mat_id_to_entry.find(item_id);
    if (it != g_non_mat_id_to_entry.end()) return it->second.name_key;

    // Check mod items.
    auto mod_it = g_mod_item_id_to_index.find(item_id);
    if (mod_it != g_mod_item_id_to_index.end()) {
        return g_mod_items[mod_it->second].name_key;
    }
    return "";
}

bool ItemRegistry::is_valid_combination(const Material* material,
                                          MaterialForm form) {
    if (material == nullptr) return false;
    return material->generates_form(form);
}

size_t ItemRegistry::get_material_item_count() {
    return g_registered_item_count;
}

const char* ItemRegistry::get_item_title_key(ItemId item_id) {
    // Check material items first.
    const MaterialItem* item = get_item(item_id);
    if (item != nullptr) return item->title_key;

    // Check non-material items.
    {
        auto it = g_non_mat_id_to_entry.find(item_id);
        if (it != g_non_mat_id_to_entry.end()) return it->second.title_key;
    }

    // Check encoded patterns.
    const char* pattern_key = PatternDataCache::get_pattern_title_key(item_id);
    if (pattern_key != nullptr) return pattern_key;

    // Check mod items.
    auto mod_it = g_mod_item_id_to_index.find(item_id);
    if (mod_it != g_mod_item_id_to_index.end()) {
        return g_mod_items[mod_it->second].title_key;
    }

    return "ui.unknown";
}

bool ItemRegistry::is_valid_item(ItemId item_id) {
    if (item_id == kInvalidItemId) return false;
    // Material items.
    if (item_id >= kMaterialItemBase && item_id < kMaterialItemMax) {
        const MaterialItem* item = get_item(item_id);
        return item != nullptr;
    }
    // Non-material items.
    if (item_id >= kNonMaterialItemBase && item_id < kNonMaterialItemMax) {
        return g_non_mat_id_to_entry.count(item_id) > 0;
    }
    // Encoded patterns.
    if (PatternDataCache::is_encoded_pattern(item_id)) {
        return true;
    }
    // Mod items.
    if (is_mod_item(item_id)) {
        return g_mod_item_id_to_index.count(item_id) > 0;
    }
    return false;
}

ItemId ItemRegistry::register_item(const char* item_key,
                                    const char* title_key) {
    if (item_key == nullptr || item_key[0] == '\0') {
        return kInvalidItemId;
    }
    // Reject duplicates (including mod items).
    if (get_item_id_by_key(item_key) != kInvalidItemId) {
        return kInvalidItemId;
    }
    if (g_next_non_material_item_id >= kNonMaterialItemMax) {
        return kInvalidItemId;
    }

    ItemId id = g_next_non_material_item_id++;
    NonMaterialEntry entry;
    entry.id = id;
    entry.name_key = item_key;
    entry.title_key = title_key != nullptr ? title_key : item_key;
    g_non_mat_key_to_id[item_key] = id;
    g_non_mat_id_to_entry[id] = entry;
    return id;
}

ItemId ItemRegistry::register_mod_item(const char* item_key,
                                         const char* title_key) {
    if (item_key == nullptr || item_key[0] == '\0') {
        return kInvalidItemId;
    }
    // Reject duplicates (including builtin keys).
    if (get_item_id_by_key(item_key) != kInvalidItemId) {
        return kInvalidItemId;
    }
    if (g_next_mod_item_id >= kModItemMax) {
        return kInvalidItemId;
    }

    ItemId id = g_next_mod_item_id++;
    size_t idx = g_mod_items.size();
    g_mod_items.push_back({id, item_key, title_key != nullptr ? title_key : item_key});
    g_mod_item_key_to_id[item_key] = id;
    g_mod_item_id_to_index[id] = idx;
    return id;
}

bool ItemRegistry::is_mod_item(ItemId item_id) {
    return item_id >= kModItemBase && item_id < kModItemMax;
}

} // namespace science_and_theology::gt
