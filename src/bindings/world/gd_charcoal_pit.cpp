#include "gd_charcoal_pit.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/simulation/tick_system.hpp"

namespace science_and_theology {

using namespace godot;

namespace {

constexpr int32_t kMaxLogs = 16;
constexpr double kBaseBurnTimePerLog = 120.0;  // seconds per log
constexpr int32_t kCharcoalPerLog = 1;
constexpr double kLogBurnSeconds = kBaseBurnTimePerLog * kMaxLogs;
const char* kCoverMaterialKeys[] = {"snt:dirt", "snt:sand", "snt:straw", "snt:snow", "snt:clay", nullptr};

double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

} // namespace

// ============================================================
// GDCharcoalPitData
// ============================================================

bool GDCharcoalPitData::is_burning() const {
    return lit_ && burn_progress_ < total_burn_time_;
}

double GDCharcoalPitData::get_progress_ratio() const {
    if (total_burn_time_ <= 0.0) return 0.0;
    return clamp01(burn_progress_ / total_burn_time_);
}

bool GDCharcoalPitData::is_ready() const {
    return lit_ && burn_progress_ >= total_burn_time_ && total_burn_time_ > 0.0;
}

Dictionary GDCharcoalPitData::to_dictionary() const {
    Dictionary d;
    d["log_count"] = log_count_;
    d["covered"] = covered_;
    d["lit"] = lit_;
    d["burn_progress"] = burn_progress_;
    d["total_burn_time"] = total_burn_time_;
    d["charcoal_yield"] = charcoal_yield_;
    d["progress_ratio"] = get_progress_ratio();
    d["is_burning"] = is_burning();
    d["is_ready"] = is_ready();
    return d;
}

void GDCharcoalPitData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_log_count"), &GDCharcoalPitData::get_log_count);
    ClassDB::bind_method(D_METHOD("set_log_count", "value"), &GDCharcoalPitData::set_log_count);
    ClassDB::bind_method(D_METHOD("get_covered"), &GDCharcoalPitData::get_covered);
    ClassDB::bind_method(D_METHOD("set_covered", "value"), &GDCharcoalPitData::set_covered);
    ClassDB::bind_method(D_METHOD("get_lit"), &GDCharcoalPitData::get_lit);
    ClassDB::bind_method(D_METHOD("set_lit", "value"), &GDCharcoalPitData::set_lit);
    ClassDB::bind_method(D_METHOD("get_burn_progress"), &GDCharcoalPitData::get_burn_progress);
    ClassDB::bind_method(D_METHOD("set_burn_progress", "value"), &GDCharcoalPitData::set_burn_progress);
    ClassDB::bind_method(D_METHOD("get_total_burn_time"), &GDCharcoalPitData::get_total_burn_time);
    ClassDB::bind_method(D_METHOD("set_total_burn_time", "value"), &GDCharcoalPitData::set_total_burn_time);
    ClassDB::bind_method(D_METHOD("get_charcoal_yield"), &GDCharcoalPitData::get_charcoal_yield);
    ClassDB::bind_method(D_METHOD("set_charcoal_yield", "value"), &GDCharcoalPitData::set_charcoal_yield);
    ClassDB::bind_method(D_METHOD("is_burning"), &GDCharcoalPitData::is_burning);
    ClassDB::bind_method(D_METHOD("get_progress_ratio"), &GDCharcoalPitData::get_progress_ratio);
    ClassDB::bind_method(D_METHOD("is_ready"), &GDCharcoalPitData::is_ready);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &GDCharcoalPitData::to_dictionary);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "log_count"), "set_log_count", "get_log_count");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "covered"), "set_covered", "get_covered");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lit"), "set_lit", "get_lit");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burn_progress"), "set_burn_progress", "get_burn_progress");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "total_burn_time"), "set_total_burn_time", "get_total_burn_time");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "charcoal_yield"), "set_charcoal_yield", "get_charcoal_yield");
}

// ============================================================
// GDCharcoalPitManager
// ============================================================

void GDCharcoalPitManager::_ready() {
}

void GDCharcoalPitManager::_process(double delta) {
    tick_all(delta);
}

bool GDCharcoalPitManager::place_pit(const StringName& dimension, const Vector3i& cell) {
    const PitKey key = make_key(dimension, cell);
    if (pits_.find(key) != pits_.end()) return false;

    Ref<GDCharcoalPitData> data;
    data.instantiate();
    pits_[key] = data;
    mark_dirty(key);
    emit_signal("pit_placed", dimension, cell);
    return true;
}

bool GDCharcoalPitManager::remove_pit(const StringName& dimension, const Vector3i& cell) {
    const PitKey key = make_key(dimension, cell);
    if (pits_.erase(key) > 0) {
        mark_dirty(key);
        emit_signal("pit_removed", dimension, cell);
        return true;
    }
    return false;
}

Ref<GDCharcoalPitData> GDCharcoalPitManager::get_pit(
        const StringName& dimension, const Vector3i& cell) const {
    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    return it != pits_.end() ? it->second : Ref<GDCharcoalPitData>();
}

bool GDCharcoalPitManager::has_pit(const StringName& dimension, const Vector3i& cell) const {
    return pits_.find(make_key(dimension, cell)) != pits_.end();
}

bool GDCharcoalPitManager::add_log(const StringName& dimension, const Vector3i& cell) {
    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    if (it == pits_.end() || it->second.is_null()) return false;

    GDCharcoalPitData* data = it->second.ptr();
    if (data->get_lit()) return false;  // already burning
    if (data->get_log_count() >= kMaxLogs) return false;

    data->set_log_count(data->get_log_count() + 1);
    data->set_total_burn_time(data->get_log_count() * kBaseBurnTimePerLog);
    mark_dirty(key);
    return true;
}

bool GDCharcoalPitManager::cover(const StringName& dimension, const Vector3i& cell) {
    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    if (it == pits_.end() || it->second.is_null()) return false;

    it->second->set_covered(true);
    mark_dirty(key);
    return true;
}

bool GDCharcoalPitManager::light(const StringName& dimension, const Vector3i& cell) {
    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    if (it == pits_.end() || it->second.is_null()) return false;

    GDCharcoalPitData* data = it->second.ptr();
    if (data->get_lit()) return false;
    if (!data->get_covered()) return false;
    if (data->get_log_count() <= 0) return false;

    data->set_lit(true);
    data->set_total_burn_time(data->get_log_count() * kBaseBurnTimePerLog);
    data->set_charcoal_yield(data->get_log_count() * kCharcoalPerLog);
    mark_dirty(key);
    return true;
}

Dictionary GDCharcoalPitManager::collect(const StringName& dimension, const Vector3i& cell) {
    Dictionary result;
    result["ok"] = false;
    result["count"] = 0;

    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    if (it == pits_.end() || it->second.is_null()) return result;

    GDCharcoalPitData* data = it->second.ptr();
    if (!data->is_ready()) return result;

    result["ok"] = true;
    result["count"] = data->get_charcoal_yield();
    pits_.erase(it);
    emit_signal("pit_removed", dimension, cell);
    return result;
}

Dictionary GDCharcoalPitManager::get_snapshot(
        const StringName& dimension, const Vector3i& cell) const {
    Dictionary d;
    const PitKey key = make_key(dimension, cell);
    auto it = pits_.find(key);
    if (it != pits_.end() && !it->second.is_null()) {
        d = it->second->to_dictionary();
    }
    d["dimension"] = dimension;
    d["cell"] = cell;
    return d;
}

void GDCharcoalPitManager::tick_all(double delta) {
    if (delta <= 0.0 || pits_.empty()) return;

    for (auto& pair : pits_) {
        if (pair.second.is_null()) continue;
        tick_pit(pair.first, pair.second.ptr(), delta);
    }
}

void GDCharcoalPitManager::clear() {
    pits_.clear();
    dirty_pits_.clear();
}

// ============================================================
// Private helpers
// ============================================================

GDCharcoalPitManager::PitKey GDCharcoalPitManager::make_key(
        const StringName& dimension, const Vector3i& cell) {
    return {String(dimension).utf8().get_data(), cell.x, cell.y, cell.z};
}

String GDCharcoalPitManager::key_to_log_text(const PitKey& key) {
    return String(key.dimension.c_str()) + "@" +
        String::num_int64(key.x) + "," +
        String::num_int64(key.y) + "," +
        String::num_int64(key.z);
}

size_t GDCharcoalPitManager::PitKeyHash::operator()(const PitKey& key) const {
    size_t h = std::hash<std::string>()(key.dimension);
    h ^= std::hash<int32_t>()(key.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(key.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(key.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void GDCharcoalPitManager::mark_dirty(const PitKey& key) {
    dirty_pits_.insert(key);
}

bool GDCharcoalPitManager::tick_pit(const PitKey& key, GDCharcoalPitData* data, double delta) {
    if (data == nullptr || !data->is_burning()) return false;

    data->set_burn_progress(data->get_burn_progress() + delta);

    if (data->get_burn_progress() >= data->get_total_burn_time()) {
        data->set_burn_progress(data->get_total_burn_time());
        emit_signal("pit_ready", String(key.dimension.c_str()),
                    Vector3i(key.x, key.y, key.z));
    }

    mark_dirty(key);
    return true;
}

void GDCharcoalPitManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_pit", "dimension", "cell"), &GDCharcoalPitManager::place_pit);
    ClassDB::bind_method(D_METHOD("remove_pit", "dimension", "cell"), &GDCharcoalPitManager::remove_pit);
    ClassDB::bind_method(D_METHOD("get_pit", "dimension", "cell"), &GDCharcoalPitManager::get_pit);
    ClassDB::bind_method(D_METHOD("has_pit", "dimension", "cell"), &GDCharcoalPitManager::has_pit);
    ClassDB::bind_method(D_METHOD("add_log", "dimension", "cell"), &GDCharcoalPitManager::add_log);
    ClassDB::bind_method(D_METHOD("cover", "dimension", "cell"), &GDCharcoalPitManager::cover);
    ClassDB::bind_method(D_METHOD("light", "dimension", "cell"), &GDCharcoalPitManager::light);
    ClassDB::bind_method(D_METHOD("collect", "dimension", "cell"), &GDCharcoalPitManager::collect);
    ClassDB::bind_method(D_METHOD("get_snapshot", "dimension", "cell"), &GDCharcoalPitManager::get_snapshot);
    ClassDB::bind_method(D_METHOD("tick_all", "delta"), &GDCharcoalPitManager::tick_all);
    ClassDB::bind_method(D_METHOD("clear"), &GDCharcoalPitManager::clear);

    ADD_SIGNAL(MethodInfo("pit_placed",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("pit_removed",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("pit_ready",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
}

} // namespace science_and_theology
