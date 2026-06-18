#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/player/satiation_data.hpp"

namespace science_and_theology {

// ============================================================
// GDSatiationData — GDExtension binding for SatiationData
// ============================================================
class GDSatiationData : public godot::Resource {
    GDCLASS(GDSatiationData, godot::Resource)

public:
    GDSatiationData() = default;

    // --- Satiation value ---
    float get_satiation_current() const;
    float get_satiation_max() const;
    float get_decay_rate() const;
    void set_satiation_max(float max_val);
    void set_decay_rate(float rate);

    // Eat food (legacy): restores satiation only.
    float eat(float food_value);

    // Eat food with element essence: restores satiation + element essence.
    // food_dict keys: "food_value" (float), "elements" (Array of [element_id, value]).
    float eat_food(const godot::Dictionary& food_dict);

    // Direct set (for save/load or admin commands).
    void set_satiation(float value);

    // --- Hunger level ---
    int get_hunger_level() const;
    bool get_is_starving() const;

    // --- Effect modifiers ---
    float get_speed_modifier() const;
    float get_health_regen_modifier() const;
    float get_attack_modifier() const;
    float get_starvation_damage_per_tick() const;

    // --- Thresholds ---
    float get_peckish_threshold() const;
    float get_hungry_threshold() const;
    float get_starving_threshold() const;
    void set_peckish_threshold(float val);
    void set_hungry_threshold(float val);
    void set_starving_threshold(float val);

    // --- Source essence pool ---
    float get_source_essence(int element) const;
    float get_total_source_essence() const;
    float get_source_essence_cap() const;
    void set_source_essence_cap(float max_val);
    float get_source_essence_decay_rate() const;
    void set_source_essence_decay_rate(float rate);

    // Get all source essence values as Dictionary {element_id: value}.
    godot::Dictionary get_source_essence_all() const;

    // --- Food helper constructors (exposed to GDScript) ---
    // Create a plant food definition.
    static godot::Dictionary make_plant_food(float food_value, float total_essence);

    // Create a creature food definition.
    static godot::Dictionary make_creature_food(float food_value, float total_essence,
                                                 int creature_element);

    // --- Tick ---
    int tick();

    // --- Reset ---
    void reset();

    // --- Serialization ---
    godot::Dictionary to_dict() const;
    void from_dict(const godot::Dictionary& data);

    // Access to underlying C++ data (for SatiationSystem binding).
    SatiationData& data() { return data_; }
    const SatiationData& data() const { return data_; }

protected:
    static void _bind_methods();

private:
    SatiationData data_;
};

} // namespace science_and_theology
