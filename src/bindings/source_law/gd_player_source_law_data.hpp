#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/source_law/player_source_law_data.hpp"

namespace science_and_theology {

// ============================================================
// GDPlayerSourceLawData — GDExtension binding for PlayerSourceLawData
// ============================================================
class GDPlayerSourceLawData : public godot::Resource {
    GDCLASS(GDPlayerSourceLawData, godot::Resource)

public:
    GDPlayerSourceLawData() = default;

    // --- Source reserve ---
    int get_source_current() const;
    int get_source_max() const;
    float get_source_regen() const;
    void set_source_max(int max_val);
    void add_source(int amount);
    bool consume_source(int amount);

    // --- Stability ---
    float get_stability() const;
    void set_stability(float val);
    void modify_stability(float delta);

    // --- Mutation ---
    float get_mutation() const;
    void set_mutation(float val);
    void modify_mutation(float delta);

    // --- Psionic level ---
    int get_psionic_level() const;
    void set_psionic_level(int level);

    // --- Mental load ---
    int get_mental_load() const;
    void set_mental_load(int load);
    void modify_mental_load(int delta);

    // --- Sublimation ---
    int get_sublimation_level() const;
    int get_path_id() const;
    void set_sublimation_level(int level);
    void set_path_id(int path);

    // --- Initiation check ---
    bool is_initiated() const;

    // --- Source cost element modifier (元素损耗) ---
    float compute_source_cost_multiplier(int new_element, int exclude_slot) const;

    // --- Mana pool ---
    int get_mana_current() const;
    int get_mana_max() const;
    float get_mana_fill_percent() const;
    bool consume_mana(int amount);
    void add_mana(int amount);

    // --- Organs (fixed 7-slot array, always present) ---
    godot::Dictionary get_organ(int slot) const;
    bool transform_organ(int slot, int element, int path, int source_cost);
    void purify_organ(int slot);
    void purify_all_organs();
    void tune_organ(int slot, int degree_delta);

    // --- Elixir application ---
    bool apply_elixir(const godot::String& elixir_id);

    // --- Bloodline (devour dropped organ) ---
    bool devour_organ(const godot::String& dropped_organ_id);

    // --- Transform rejection (排异掉血) ---
    bool is_rejecting() const;
    godot::Dictionary get_rejection() const;
    float get_rejection_progress() const;
    void handle_rejection_death();
    void complete_rejection();

    // --- Available skills ---
    godot::Array get_available_skills() const;

    // --- Element affinities ---
    int get_affinity(int element) const;
    void set_affinity(int element, int value);

    // --- Network affinity report ---
    godot::Dictionary compute_network_report() const;

    // --- Combat attributes ---
    godot::Dictionary compute_combat_attributes() const;

    // --- Tick ---
    void tick();

    // --- Reset ---
    void reset();

    // --- Serialization ---
    godot::Dictionary to_dict() const;
    void from_dict(const godot::Dictionary& data);

protected:
    static void _bind_methods();

private:
    source_law::PlayerSourceLawData data_;
};

} // namespace science_and_theology
