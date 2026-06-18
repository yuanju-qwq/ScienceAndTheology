#include "gd_player_accessory.h"

#include <godot_cpp/core/class_db.hpp>

VARIANT_ENUM_CAST(science_and_theology::GDPlayerAccessory::SlotConst);

namespace science_and_theology {

using namespace godot;

GDPlayerAccessory::GDPlayerAccessory() = default;
GDPlayerAccessory::~GDPlayerAccessory() = default;

bool GDPlayerAccessory::equip(int32_t slot, int64_t item_id) {
    return accessory_.equip(
        static_cast<gt::AccessorySlot>(slot),
        static_cast<gt::ItemId>(item_id));
}

int64_t GDPlayerAccessory::unequip(int32_t slot) {
    return static_cast<int64_t>(
        accessory_.unequip(static_cast<gt::AccessorySlot>(slot)));
}

int64_t GDPlayerAccessory::get_equipped(int32_t slot) const {
    return static_cast<int64_t>(
        accessory_.get_equipped(static_cast<gt::AccessorySlot>(slot)));
}

void GDPlayerAccessory::clear() {
    accessory_.clear();
}

void GDPlayerAccessory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("equip", "slot", "item_id"),
                         &GDPlayerAccessory::equip);
    ClassDB::bind_method(D_METHOD("unequip", "slot"),
                         &GDPlayerAccessory::unequip);
    ClassDB::bind_method(D_METHOD("get_equipped", "slot"),
                         &GDPlayerAccessory::get_equipped);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDPlayerAccessory::clear);

    BIND_ENUM_CONSTANT(SLOT_NECKLACE);
    BIND_ENUM_CONSTANT(SLOT_BACK);
    BIND_ENUM_CONSTANT(SLOT_BRACELET);
    BIND_ENUM_CONSTANT(SLOT_RING);
    BIND_ENUM_CONSTANT(SLOT_BELT);
    BIND_ENUM_CONSTANT(SLOT_CHARM);
}

} // namespace science_and_theology
