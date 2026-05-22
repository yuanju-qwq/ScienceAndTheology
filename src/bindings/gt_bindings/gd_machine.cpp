#include "gd_machine.h"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDMachine::GDMachine() = default;
GDMachine::~GDMachine() = default;

void GDMachine::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "machine_name", "machine_type",
                                   "recipe_map_name", "tier",
                                   "max_input_voltage", "input_slots",
                                   "output_slots", "power_buffer",
                                   "footprint_w", "footprint_h"),
                         &GDMachine::configure);

    ClassDB::bind_method(D_METHOD("get_machine_name"),
                         &GDMachine::get_machine_name);
    ClassDB::bind_method(D_METHOD("get_machine_type"),
                         &GDMachine::get_machine_type);
    ClassDB::bind_method(D_METHOD("get_state"), &GDMachine::get_state);
    ClassDB::bind_method(D_METHOD("get_state_name"),
                         &GDMachine::get_state_name);
    ClassDB::bind_method(D_METHOD("get_progress"),
                         &GDMachine::get_progress);
    ClassDB::bind_method(D_METHOD("get_progress_max"),
                         &GDMachine::get_progress_max);
    ClassDB::bind_method(D_METHOD("get_progress_percent"),
                         &GDMachine::get_progress_percent);

    ClassDB::bind_method(D_METHOD("get_footprint_width"),
                         &GDMachine::get_footprint_width);
    ClassDB::bind_method(D_METHOD("get_footprint_height"),
                         &GDMachine::get_footprint_height);

    ClassDB::bind_method(D_METHOD("add_port", "rel_x", "rel_y",
                                   "port_type", "direction", "locked"),
                         &GDMachine::add_port, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_port_info", "index"),
                         &GDMachine::get_port_info);
    ClassDB::bind_method(D_METHOD("get_port_count"),
                         &GDMachine::get_port_count);

    ClassDB::bind_method(D_METHOD("set_port_direction", "index", "direction"),
                         &GDMachine::set_port_direction);
    ClassDB::bind_method(D_METHOD("is_port_locked", "index"),
                         &GDMachine::is_port_locked);

    ClassDB::bind_method(D_METHOD("define_module_slot", "category",
                                   "max_count", "min_tier", "max_tier"),
                         &GDMachine::define_module_slot);
    ClassDB::bind_method(D_METHOD("get_module_slot_info", "index"),
                         &GDMachine::get_module_slot_info);
    ClassDB::bind_method(D_METHOD("get_module_slot_count"),
                         &GDMachine::get_module_slot_count);

    ClassDB::bind_method(D_METHOD("install_module", "module_name"),
                         &GDMachine::install_module);
    ClassDB::bind_method(D_METHOD("remove_module", "module_name"),
                         &GDMachine::remove_module);
    ClassDB::bind_method(D_METHOD("get_installed_module_count"),
                         &GDMachine::get_installed_module_count);
    ClassDB::bind_method(D_METHOD("get_installed_module_info", "index"),
                         &GDMachine::get_installed_module_info);

    ClassDB::bind_method(D_METHOD("get_derived_heat_capacity"),
                         &GDMachine::get_derived_heat_capacity);
    ClassDB::bind_method(D_METHOD("get_derived_parallel"),
                         &GDMachine::get_derived_parallel);
    ClassDB::bind_method(D_METHOD("get_derived_efficiency_pct"),
                         &GDMachine::get_derived_efficiency_pct);
    ClassDB::bind_method(D_METHOD("get_derived_pollution_pct"),
                         &GDMachine::get_derived_pollution_pct);
    ClassDB::bind_method(D_METHOD("get_max_input_voltage"),
                         &GDMachine::get_max_input_voltage);

    ClassDB::bind_method(D_METHOD("set_power_available", "available_eu_t"),
                         &GDMachine::set_power_available);
    ClassDB::bind_method(D_METHOD("get_power_demand"),
                         &GDMachine::get_power_demand);
    ClassDB::bind_method(D_METHOD("is_powered"),
                         &GDMachine::is_powered);

    ClassDB::bind_method(D_METHOD("tick"), &GDMachine::tick);
    ClassDB::bind_method(D_METHOD("abort_processing"),
                         &GDMachine::abort_processing);
    ClassDB::bind_method(D_METHOD("reset_machine"),
                         &GDMachine::reset_machine);

    ClassDB::bind_method(D_METHOD("get_input_slot_count"),
                         &GDMachine::get_input_slot_count);
    ClassDB::bind_method(D_METHOD("get_output_slot_count"),
                         &GDMachine::get_output_slot_count);

    ClassDB::bind_static_method("GDMachine",
        D_METHOD("get_module_definition", "name"),
        &GDMachine::get_module_definition);
    ClassDB::bind_static_method("GDMachine",
        D_METHOD("get_all_module_names"),
        &GDMachine::get_all_module_names);
    ClassDB::bind_static_method("GDMachine",
        D_METHOD("get_module_names_by_category", "category"),
        &GDMachine::get_module_names_by_category);
}

// --- Configuration ---

void GDMachine::configure(godot::String machine_name,
                           godot::String machine_type,
                           godot::String recipe_map_name,
                           int tier, int64_t max_input_voltage,
                           int64_t input_slots, int64_t output_slots,
                           int64_t power_buffer,
                           int footprint_w, int footprint_h) {
    name_buf_ = machine_name.utf8().get_data();
    type_buf_ = machine_type.utf8().get_data();
    recipe_buf_ = recipe_map_name.utf8().get_data();

    config_.machine_name = name_buf_.c_str();
    config_.machine_type = type_buf_.c_str();
    config_.recipe_map_name = recipe_buf_.c_str();
    config_.tier = static_cast<VoltageTier>(tier);
    config_.max_input_voltage = max_input_voltage > 0
        ? max_input_voltage : get_voltage(config_.tier);
    config_.input_slot_count = static_cast<size_t>(input_slots);
    config_.output_slot_count = static_cast<size_t>(output_slots);
    config_.internal_power_buffer = power_buffer;
    config_.footprint_width = footprint_w;
    config_.footprint_height = footprint_h;

    machine_ = std::make_unique<Machine>(config_);
}

// --- Accessors ---

godot::String GDMachine::get_machine_name() const {
    if (machine_) return machine_->config().machine_name;
    return "";
}

godot::String GDMachine::get_machine_type() const {
    if (machine_) return machine_->config().machine_type;
    return "";
}

int GDMachine::get_state() const {
    if (machine_) return static_cast<int>(machine_->state());
    return 0;
}

godot::String GDMachine::get_state_name() const {
    if (machine_) return machine_->state_name();
    return "";
}

int64_t GDMachine::get_progress() const {
    if (machine_) return machine_->progress();
    return 0;
}

int64_t GDMachine::get_progress_max() const {
    if (machine_) return machine_->progress_max();
    return 0;
}

float GDMachine::get_progress_percent() const {
    if (machine_) return machine_->progress_percent();
    return 0.0f;
}

// --- Footprint ---

int GDMachine::get_footprint_width() const {
    return config_.footprint_width;
}

int GDMachine::get_footprint_height() const {
    return config_.footprint_height;
}

// --- Ports ---

void GDMachine::add_port(int rel_x, int rel_y, int port_type,
                          int direction, bool locked) {
    MachinePort port;
    port.rel_x = rel_x;
    port.rel_y = rel_y;
    port.type = static_cast<PortType>(port_type);
    port.direction = static_cast<PortDirection>(direction);
    port.direction_locked = locked;
    config_.ports.push_back(port);
}

godot::Dictionary GDMachine::get_port_info(int index) const {
    godot::Dictionary info;
    if (index < 0 || index >= static_cast<int>(config_.ports.size())) {
        return info;
    }
    const auto& port = config_.ports[index];
    PortDirection effective_dir = port.direction;
    if (machine_ && index < static_cast<int>(machine_->port_states().size())) {
        effective_dir = machine_->port_states()[index].direction;
    }
    info["rel_x"] = port.rel_x;
    info["rel_y"] = port.rel_y;
    info["type"] = static_cast<int>(port.type);
    info["direction"] = static_cast<int>(effective_dir);
    info["locked"] = port.direction_locked;
    return info;
}

int GDMachine::get_port_count() const {
    return static_cast<int>(config_.ports.size());
}

bool GDMachine::set_port_direction(int index, int direction) {
    if (!machine_) return false;
    if (index < 0 || index >= static_cast<int>(config_.ports.size())) {
        return false;
    }
    if (config_.ports[index].direction_locked) return false;
    machine_->set_port_direction(index,
                                  static_cast<PortDirection>(direction));
    return true;
}

bool GDMachine::is_port_locked(int index) const {
    if (index < 0 || index >= static_cast<int>(config_.ports.size())) {
        return true;
    }
    return config_.ports[index].direction_locked;
}

// --- Module slots ---

void GDMachine::define_module_slot(int category, int max_count,
                                    int min_tier, int max_tier) {
    ModuleSlot slot;
    slot.category = static_cast<ModuleCategory>(category);
    slot.max_count = max_count;
    slot.min_tier = static_cast<VoltageTier>(min_tier);
    slot.max_tier = static_cast<VoltageTier>(max_tier);
    config_.module_slots.push_back(slot);
}

godot::Dictionary GDMachine::get_module_slot_info(int index) const {
    godot::Dictionary info;
    if (index < 0 || index >= static_cast<int>(config_.module_slots.size())) {
        return info;
    }
    const auto& slot = config_.module_slots[index];
    info["category"] = static_cast<int>(slot.category);
    info["max_count"] = slot.max_count;
    info["min_tier"] = static_cast<int>(slot.min_tier);
    info["max_tier"] = static_cast<int>(slot.max_tier);
    return info;
}

int GDMachine::get_module_slot_count() const {
    return static_cast<int>(config_.module_slots.size());
}

// --- Module installation ---

bool GDMachine::install_module(const godot::String& module_name) {
    if (!machine_) return false;
    const ModuleDefinition* def = ModuleRegistry::get(
        module_name.utf8().get_data());
    return machine_->install_module(def);
}

bool GDMachine::remove_module(const godot::String& module_name) {
    if (!machine_) return false;
    const ModuleDefinition* def = ModuleRegistry::get(
        module_name.utf8().get_data());
    return machine_->remove_module(def);
}

int64_t GDMachine::get_installed_module_count() const {
    if (!machine_) return 0;
    return static_cast<int64_t>(machine_->installed_modules().size());
}

godot::Dictionary GDMachine::get_installed_module_info(int index) const {
    godot::Dictionary info;
    if (!machine_) return info;
    const auto& mods = machine_->installed_modules();
    if (index < 0 || index >= static_cast<int>(mods.size())) return info;
    const auto& inst = mods[index];
    if (inst.def == nullptr) return info;
    info["name"] = inst.def->name;
    info["display_name"] = inst.def->display_name;
    info["category"] = static_cast<int>(inst.def->category);
    info["tier"] = static_cast<int>(inst.def->tier);
    return info;
}

// --- Derived stats ---

int64_t GDMachine::get_derived_heat_capacity() const {
    if (!machine_) return 0;
    return machine_->derived_heat_capacity();
}

int64_t GDMachine::get_derived_parallel() const {
    if (!machine_) return 1;
    return machine_->derived_parallel();
}

int64_t GDMachine::get_derived_efficiency_pct() const {
    if (!machine_) return 100;
    return machine_->derived_efficiency_pct();
}

int64_t GDMachine::get_derived_pollution_pct() const {
    if (!machine_) return 100;
    return machine_->derived_pollution_pct();
}

int64_t GDMachine::get_max_input_voltage() const {
    if (!machine_) return 0;
    return machine_->config().max_input_voltage;
}

// --- Power ---

void GDMachine::set_power_available(int64_t available_eu_t) {
    if (machine_) machine_->set_power_available(available_eu_t);
}

int64_t GDMachine::get_power_demand() const {
    if (machine_) return machine_->get_power_demand();
    return 0;
}

bool GDMachine::is_powered() const {
    if (machine_) return machine_->is_powered();
    return false;
}

// --- Processing ---

void GDMachine::tick() {
    if (machine_) machine_->tick();
}

void GDMachine::abort_processing() {
    if (machine_) machine_->abort();
}

void GDMachine::reset_machine() {
    if (machine_) machine_->reset();
}

// --- Inventory ---

int64_t GDMachine::get_input_slot_count() const {
    return static_cast<int64_t>(config_.input_slot_count);
}

int64_t GDMachine::get_output_slot_count() const {
    return static_cast<int64_t>(config_.output_slot_count);
}

// --- Static helpers ---

godot::Dictionary GDMachine::get_module_definition(
        const godot::String& name) {
    godot::Dictionary info;
    const ModuleDefinition* def = ModuleRegistry::get(
        name.utf8().get_data());
    if (def == nullptr) return info;
    info["name"] = def->name;
    info["display_name"] = def->display_name;
    info["category"] = static_cast<int>(def->category);
    info["tier"] = static_cast<int>(def->tier);
    info["max_eu_per_tick"] = def->max_eu_per_tick;
    info["heat_capacity"] = def->heat_capacity;
    info["efficiency_pct"] = def->efficiency_pct;
    info["parallel_bonus"] = def->parallel_bonus;
    info["pollution_reduction_pct"] = def->pollution_reduction_pct;
    info["speed_multiplier_pct"] = def->speed_multiplier_pct;
    info["power_multiplier_pct"] = def->power_multiplier_pct;
    return info;
}

godot::PackedStringArray GDMachine::get_all_module_names() {
    godot::PackedStringArray arr;
    for (auto* def : ModuleRegistry::get_all()) {
        arr.append(def->name);
    }
    return arr;
}

godot::PackedStringArray GDMachine::get_module_names_by_category(
        int category) {
    godot::PackedStringArray arr;
    auto cat = static_cast<ModuleCategory>(category);
    for (auto* def : ModuleRegistry::get_all()) {
        if (def->category == cat) {
            arr.append(def->name);
        }
    }
    return arr;
}

} // namespace science_and_theology