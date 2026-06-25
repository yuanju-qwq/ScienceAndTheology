#include "gd_bloomery.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "bindings/world/gd_world_data.h"

namespace science_and_theology {

using namespace godot;

namespace {
constexpr double kMinProcessTemp = 1300.0;
constexpr double kMaxTemp = 1600.0;
constexpr double kHeatRate = 5.0;
constexpr double kCoolRate = 2.0;
constexpr double kBellowsBoost = 200.0;
constexpr double kBellowsDuration = 60.0;
constexpr int32_t kOrePerBloom = 5;
constexpr int32_t kCharcoalPerBloom = 5;
constexpr double kBloomTime = 600.0; // seconds at temp

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

// ============================================================
// GDBloomeryData
// ============================================================

bool GDBloomeryData::is_formed() const {
    return ore_count_ > 0 && charcoal_count_ > 0;
}

double GDBloomeryData::get_progress_ratio() const {
    if (kBloomTime <= 0.0) return 0.0;
    return clamp01(process_progress_ / kBloomTime);
}

bool GDBloomeryData::is_ready() const {
    return process_progress_ >= kBloomTime && iron_bloom_yield_ > 0;
}

Dictionary GDBloomeryData::to_dictionary() const {
    Dictionary d;
    d["ore_count"] = ore_count_;
    d["charcoal_count"] = charcoal_count_;
    d["temperature"] = temperature_;
    d["process_progress"] = process_progress_;
    d["lit"] = lit_;
    d["bellows_boosted"] = bellows_boosted_;
    d["bellows_timer"] = bellows_timer_;
    d["iron_bloom_yield"] = iron_bloom_yield_;
    d["progress_ratio"] = get_progress_ratio();
    d["is_ready"] = is_ready();
    d["temperature_pct"] = clamp01(temperature_ / kMaxTemp);
    return d;
}

void GDBloomeryData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_ore_count"), &GDBloomeryData::get_ore_count);
    ClassDB::bind_method(D_METHOD("set_ore_count", "v"), &GDBloomeryData::set_ore_count);
    ClassDB::bind_method(D_METHOD("get_charcoal_count"), &GDBloomeryData::get_charcoal_count);
    ClassDB::bind_method(D_METHOD("set_charcoal_count", "v"), &GDBloomeryData::set_charcoal_count);
    ClassDB::bind_method(D_METHOD("get_temperature"), &GDBloomeryData::get_temperature);
    ClassDB::bind_method(D_METHOD("set_temperature", "v"), &GDBloomeryData::set_temperature);
    ClassDB::bind_method(D_METHOD("get_process_progress"), &GDBloomeryData::get_process_progress);
    ClassDB::bind_method(D_METHOD("set_process_progress", "v"), &GDBloomeryData::set_process_progress);
    ClassDB::bind_method(D_METHOD("get_lit"), &GDBloomeryData::get_lit);
    ClassDB::bind_method(D_METHOD("set_lit", "v"), &GDBloomeryData::set_lit);
    ClassDB::bind_method(D_METHOD("get_bellows_boosted"), &GDBloomeryData::get_bellows_boosted);
    ClassDB::bind_method(D_METHOD("set_bellows_boosted", "v"), &GDBloomeryData::set_bellows_boosted);
    ClassDB::bind_method(D_METHOD("get_bellows_timer"), &GDBloomeryData::get_bellows_timer);
    ClassDB::bind_method(D_METHOD("set_bellows_timer", "v"), &GDBloomeryData::set_bellows_timer);
    ClassDB::bind_method(D_METHOD("get_iron_bloom_yield"), &GDBloomeryData::get_iron_bloom_yield);
    ClassDB::bind_method(D_METHOD("set_iron_bloom_yield", "v"), &GDBloomeryData::set_iron_bloom_yield);
    ClassDB::bind_method(D_METHOD("get_progress_ratio"), &GDBloomeryData::get_progress_ratio);
    ClassDB::bind_method(D_METHOD("is_ready"), &GDBloomeryData::is_ready);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &GDBloomeryData::to_dictionary);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "ore_count"), "set_ore_count", "get_ore_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "charcoal_count"), "set_charcoal_count", "get_charcoal_count");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature"), "set_temperature", "get_temperature");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "process_progress"), "set_process_progress", "get_process_progress");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lit"), "set_lit", "get_lit");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bellows_boosted"), "set_bellows_boosted", "get_bellows_boosted");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bellows_timer"), "set_bellows_timer", "get_bellows_timer");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "iron_bloom_yield"), "set_iron_bloom_yield", "get_iron_bloom_yield");
}

// ============================================================
// GDBloomeryManager
// ============================================================

void GDBloomeryManager::_ready() {}
void GDBloomeryManager::_process(double delta) { tick_all(delta); }

bool GDBloomeryManager::place_bloomery(const StringName& dim, const Vector3i& cell) {
    BloomKey k = mk(dim, cell);
    if (blooms_.find(k) != blooms_.end()) return false;
    Ref<GDBloomeryData> d; d.instantiate();
    blooms_[k] = d;
    emit_signal("bloomery_placed", dim, cell);
    return true;
}

bool GDBloomeryManager::remove_bloomery(const StringName& dim, const Vector3i& cell) {
    BloomKey k = mk(dim, cell);
    if (blooms_.erase(k) > 0) { emit_signal("bloomery_removed", dim, cell); return true; }
    return false;
}

Ref<GDBloomeryData> GDBloomeryManager::get_bloomery(const StringName& dim, const Vector3i& cell) const {
    auto it = blooms_.find(mk(dim, cell));
    return it != blooms_.end() ? it->second : Ref<GDBloomeryData>();
}

bool GDBloomeryManager::has_bloomery(const StringName& dim, const Vector3i& cell) const {
    return blooms_.find(mk(dim, cell)) != blooms_.end();
}

bool GDBloomeryManager::add_ore(const StringName& dim, const Vector3i& cell) {
    auto it = blooms_.find(mk(dim, cell));
    if (it == blooms_.end() || it->second.is_null()) return false;
    GDBloomeryData* d = it->second.ptr();
    if (d->get_lit()) return false;
    d->set_ore_count(d->get_ore_count() + 1);
    return true;
}

bool GDBloomeryManager::add_charcoal(const StringName& dim, const Vector3i& cell) {
    auto it = blooms_.find(mk(dim, cell));
    if (it == blooms_.end() || it->second.is_null()) return false;
    GDBloomeryData* d = it->second.ptr();
    if (d->get_lit()) return false;
    d->set_charcoal_count(d->get_charcoal_count() + 1);
    return true;
}

bool GDBloomeryManager::light_bloomery(const StringName& dim, const Vector3i& cell) {
    auto it = blooms_.find(mk(dim, cell));
    if (it == blooms_.end() || it->second.is_null()) return false;
    GDBloomeryData* d = it->second.ptr();
    if (d->get_lit()) return false;
    if (!has_valid_structure(dim, cell)) return false;
    if (d->get_ore_count() < kOrePerBloom || d->get_charcoal_count() < kCharcoalPerBloom) return false;
    d->set_lit(true);
    d->set_iron_bloom_yield(d->get_ore_count() / kOrePerBloom);
    return true;
}

bool GDBloomeryManager::use_bellows(const StringName& dim, const Vector3i& cell) {
    auto it = blooms_.find(mk(dim, cell));
    if (it == blooms_.end() || it->second.is_null()) return false;
    GDBloomeryData* d = it->second.ptr();
    if (!d->get_lit()) return false;
    d->set_bellows_boosted(true);
    d->set_bellows_timer(kBellowsDuration);
    return true;
}

Dictionary GDBloomeryManager::break_bloomery(const StringName& dim, const Vector3i& cell) {
    Dictionary result;
    result["ok"] = false;
    result["yield"] = 0;

    auto it = blooms_.find(mk(dim, cell));
    if (it == blooms_.end() || it->second.is_null()) return result;
    GDBloomeryData* d = it->second.ptr();
    if (!d->is_ready()) return result;

    result["ok"] = true;
    result["yield"] = static_cast<int32_t>(d->get_iron_bloom_yield());
    blooms_.erase(it);
    emit_signal("bloomery_removed", dim, cell);
    return result;
}

bool GDBloomeryManager::has_valid_structure(const StringName& dim, const Vector3i& cell) const {
    // Check 3x3x2 bloomery structure around the controller cell.
    // The controller (cell) is at the center-bottom.
    // Valid: all 18 blocks (3x3x2 minus controller center) are MAT_BLOOMERY.
    if (bloomery_material_id_ <= 0) return true; // no material check = always valid (fallback)

    int32_t count = 0;
    for (int32_t dy = 0; dy < 2; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            for (int32_t dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue; // skip controller
                count += count_nearby_material(dim,
                    Vector3i(cell.x + dx, cell.y + dy, cell.z + dz), bloomery_material_id_);
            }
        }
    }
    return count >= 17; // all required cells match
}

Dictionary GDBloomeryManager::get_snapshot(const StringName& dim, const Vector3i& cell) const {
    Dictionary d;
    auto it = blooms_.find(mk(dim, cell));
    if (it != blooms_.end() && !it->second.is_null()) d = it->second->to_dictionary();
    d["dimension"] = dim; d["cell"] = cell;
    return d;
}

void GDBloomeryManager::tick_all(double delta) {
    if (delta <= 0.0 || blooms_.empty()) return;
    for (auto& pair : blooms_) {
        if (pair.second.is_null()) continue;
        tick_bloom(pair.first, pair.second.ptr(), delta);
    }
}

void GDBloomeryManager::clear() { blooms_.clear(); dirty_blooms_.clear(); }

// ============================================================
// Private
// ============================================================

GDBloomeryManager::BloomKey GDBloomeryManager::mk(const StringName& d, const Vector3i& c) {
    return {String(d).utf8().get_data(), c.x, c.y, c.z};
}

size_t GDBloomeryManager::BloomKeyHash::operator()(const BloomKey& k) const {
    size_t h = std::hash<std::string>()(k.d);
    h ^= std::hash<int32_t>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void GDBloomeryManager::mdirty(const BloomKey& k) { dirty_blooms_.insert(k); }

bool GDBloomeryManager::tick_bloom(const BloomKey& k, GDBloomeryData* d, double delta) {
    if (d == nullptr || !d->get_lit()) return false;

    // Temperature model
    if (d->get_charcoal_count() > 0) {
        // Burning: heat up
        double target = kMinProcessTemp;
        if (d->get_bellows_boosted()) target += kBellowsBoost;
        double dt = (d->get_temperature() < target) ? kHeatRate : -kCoolRate;
        d->set_temperature(clamp(d->get_temperature() + dt * delta, 0.0, kMaxTemp));

        // Bellows timer decay
        if (d->get_bellows_boosted()) {
            d->set_bellows_timer(d->get_bellows_timer() - delta);
            if (d->get_bellows_timer() <= 0.0) {
                d->set_bellows_boosted(false);
                d->set_bellows_timer(0.0);
            }
        }
    } else {
        // No fuel: cool down
        d->set_temperature(clamp(d->get_temperature() - kCoolRate * delta, 0.0, kMaxTemp));
    }

    // Process progress (only at sufficient temperature)
    if (d->get_temperature() >= kMinProcessTemp) {
        d->set_process_progress(d->get_process_progress() + delta);
    }

    // Check completion
    if (d->is_ready()) {
        emit_signal("bloomery_ready", String(k.d.c_str()), Vector3i(k.x, k.y, k.z));
    }

    return true;
}

int32_t GDBloomeryManager::count_nearby_material(const StringName& dim, const Vector3i& cell, int32_t mat_id) const {
    // Simplified: uses world data lookup if available
    if (wd_ == nullptr) return 1; // skip check if no world data
    auto* wd = Object::cast_to<GDWorldData>(wd_);
    if (wd == nullptr) return 1;

    const int32_t chunk_size = 32;
    Vector3i chunk(
        static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
        static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
        static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
    Vector3i local(cell.x - chunk.x * chunk_size, cell.y - chunk.y * chunk_size, cell.z - chunk.z * chunk_size);

    Dictionary cell_data = wd->get_terrain_cell(
        String(dim), chunk.x, chunk.y, chunk.z, local.x, local.y, local.z);
    int32_t m = static_cast<int32_t>(static_cast<int64_t>(cell_data.get("material", 0)));
    return (m == mat_id) ? 1 : 0;
}

void GDBloomeryManager::set_bloomery_material_id(int32_t id) {
    bloomery_material_id_ = id;
}

void GDBloomeryManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_bloomery", "dimension", "cell"), &GDBloomeryManager::place_bloomery);
    ClassDB::bind_method(D_METHOD("remove_bloomery", "dimension", "cell"), &GDBloomeryManager::remove_bloomery);
    ClassDB::bind_method(D_METHOD("get_bloomery", "dimension", "cell"), &GDBloomeryManager::get_bloomery);
    ClassDB::bind_method(D_METHOD("has_bloomery", "dimension", "cell"), &GDBloomeryManager::has_bloomery);
    ClassDB::bind_method(D_METHOD("add_ore", "dimension", "cell"), &GDBloomeryManager::add_ore);
    ClassDB::bind_method(D_METHOD("add_charcoal", "dimension", "cell"), &GDBloomeryManager::add_charcoal);
    ClassDB::bind_method(D_METHOD("light_bloomery", "dimension", "cell"), &GDBloomeryManager::light_bloomery);
    ClassDB::bind_method(D_METHOD("use_bellows", "dimension", "cell"), &GDBloomeryManager::use_bellows);
    ClassDB::bind_method(D_METHOD("break_bloomery", "dimension", "cell"), &GDBloomeryManager::break_bloomery);
    ClassDB::bind_method(D_METHOD("has_valid_structure", "dimension", "cell"), &GDBloomeryManager::has_valid_structure);
    ClassDB::bind_method(D_METHOD("get_snapshot", "dimension", "cell"), &GDBloomeryManager::get_snapshot);
    ClassDB::bind_method(D_METHOD("tick_all", "delta"), &GDBloomeryManager::tick_all);
    ClassDB::bind_method(D_METHOD("clear"), &GDBloomeryManager::clear);
    ClassDB::bind_method(D_METHOD("set_bloomery_material_id", "id"), &GDBloomeryManager::set_bloomery_material_id);

    ADD_SIGNAL(MethodInfo("bloomery_placed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("bloomery_removed", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("bloomery_ready", PropertyInfo(Variant::STRING_NAME, "dimension"), PropertyInfo(Variant::VECTOR3I, "cell")));
}

} // namespace science_and_theology
