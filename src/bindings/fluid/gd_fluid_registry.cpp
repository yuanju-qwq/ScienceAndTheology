#include "gd_fluid_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <unordered_set>

#include "core/fluid/fluid_registry.hpp"
#include "core/fluid/fluid_def.hpp"

namespace science_and_theology {

using namespace godot;

namespace {
// Persistent string storage for fluid names/keys passed from GDScript.
// FluidDefinition stores const char* pointers that must outlive the
// registry, so we keep the strings here for the lifetime of the process.
std::unordered_set<std::string> g_string_pool;

const char* intern_string(const std::string& s) {
    auto it = g_string_pool.find(s);
    if (it != g_string_pool.end()) {
        return it->c_str();
    }
    auto result = g_string_pool.insert(s);
    return result.first->c_str();
}

const char* intern_variant(const Variant& v, const char* fallback) {
    if (v.get_type() == Variant::NIL) return fallback;
    std::string s = String(v).utf8().get_data();
    if (s.empty()) return fallback;
    return intern_string(s);
}
} // namespace

int64_t GDFluidRegistry::register_fluid(const Dictionary& def) {
    String name = def.get("name", "");
    if (name.is_empty()) {
        return -1;
    }

    gt::FluidDefinition fluid_def;
    fluid_def.name = intern_string(std::string(name.utf8().get_data()));
    fluid_def.title_key = intern_variant(
        def.get("title_key", ""),
        fluid_def.name);
    fluid_def.chemical_formula = intern_variant(
        def.get("chemical_formula", ""),
        "");
    fluid_def.temperature = static_cast<int64_t>(def.get("temperature", 300));
    fluid_def.is_gas = static_cast<bool>(def.get("is_gas", false));

    // 支持显式确定性 ID（P1: 热重载后 ID 不漂移）
    gt::FluidId explicit_id = gt::kInvalidFluidId;
    Variant id_var = def.get("id", Variant());
    if (id_var.get_type() == Variant::INT) {
        explicit_id = static_cast<gt::FluidId>(static_cast<int64_t>(id_var));
    }

    gt::FluidId id = gt::FluidRegistry::register_fluid(fluid_def, explicit_id);
    return static_cast<int64_t>(id);
}

int64_t GDFluidRegistry::get_fluid_id(const String& name) {
    gt::FluidId id = gt::FluidRegistry::get_fluid_id(name.utf8().get_data());
    return (id == gt::kInvalidFluidId) ? -1 : static_cast<int64_t>(id);
}

Dictionary GDFluidRegistry::get_fluid(int64_t id) {
    Dictionary out;
    const gt::FluidDefinition* def = gt::FluidRegistry::get_fluid(
        static_cast<gt::FluidId>(id));
    if (def == nullptr) {
        return out;
    }
    out["name"] = String(def->name);
    out["title_key"] = String(def->title_key);
    out["chemical_formula"] = String(def->chemical_formula);
    out["temperature"] = static_cast<int64_t>(def->temperature);
    out["is_gas"] = def->is_gas;
    return out;
}

int64_t GDFluidRegistry::get_fluid_count() {
    return static_cast<int64_t>(gt::FluidRegistry::get_fluid_count());
}

bool GDFluidRegistry::link_phase_transition(
        const String& from_name, const String& to_name,
        int64_t evaporation_temp, int64_t condensation_temp) {
    gt::FluidId from_id = gt::FluidRegistry::get_fluid_id(from_name.utf8().get_data());
    gt::FluidId to_id = gt::FluidRegistry::get_fluid_id(to_name.utf8().get_data());
    if (from_id == gt::kInvalidFluidId || to_id == gt::kInvalidFluidId) {
        return false;
    }

    gt::FluidDefinition* from_def = const_cast<gt::FluidDefinition*>(
        gt::FluidRegistry::get_fluid(from_id));
    gt::FluidDefinition* to_def = const_cast<gt::FluidDefinition*>(
        gt::FluidRegistry::get_fluid(to_id));
    if (from_def == nullptr || to_def == nullptr) {
        return false;
    }

    from_def->evaporation_temp = evaporation_temp;
    from_def->evaporation_target = to_id;
    to_def->condensation_temp = condensation_temp;
    to_def->condensation_target = from_id;
    return true;
}

void GDFluidRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDFluidRegistry",
        D_METHOD("register_fluid", "def"),
        &GDFluidRegistry::register_fluid);
    ClassDB::bind_static_method("GDFluidRegistry",
        D_METHOD("get_fluid_id", "name"),
        &GDFluidRegistry::get_fluid_id);
    ClassDB::bind_static_method("GDFluidRegistry",
        D_METHOD("get_fluid", "id"),
        &GDFluidRegistry::get_fluid);
    ClassDB::bind_static_method("GDFluidRegistry",
        D_METHOD("get_fluid_count"),
        &GDFluidRegistry::get_fluid_count);
    ClassDB::bind_static_method("GDFluidRegistry",
        D_METHOD("link_phase_transition", "from_name", "to_name",
                 "evaporation_temp", "condensation_temp"),
        &GDFluidRegistry::link_phase_transition);
}

} // namespace science_and_theology
