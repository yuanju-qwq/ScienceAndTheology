#include "gd_pit_kiln.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/machine/recipe.hpp"

namespace science_and_theology {

using namespace godot;

namespace {
double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Map unfired item ID to fired item ID + fire time
struct KilnRecipe {
    int64_t output_id;
    double fire_time;
};
const std::unordered_map<int64_t, KilnRecipe> kKilnRecipes = {
    // Keyed by GDScript item IDs — these match ItemDatabase constants
    // unfired_bowl -> fired_bowl
    // unfired_jug -> fired_jug
    // unfired_crucible -> fired_crucible
    // unfired_brick -> refractory_brick
    // Placeholder — actual IDs resolved at runtime via item key.
};
} // namespace

// ============================================================
// GDPitKilnData
// ============================================================

bool GDPitKilnData::is_burning() const {
    return lit_ && fire_progress_ < total_fire_time_;
}

double GDPitKilnData::get_progress_ratio() const {
    if (total_fire_time_ <= 0.0) return 0.0;
    return clamp01(fire_progress_ / total_fire_time_);
}

bool GDPitKilnData::is_ready() const {
    return lit_ && fire_progress_ >= total_fire_time_ && total_fire_time_ > 0.0;
}

Dictionary GDPitKilnData::to_dictionary() const {
    Dictionary d;
    d["input_count"] = input_count_;
    d["input_item_id"] = input_item_id_;
    d["covered"] = covered_;
    d["lit"] = lit_;
    d["fire_progress"] = fire_progress_;
    d["total_fire_time"] = total_fire_time_;
    d["output_item_id"] = output_item_id_;
    d["progress_ratio"] = get_progress_ratio();
    d["is_burning"] = is_burning();
    d["is_ready"] = is_ready();
    return d;
}

void GDPitKilnData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_input_count"), &GDPitKilnData::get_input_count);
    ClassDB::bind_method(D_METHOD("set_input_count", "v"), &GDPitKilnData::set_input_count);
    ClassDB::bind_method(D_METHOD("get_input_item_id"), &GDPitKilnData::get_input_item_id);
    ClassDB::bind_method(D_METHOD("set_input_item_id", "v"), &GDPitKilnData::set_input_item_id);
    ClassDB::bind_method(D_METHOD("get_covered"), &GDPitKilnData::get_covered);
    ClassDB::bind_method(D_METHOD("set_covered", "v"), &GDPitKilnData::set_covered);
    ClassDB::bind_method(D_METHOD("get_lit"), &GDPitKilnData::get_lit);
    ClassDB::bind_method(D_METHOD("set_lit", "v"), &GDPitKilnData::set_lit);
    ClassDB::bind_method(D_METHOD("get_fire_progress"), &GDPitKilnData::get_fire_progress);
    ClassDB::bind_method(D_METHOD("set_fire_progress", "v"), &GDPitKilnData::set_fire_progress);
    ClassDB::bind_method(D_METHOD("get_total_fire_time"), &GDPitKilnData::get_total_fire_time);
    ClassDB::bind_method(D_METHOD("set_total_fire_time", "v"), &GDPitKilnData::set_total_fire_time);
    ClassDB::bind_method(D_METHOD("get_output_item_id"), &GDPitKilnData::get_output_item_id);
    ClassDB::bind_method(D_METHOD("set_output_item_id", "v"), &GDPitKilnData::set_output_item_id);
    ClassDB::bind_method(D_METHOD("is_burning"), &GDPitKilnData::is_burning);
    ClassDB::bind_method(D_METHOD("get_progress_ratio"), &GDPitKilnData::get_progress_ratio);
    ClassDB::bind_method(D_METHOD("is_ready"), &GDPitKilnData::is_ready);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &GDPitKilnData::to_dictionary);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "input_count"), "set_input_count", "get_input_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "input_item_id"), "set_input_item_id", "get_input_item_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "covered"), "set_covered", "get_covered");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lit"), "set_lit", "get_lit");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_progress"), "set_fire_progress", "get_fire_progress");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "total_fire_time"), "set_total_fire_time", "get_total_fire_time");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "output_item_id"), "set_output_item_id", "get_output_item_id");
}

// ============================================================
// GDPitKilnManager
// ============================================================

void GDPitKilnManager::_ready() {}
void GDPitKilnManager::_process(double delta) { tick_all(delta); }

bool GDPitKilnManager::place_kiln(const StringName& dim, const Vector3i& cell) {
    KilnKey k = make_key(dim, cell);
    if (kilns_.find(k) != kilns_.end()) return false;
    Ref<GDPitKilnData> d; d.instantiate();
    kilns_[k] = d;
    emit_signal("kiln_placed", dim, cell);
    return true;
}

bool GDPitKilnManager::remove_kiln(const StringName& dim, const Vector3i& cell) {
    KilnKey k = make_key(dim, cell);
    if (kilns_.erase(k) > 0) { emit_signal("kiln_removed", dim, cell); return true; }
    return false;
}

Ref<GDPitKilnData> GDPitKilnManager::get_kiln(const StringName& dim, const Vector3i& cell) const {
    auto it = kilns_.find(make_key(dim, cell));
    return it != kilns_.end() ? it->second : Ref<GDPitKilnData>();
}

bool GDPitKilnManager::has_kiln(const StringName& dim, const Vector3i& cell) const {
    return kilns_.find(make_key(dim, cell)) != kilns_.end();
}

bool GDPitKilnManager::insert_input(const StringName& dim, const Vector3i& cell, int64_t item_id) {
    auto it = kilns_.find(make_key(dim, cell));
    if (it == kilns_.end() || it->second.is_null()) return false;
    GDPitKilnData* d = it->second.ptr();
    if (d->get_lit()) return false;
    if (d->get_input_count() >= 4) return false;
    if (d->get_input_count() > 0 && d->get_input_item_id() != item_id) return false;
    d->set_input_item_id(item_id);
    d->set_input_count(d->get_input_count() + 1);
    return true;
}

bool GDPitKilnManager::cover(const StringName& dim, const Vector3i& cell) {
    auto it = kilns_.find(make_key(dim, cell));
    if (it == kilns_.end() || it->second.is_null()) return false;
    it->second->set_covered(true);
    return true;
}

bool GDPitKilnManager::light(const StringName& dim, const Vector3i& cell) {
    auto it = kilns_.find(make_key(dim, cell));
    if (it == kilns_.end() || it->second.is_null()) return false;
    GDPitKilnData* d = it->second.ptr();
    if (d->get_lit() || !d->get_covered() || d->get_input_count() <= 0) return false;
    d->set_lit(true);
    return true;
}

Dictionary GDPitKilnManager::collect(const StringName& dim, const Vector3i& cell) {
    Dictionary result;
    result["ok"] = false;
    result["item_id"] = 0;
    result["count"] = 0;

    auto it = kilns_.find(make_key(dim, cell));
    if (it == kilns_.end() || it->second.is_null()) return result;
    GDPitKilnData* d = it->second.ptr();
    if (!d->is_ready()) return result;

    result["ok"] = true;
    result["item_id"] = static_cast<int64_t>(d->get_output_item_id());
    result["count"] = static_cast<int32_t>(d->get_input_count());
    kilns_.erase(it);
    emit_signal("kiln_removed", dim, cell);
    return result;
}

Dictionary GDPitKilnManager::get_snapshot(const StringName& dim, const Vector3i& cell) const {
    Dictionary d;
    auto it = kilns_.find(make_key(dim, cell));
    if (it != kilns_.end() && !it->second.is_null()) d = it->second->to_dictionary();
    d["dimension"] = dim; d["cell"] = cell;
    return d;
}

void GDPitKilnManager::tick_all(double delta) {
    if (delta <= 0.0 || kilns_.empty()) return;
    for (auto& pair : kilns_) {
        if (pair.second.is_null()) continue;
        tick_kiln(pair.first, pair.second.ptr(), delta);
    }
}

void GDPitKilnManager::clear() { kilns_.clear(); dirty_kilns_.clear(); }

// Private
GDPitKilnManager::KilnKey GDPitKilnManager::make_key(const StringName& d, const Vector3i& c) {
    return {String(d).utf8().get_data(), c.x, c.y, c.z};
}

size_t GDPitKilnManager::KilnKeyHash::operator()(const KilnKey& k) const {
    size_t h = std::hash<std::string>()(k.dimension);
    h ^= std::hash<int32_t>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void GDPitKilnManager::mark_dirty(const KilnKey& k) { dirty_kilns_.insert(k); }

bool GDPitKilnManager::tick_kiln(const KilnKey& k, GDPitKilnData* d, double delta) {
    if (d == nullptr || !d->is_burning()) return false;
    d->set_fire_progress(d->get_fire_progress() + delta);
    if (d->get_fire_progress() >= d->get_total_fire_time()) {
        d->set_fire_progress(d->get_total_fire_time());
        // Resolve output: unfired item -> fired item (handled by GDScript signal)
        emit_signal("kiln_ready", String(k.dimension.c_str()), Vector3i(k.x, k.y, k.z));
    }
    return true;
}

void GDPitKilnManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_kiln", "dimension", "cell"), &GDPitKilnManager::place_kiln);
    ClassDB::bind_method(D_METHOD("remove_kiln", "dimension", "cell"), &GDPitKilnManager::remove_kiln);
    ClassDB::bind_method(D_METHOD("get_kiln", "dimension", "cell"), &GDPitKilnManager::get_kiln);
    ClassDB::bind_method(D_METHOD("has_kiln", "dimension", "cell"), &GDPitKilnManager::has_kiln);
    ClassDB::bind_method(D_METHOD("insert_input", "dimension", "cell", "item_id"), &GDPitKilnManager::insert_input);
    ClassDB::bind_method(D_METHOD("cover", "dimension", "cell"), &GDPitKilnManager::cover);
    ClassDB::bind_method(D_METHOD("light", "dimension", "cell"), &GDPitKilnManager::light);
    ClassDB::bind_method(D_METHOD("collect", "dimension", "cell"), &GDPitKilnManager::collect);
    ClassDB::bind_method(D_METHOD("get_snapshot", "dimension", "cell"), &GDPitKilnManager::get_snapshot);
    ClassDB::bind_method(D_METHOD("tick_all", "delta"), &GDPitKilnManager::tick_all);
    ClassDB::bind_method(D_METHOD("clear"), &GDPitKilnManager::clear);

    ADD_SIGNAL(MethodInfo("kiln_placed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("kiln_removed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("kiln_ready", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
}

} // namespace science_and_theology
