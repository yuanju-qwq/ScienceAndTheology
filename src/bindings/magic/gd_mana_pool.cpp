#include "gd_mana_pool.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace science_and_theology {

void GDManaPool::_bind_methods() {
    ClassDB::bind_method(D_METHOD("consume", "amount"),
                         &GDManaPool::consume);
    ClassDB::bind_method(D_METHOD("add", "amount"),
                         &GDManaPool::add);
    ClassDB::bind_method(D_METHOD("set_max", "new_max"),
                         &GDManaPool::set_max);
    ClassDB::bind_method(D_METHOD("expand_max", "delta"),
                         &GDManaPool::expand_max);
    ClassDB::bind_method(D_METHOD("get_current"),
                         &GDManaPool::get_current);
    ClassDB::bind_method(D_METHOD("get_max"),
                         &GDManaPool::get_max);
    ClassDB::bind_method(D_METHOD("get_fill_percent"),
                         &GDManaPool::get_fill_percent);
    ClassDB::bind_method(D_METHOD("is_full"),
                         &GDManaPool::is_full);
    ClassDB::bind_method(D_METHOD("tick"),
                         &GDManaPool::tick);
    ClassDB::bind_method(D_METHOD("tick_bonus", "multiplier"),
                         &GDManaPool::tick_bonus);
}

bool GDManaPool::consume(int amount) {
    return pool_.consume(amount);
}

void GDManaPool::add(int amount) {
    pool_.add(amount);
}

void GDManaPool::set_max(int new_max) {
    pool_.set_max(new_max);
}

void GDManaPool::expand_max(int delta) {
    pool_.expand_max(delta);
}

int GDManaPool::get_current() const {
    return pool_.current_mana;
}

int GDManaPool::get_max() const {
    return pool_.max_mana;
}

float GDManaPool::get_fill_percent() const {
    return pool_.fill_percent();
}

bool GDManaPool::is_full() const {
    return pool_.is_full();
}

void GDManaPool::tick() {
    pool_.tick();
}

void GDManaPool::tick_bonus(float multiplier) {
    pool_.tick_bonus(multiplier);
}

} // namespace science_and_theology
