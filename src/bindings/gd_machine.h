#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/machine/machine.hpp"
#include "core/machine/machine_port.hpp"
#include "core/machine/module.hpp"

namespace science_and_theology {

class GDMachine : public godot::Resource {
    GDCLASS(GDMachine, godot::Resource)

public:
    GDMachine();
    ~GDMachine() override;

    void configure(godot::String machine_name, godot::String machine_type,
                   godot::String recipe_map_name,
                   int tier, int64_t max_input_voltage,
                   int64_t input_slots, int64_t output_slots,
                   int64_t power_buffer,
                   int footprint_w, int footprint_h);

    godot::String get_machine_name() const;
    godot::String get_machine_type() const;
    int get_state() const;
    godot::String get_state_name() const;
    int64_t get_progress() const;
    int64_t get_progress_max() const;
    float get_progress_percent() const;

    int get_footprint_width() const;
    int get_footprint_height() const;

    // Port API.
    // add_port: rel_x/rel_y relative to machine origin, port_type (0=ENERGY, 1=UNIVERSAL),
    //           direction (0=IN, 1=OUT), locked (player cannot flip).
    void add_port(int rel_x, int rel_y, int port_type, int direction,
                  bool locked = false);
    godot::Dictionary get_port_info(int index) const;
    int get_port_count() const;

    // Runtime direction flipping (only if not locked).
    bool set_port_direction(int index, int direction);
    bool is_port_locked(int index) const;

    void define_module_slot(int category, int max_count,
                            int min_tier, int max_tier);
    godot::Dictionary get_module_slot_info(int index) const;
    int get_module_slot_count() const;

    bool install_module(const godot::String& module_name);
    bool remove_module(const godot::String& module_name);
    int64_t get_installed_module_count() const;
    godot::Dictionary get_installed_module_info(int index) const;

    int64_t get_derived_heat_capacity() const;
    int64_t get_derived_parallel() const;
    int64_t get_derived_efficiency_pct() const;
    int64_t get_derived_pollution_pct() const;
    int64_t get_max_input_voltage() const;

    void set_power_available(int64_t available_eu_t);
    int64_t get_power_demand() const;
    bool is_powered() const;

    void tick();
    void abort_processing();
    void reset_machine();

    int64_t get_input_slot_count() const;
    int64_t get_output_slot_count() const;

    static godot::Dictionary get_module_definition(const godot::String& name);
    static godot::PackedStringArray get_all_module_names();
    static godot::PackedStringArray get_module_names_by_category(int category);

protected:
    static void _bind_methods();

private:
    gt::MachineConfig config_;
    std::unique_ptr<gt::Machine> machine_;

    // Owned string buffers (config_ points into these).
    std::string name_buf_;
    std::string type_buf_;
    std::string recipe_buf_;
};

} // namespace science_and_theology