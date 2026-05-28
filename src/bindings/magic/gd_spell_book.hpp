#pragma once

#include <cstdint>
#include <string>

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/magic/spell_book.hpp"
#include "core/magic/spell_compiler.hpp"
#include "core/magic/spell_caster.hpp"
#include "core/magic/mana_pool.hpp"

namespace science_and_theology {

class GDSpellBook : public godot::Resource {
    GDCLASS(GDSpellBook, godot::Resource)

public:
    GDSpellBook();
    ~GDSpellBook() override = default;

    void configure(int tier_level);

    int get_tier() const;
    int get_max_presets() const;
    int get_max_augments() const;
    float get_mana_multiplier() const;
    int get_active_preset() const;

    // Preset management
    bool set_active_preset(int index);
    bool save_current_preset(int slot);

    // Glyph slotting — consumes glyph (insert only, no removal)
    bool set_form(int preset_index, const godot::String& glyph_name);
    bool set_effect(int preset_index, const godot::String& glyph_name);
    bool add_augment(int preset_index, const godot::String& glyph_name);
    void clear_preset(int preset_index);

    // Spell compilation
    godot::Dictionary compile_spell(int preset_index = -1) const;
    bool can_cast(const godot::Dictionary& mana_dict, int64_t last_cast_tick) const;

    // Query
    godot::Dictionary get_preset_info(int preset_index) const;
    godot::Dictionary get_glyph_info(int preset_index, int slot) const;

protected:
    static void _bind_methods();

private:
    magic::SpellBook book_;
    magic::SpellCompiler compiler_;
    magic::SpellCaster caster_;

    std::string id_buf_;

    static godot::Dictionary spell_to_dict(const magic::SpellDef& spell);
};

} // namespace science_and_theology
