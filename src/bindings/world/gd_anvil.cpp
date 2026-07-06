#include "gd_anvil.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

void GDAnvilManager::_ready() {}

bool GDAnvilManager::place_anvil(const StringName& dim, const Vector3i& cell) {
    AnvilKey k = mk(dim, cell);
    if (anvils_.find(k) != anvils_.end()) return false;
    anvils_[k] = true;
    emit_signal("anvil_placed", dim, cell);
    return true;
}

bool GDAnvilManager::remove_anvil(const StringName& dim, const Vector3i& cell) {
    if (anvils_.erase(mk(dim, cell)) > 0) {
        emit_signal("anvil_removed", dim, cell);
        return true;
    }
    return false;
}

bool GDAnvilManager::has_anvil(const StringName& dim, const Vector3i& cell) const {
    return anvils_.find(mk(dim, cell)) != anvils_.end();
}

Array GDAnvilManager::get_all_anvils() const {
    Array result;
    for (const auto& pair : anvils_) {
        Dictionary d;
        d["dimension"] = String(pair.first.d.c_str());
        d["cell"] = Vector3i(pair.first.x, pair.first.y, pair.first.z);
        result.append(d);
    }
    return result;
}

Dictionary GDAnvilManager::weld(const StringName& dim, const Vector3i& cell) {
    Dictionary result;
    result["ok"] = has_anvil(dim, cell);
    if (bool(result["ok"])) {
        result["ingot_item_id"] = 0;  // resolved by GDScript via item key
        result["count"] = 1;
    }
    return result;
}

void GDAnvilManager::clear() { anvils_.clear(); }

GDAnvilManager::AnvilKey GDAnvilManager::mk(const StringName& d, const Vector3i& c) {
    return {String(d).utf8().get_data(), c.x, c.y, c.z};
}

size_t GDAnvilManager::AnvilKeyHash::operator()(const AnvilKey& k) const {
    size_t h = std::hash<std::string>()(k.d);
    h ^= std::hash<int32_t>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void GDAnvilManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_anvil", "dimension", "cell"), &GDAnvilManager::place_anvil);
    ClassDB::bind_method(D_METHOD("remove_anvil", "dimension", "cell"), &GDAnvilManager::remove_anvil);
    ClassDB::bind_method(D_METHOD("has_anvil", "dimension", "cell"), &GDAnvilManager::has_anvil);
    ClassDB::bind_method(D_METHOD("get_all_anvils"), &GDAnvilManager::get_all_anvils);
    ClassDB::bind_method(D_METHOD("weld", "dimension", "cell"), &GDAnvilManager::weld);
    ClassDB::bind_method(D_METHOD("clear"), &GDAnvilManager::clear);

    ADD_SIGNAL(MethodInfo("anvil_placed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("anvil_removed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
}

} // namespace science_and_theology
