#include "gd_machine_definition_registry.h"

#include <godot_cpp/core/class_db.hpp>

#include "core/machine/machine_definition_registry.hpp"

namespace science_and_theology {

using namespace godot;

bool GDMachineDefinitionRegistry::register_definition(const Dictionary& def) {
    String type_key = def.get("type_key", "");
    if (type_key.is_empty()) return false;

    gt::MachineDefinition cpp_def;
    cpp_def.type_key = type_key.utf8().get_data();
    cpp_def.display_name = String(def.get("display_name", "")).utf8().get_data();
    cpp_def.gui_scene_path = String(def.get("gui_scene_path", "")).utf8().get_data();
    cpp_def.model_path = String(def.get("model_path", "")).utf8().get_data();
    cpp_def.tier = static_cast<int32_t>(def.get("tier", 1));
    cpp_def.input_slots = static_cast<int32_t>(def.get("input_slots", 1));
    cpp_def.output_slots = static_cast<int32_t>(def.get("output_slots", 1));
    cpp_def.power_capacity = static_cast<int32_t>(def.get("power_capacity", 0));
    cpp_def.hatch_mask = static_cast<uint16_t>(
        static_cast<int64_t>(def.get("hatch_mask", 0)));

    return gt::MachineDefinitionRegistry::register_definition(cpp_def);
}

bool GDMachineDefinitionRegistry::has_definition(const String& type_key) {
    return gt::MachineDefinitionRegistry::has_definition(
        type_key.utf8().get_data());
}

Dictionary GDMachineDefinitionRegistry::get_definition(const String& type_key) {
    Dictionary out;
    const gt::MachineDefinition* def = gt::MachineDefinitionRegistry::get_definition(
        type_key.utf8().get_data());
    if (def == nullptr) return out;

    out["type_key"] = String(def->type_key.c_str());
    out["display_name"] = String(def->display_name.c_str());
    out["gui_scene_path"] = String(def->gui_scene_path.c_str());
    out["model_path"] = String(def->model_path.c_str());
    out["tier"] = static_cast<int64_t>(def->tier);
    out["input_slots"] = static_cast<int64_t>(def->input_slots);
    out["output_slots"] = static_cast<int64_t>(def->output_slots);
    out["power_capacity"] = static_cast<int64_t>(def->power_capacity);
    out["hatch_mask"] = static_cast<int64_t>(def->hatch_mask);
    return out;
}

int64_t GDMachineDefinitionRegistry::get_definition_count() {
    return static_cast<int64_t>(gt::MachineDefinitionRegistry::get_definition_count());
}

void GDMachineDefinitionRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDMachineDefinitionRegistry",
        D_METHOD("register_definition", "def"),
        &GDMachineDefinitionRegistry::register_definition);
    ClassDB::bind_static_method("GDMachineDefinitionRegistry",
        D_METHOD("has_definition", "type_key"),
        &GDMachineDefinitionRegistry::has_definition);
    ClassDB::bind_static_method("GDMachineDefinitionRegistry",
        D_METHOD("get_definition", "type_key"),
        &GDMachineDefinitionRegistry::get_definition);
    ClassDB::bind_static_method("GDMachineDefinitionRegistry",
        D_METHOD("get_definition_count"),
        &GDMachineDefinitionRegistry::get_definition_count);
}

} // namespace science_and_theology
