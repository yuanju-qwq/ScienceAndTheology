#include "gd_player_equipment.h"

#include <godot_cpp/core/class_db.hpp>

VARIANT_ENUM_CAST(science_and_theology::GDPlayerEquipment::SlotConst);

namespace science_and_theology {

using namespace godot;

GDPlayerEquipment::GDPlayerEquipment() = default;
GDPlayerEquipment::~GDPlayerEquipment() = default;

bool GDPlayerEquipment::equip(int32_t slot, int64_t item_id) {
    return equipment_.equip(
        static_cast<gt::EquipmentSlot>(slot),
        static_cast<gt::ItemId>(item_id));
}

int64_t GDPlayerEquipment::unequip(int32_t slot) {
    return static_cast<int64_t>(
        equipment_.unequip(static_cast<gt::EquipmentSlot>(slot)));
}

int64_t GDPlayerEquipment::get_equipped(int32_t slot) const {
    return static_cast<int64_t>(
        equipment_.get_equipped(static_cast<gt::EquipmentSlot>(slot)));
}

Dictionary GDPlayerEquipment::get_tool_stats() const {
    auto stats = equipment_.get_tool_stats();
    Dictionary d;
    d["type"] = static_cast<int32_t>(stats.type);
    d["tier"] = stats.tier;
    d["speed_multiplier"] = stats.speed_multiplier;
    d["mining_level"] = stats.mining_level;
    d["attack_damage"] = stats.attack_damage;
    d["attack_cooldown"] = stats.attack_cooldown;
    d["max_durability"] = stats.max_durability;
    d["range"] = stats.range;
    return d;
}

void GDPlayerEquipment::clear() {
    equipment_.clear();
}

void GDPlayerEquipment::_bind_methods() {
    ClassDB::bind_method(D_METHOD("equip", "slot", "item_id"),
                         &GDPlayerEquipment::equip);
    ClassDB::bind_method(D_METHOD("unequip", "slot"),
                         &GDPlayerEquipment::unequip);
    ClassDB::bind_method(D_METHOD("get_equipped", "slot"),
                         &GDPlayerEquipment::get_equipped);
    ClassDB::bind_method(D_METHOD("get_tool_stats"),
                         &GDPlayerEquipment::get_tool_stats);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDPlayerEquipment::clear);

    BIND_ENUM_CONSTANT(SLOT_MAIN_HAND);
    BIND_ENUM_CONSTANT(SLOT_OFF_HAND);
    BIND_ENUM_CONSTANT(SLOT_HEAD);
    BIND_ENUM_CONSTANT(SLOT_CHEST);
    BIND_ENUM_CONSTANT(SLOT_LEGS);
    BIND_ENUM_CONSTANT(SLOT_FEET);
}

} // namespace science_and_theology
