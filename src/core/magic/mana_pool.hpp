#pragma once

#include <cstdint>

namespace science_and_theology::magic {

struct ManaPool {
    int current_mana = 0;
    int max_mana = 100;
    float regen_rate = 0.1f;   // per tick

    bool consume(int amount) {
        if (current_mana < amount) return false;
        current_mana -= amount;
        return true;
    }

    void add(int amount) {
        current_mana += amount;
        if (current_mana > max_mana) current_mana = max_mana;
    }

    void set_max(int new_max) {
        max_mana = new_max;
        if (current_mana > max_mana) current_mana = max_mana;
    }

    void expand_max(int delta) {
        max_mana += delta;
    }

    bool is_full() const {
        return current_mana >= max_mana;
    }

    float fill_percent() const {
        if (max_mana <= 0) return 0.0f;
        return static_cast<float>(current_mana) / static_cast<float>(max_mana);
    }

    void tick() {
        if (current_mana < max_mana) {
            current_mana += static_cast<int>(regen_rate);
            if (current_mana > max_mana) current_mana = max_mana;
        }
    }

    void tick_bonus(float multiplier) {
        if (current_mana < max_mana) {
            current_mana += static_cast<int>(regen_rate * multiplier);
            if (current_mana > max_mana) current_mana = max_mana;
        }
    }
};

} // namespace science_and_theology::magic
