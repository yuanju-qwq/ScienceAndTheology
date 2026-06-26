#include "material_item.hpp"

#include "ae2/ae2_pattern_cache.hpp"
#include "common/string_pool.hpp"

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
// Dynamic item registry (unified key→id)
// ============================================================
//
// Single registry for ALL dynamically-registered non-material items
// (builtin compounds, mod items, etc.). Replaces the old mod/builtin
// split. Lookups by id and by key are O(1) via maps.
namespace {
struct DynamicItemEntry {
    ItemId id = kInvalidItemId;
    const char* name_key = "";
    const char* title_key = "";
};

std::vector<DynamicItemEntry> g_dynamic_items;
std::unordered_map<std::string, ItemId> g_dynamic_key_to_id;
std::unordered_map<ItemId, size_t> g_dynamic_id_to_index;
ItemId g_next_dynamic_item_id = kDynamicItemBase;
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

    // Check dynamic items (unified: builtin + mod).
    auto it = g_dynamic_key_to_id.find(key);
    if (it != g_dynamic_key_to_id.end()) return it->second;

    return kInvalidItemId;
}

const char* ItemRegistry::get_item_key(ItemId item_id) {
    const MaterialItem* item = get_item(item_id);
    if (item != nullptr) return item->name_key;

    // Check dynamic items.
    auto it = g_dynamic_id_to_index.find(item_id);
    if (it != g_dynamic_id_to_index.end()) {
        return g_dynamic_items[it->second].name_key;
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

    // Check encoded patterns.
    const char* pattern_key = PatternDataCache::get_pattern_title_key(item_id);
    if (pattern_key != nullptr) return pattern_key;

    // Check dynamic items.
    auto it = g_dynamic_id_to_index.find(item_id);
    if (it != g_dynamic_id_to_index.end()) {
        return g_dynamic_items[it->second].title_key;
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
    // Encoded patterns.
    if (PatternDataCache::is_encoded_pattern(item_id)) {
        return true;
    }
    // Dynamic items.
    if (is_dynamic_item(item_id)) {
        return g_dynamic_id_to_index.count(item_id) > 0;
    }
    return false;
}

ItemId ItemRegistry::register_item(const char* item_key,
                                    const char* title_key) {
    if (item_key == nullptr || item_key[0] == '\0') {
        return kInvalidItemId;
    }
    // 幂等：若 item_key 已注册（任意范围），直接返回已有 ID
    {
        ItemId existing = get_item_id_by_key(item_key);
        if (existing != kInvalidItemId) {
            return existing;
        }
    }
    if (g_next_dynamic_item_id >= kDynamicItemMax) {
        return kInvalidItemId;
    }

    ItemId id = g_next_dynamic_item_id++;
    size_t idx = g_dynamic_items.size();
    const char* title = title_key != nullptr ? title_key : item_key;
    g_dynamic_items.push_back({id, item_key, title});
    g_dynamic_key_to_id[item_key] = id;
    g_dynamic_id_to_index[id] = idx;
    return id;
}

bool ItemRegistry::is_dynamic_item(ItemId item_id) {
    return item_id >= kDynamicItemBase && item_id < kDynamicItemMax;
}

void ItemRegistry::reset() {
    // 清空固定数组（设为 kInvalidItemId）
    size_t total_slots = kMaterialItemMax - kMaterialItemBase;
    for (size_t i = 0; i < total_slots; ++i) {
        g_item_registry[i].id = kInvalidItemId;
        g_item_registry[i].material = nullptr;
        g_item_registry[i].form = MaterialForm::COUNT;
        g_item_registry[i]._name_key_buf[0] = '\0';
        g_item_registry[i]._title_key_buf[0] = '\0';
    }
    // 复位计数与初始化标志（之后可重新调用 initialize()）
    g_registered_item_count = 0;
    g_items_initialized = false;

    // 清空动态 item 相关容器并复位 ID 分配器
    g_dynamic_items.clear();
    g_dynamic_key_to_id.clear();
    g_dynamic_id_to_index.clear();
    g_next_dynamic_item_id = kDynamicItemBase;
}

} // namespace science_and_theology::gt
