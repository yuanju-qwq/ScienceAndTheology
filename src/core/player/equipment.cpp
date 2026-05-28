#include "equipment.hpp"

namespace science_and_theology::gt {

bool Equipment::equip(EquipmentSlot slot, ItemId item_id) {
    if (slot >= EquipmentSlot::COUNT) return false;
    auto idx = static_cast<size_t>(slot);
    slots_[idx] = item_id;
    return true;
}

ItemId Equipment::unequip(EquipmentSlot slot) {
    if (slot >= EquipmentSlot::COUNT) return kInvalidItemId;
    auto idx = static_cast<size_t>(slot);
    ItemId old = slots_[idx];
    slots_[idx] = kInvalidItemId;
    return old;
}

ItemId Equipment::get_equipped(EquipmentSlot slot) const {
    if (slot >= EquipmentSlot::COUNT) return kInvalidItemId;
    return slots_[static_cast<size_t>(slot)];
}

ToolStats Equipment::get_tool_stats() const {
    // Main hand tool provides primary stats
    ItemId main_item = slots_[static_cast<size_t>(EquipmentSlot::MAIN_HAND)];
    ToolStats stats;

    // Off-hand may contribute additional modifiers
    // For now, just return stats based on main hand
    // The actual mapping from ItemId to ToolStats is done by the binding layer
    // which can look up the ToolData resource.

    return stats;
}

void Equipment::clear() {
    for (auto& slot : slots_) {
        slot = kInvalidItemId;
    }
}

} // namespace science_and_theology::gt
