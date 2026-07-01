#include "gd_machine_definition_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include "core/machine/machine_definition_registry.hpp"

namespace science_and_theology {

using namespace godot;

namespace {

// Parse a PanelElement from a GDScript Dictionary. Missing fields keep
// their struct defaults. `rect` is [left, top, right, bottom] offsets.
gt::PanelElement parse_panel_element(const Dictionary& e) {
    gt::PanelElement out;
    out.element_id = String(e.get("element_id", "")).utf8().get_data();
    out.element_type = String(e.get("element_type", "")).utf8().get_data();
    out.role = String(e.get("role", "")).utf8().get_data();
    out.slot_index = static_cast<int32_t>(e.get("slot_index", 0));
    Array rect = e.get("rect", Array());
    if (rect.size() >= 4) {
        out.offset_left = static_cast<float>(rect[0]);
        out.offset_top = static_cast<float>(rect[1]);
        out.offset_right = static_cast<float>(rect[2]);
        out.offset_bottom = static_cast<float>(rect[3]);
    }
    return out;
}

// Build a GDScript Dictionary describing a PanelElement (mirrors parse_panel_element).
Dictionary panel_element_to_dict(const gt::PanelElement& e) {
    Dictionary d;
    d["element_id"] = String(e.element_id.c_str());
    d["element_type"] = String(e.element_type.c_str());
    d["role"] = String(e.role.c_str());
    d["slot_index"] = static_cast<int64_t>(e.slot_index);
    Array rect;
    rect.append(e.offset_left);
    rect.append(e.offset_top);
    rect.append(e.offset_right);
    rect.append(e.offset_bottom);
    d["rect"] = rect;
    return d;
}

} // namespace

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

    // Parse the optional data-driven GUI layout.
    Variant layout_var = def.get("panel_layout", nullptr);
    if (layout_var.get_type() == Variant::DICTIONARY) {
        Dictionary layout = layout_var;
        cpp_def.layout.panel_width = static_cast<float>(layout.get("panel_width", 320.0));
        cpp_def.layout.panel_height = static_cast<float>(layout.get("panel_height", 220.0));
        Array elements = layout.get("elements", Array());
        for (int i = 0; i < elements.size(); ++i) {
            if (elements[i].get_type() == Variant::DICTIONARY) {
                cpp_def.layout.elements.push_back(
                    parse_panel_element(elements[i]));
            }
        }
    }

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

    // Emit the data-driven GUI layout (always present, may be empty).
    Dictionary layout;
    layout["panel_width"] = def->layout.panel_width;
    layout["panel_height"] = def->layout.panel_height;
    Array elements;
    for (const gt::PanelElement& e : def->layout.elements) {
        elements.append(panel_element_to_dict(e));
    }
    layout["elements"] = elements;
    out["panel_layout"] = layout;
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
