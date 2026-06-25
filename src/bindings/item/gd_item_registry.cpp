#include "gd_item_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <unordered_set>

#include "core/material/material_item.hpp"

namespace science_and_theology {

using namespace godot;

namespace {
// Persistent string storage for item keys passed from GDScript.
// ItemRegistry stores const char* pointers that must outlive the
// registry, so we keep the strings here for the lifetime of the
// process.
std::unordered_set<std::string> g_string_pool;

const char* intern_string(const std::string& s) {
    auto it = g_string_pool.find(s);
    if (it != g_string_pool.end()) {
        return it->c_str();
    }
    auto result = g_string_pool.insert(s);
    return result.first->c_str();
}
} // namespace

int64_t GDItemRegistry::register_item(const Dictionary& def) {
    String key = def.get("item_key", "");
    if (key.is_empty()) {
        return 0;
    }

    std::string key_str = key.utf8().get_data();
    String title = def.get("title_key", "");
    const char* title_ptr = title.is_empty()
        ? nullptr
        : intern_string(std::string(title.utf8().get_data()));

    const char* key_ptr = intern_string(key_str);
    gt::ItemId id = gt::ItemRegistry::register_mod_item(key_ptr, title_ptr);
    return static_cast<int64_t>(id);
}

int64_t GDItemRegistry::get_item_id(const String& key) {
    gt::ItemId id = gt::ItemRegistry::get_item_id_by_key(key.utf8().get_data());
    return static_cast<int64_t>(id);
}

String GDItemRegistry::get_item_key(int64_t id) {
    const char* key = gt::ItemRegistry::get_item_key(
        static_cast<gt::ItemId>(id));
    return String(key);
}

String GDItemRegistry::get_item_title_key(int64_t id) {
    const char* key = gt::ItemRegistry::get_item_title_key(
        static_cast<gt::ItemId>(id));
    return String(key);
}

bool GDItemRegistry::is_valid_item(int64_t id) {
    return gt::ItemRegistry::is_valid_item(static_cast<gt::ItemId>(id));
}

bool GDItemRegistry::is_mod_item(int64_t id) {
    return gt::ItemRegistry::is_mod_item(static_cast<gt::ItemId>(id));
}

void GDItemRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("register_item", "def"),
        &GDItemRegistry::register_item);
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("get_item_id", "key"),
        &GDItemRegistry::get_item_id);
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("get_item_key", "id"),
        &GDItemRegistry::get_item_key);
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("get_item_title_key", "id"),
        &GDItemRegistry::get_item_title_key);
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("is_valid_item", "id"),
        &GDItemRegistry::is_valid_item);
    ClassDB::bind_static_method("GDItemRegistry",
        D_METHOD("is_mod_item", "id"),
        &GDItemRegistry::is_mod_item);
}

} // namespace science_and_theology
