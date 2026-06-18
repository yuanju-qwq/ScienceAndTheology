#include "accessory.hpp"

namespace science_and_theology::gt {

bool Accessory::equip(AccessorySlot slot, ItemId item_id) {
    if (slot >= AccessorySlot::COUNT) return false;
    auto idx = static_cast<size_t>(slot);
    slots_[idx] = item_id;
    return true;
}

ItemId Accessory::unequip(AccessorySlot slot) {
    if (slot >= AccessorySlot::COUNT) return kInvalidItemId;
    auto idx = static_cast<size_t>(slot);
    ItemId old = slots_[idx];
    slots_[idx] = kInvalidItemId;
    return old;
}

ItemId Accessory::get_equipped(AccessorySlot slot) const {
    if (slot >= AccessorySlot::COUNT) return kInvalidItemId;
    return slots_[static_cast<size_t>(slot)];
}

void Accessory::clear() {
    for (auto& slot : slots_) {
        slot = kInvalidItemId;
    }
}

} // namespace science_and_theology::gt
