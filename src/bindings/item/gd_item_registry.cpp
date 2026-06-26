#include "gd_item_registry.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/common/string_pool.hpp"
#include "core/material/material_item.hpp"

namespace science_and_theology {

using namespace godot;

int64_t GDItemRegistry::register_item(const Dictionary& def) {
    String key = def.get("item_key", "");
    if (key.is_empty()) {
        return 0;
    }

    String title = def.get("title_key", "");
    const char* title_ptr = title.is_empty() ? nullptr : gt::intern_string(title.utf8().get_data());

    gt::ItemId id = gt::ItemRegistry::register_item(gt::intern_string(key.utf8().get_data()), title_ptr);
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

bool GDItemRegistry::is_dynamic_item(int64_t id) {
    return gt::ItemRegistry::is_dynamic_item(static_cast<gt::ItemId>(id));
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
        D_METHOD("is_dynamic_item", "id"),
        &GDItemRegistry::is_dynamic_item);
}

} // namespace science_and_theology
