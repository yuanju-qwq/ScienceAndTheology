#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/magic/mana_pool.hpp"

namespace science_and_theology {

class GDManaPool : public godot::Resource {
    GDCLASS(GDManaPool, godot::Resource)

public:
    GDManaPool() = default;

    bool consume(int amount);
    void add(int amount);
    void set_max(int new_max);
    void expand_max(int delta);

    int get_current() const;
    int get_max() const;
    float get_fill_percent() const;
    bool is_full() const;

    void tick();
    void tick_bonus(float multiplier);

protected:
    static void _bind_methods();

private:
    magic::ManaPool pool_;
};

} // namespace science_and_theology
