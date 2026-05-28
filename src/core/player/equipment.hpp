#pragma once

#include <cstdint>
#include <array>

#include "common/resource_types.hpp"
#include "tool_def.hpp"

namespace science_and_theology::gt {

enum class EquipmentSlot : uint8_t {
    MAIN_HAND = 0,
    OFF_HAND,
    HEAD,
    CHEST,
    LEGS,
    FEET,
    COUNT
};

inline constexpr size_t kEquipmentSlotCount =
    static_cast<size_t>(EquipmentSlot::COUNT);

inline constexpr const char* kEquipmentSlotNames[] = {
    "Main Hand",
    "Off Hand",
    "Head",
    "Chest",
    "Legs",
    "Feet",
};

class Equipment {
public:
    Equipment() = default;

    bool equip(EquipmentSlot slot, ItemId item_id);
    ItemId unequip(EquipmentSlot slot);
    ItemId get_equipped(EquipmentSlot slot) const;

    ToolStats get_tool_stats() const;

    void clear();

    const std::array<ItemId, kEquipmentSlotCount>& slots() const {
        return slots_;
    }

private:
    std::array<ItemId, kEquipmentSlotCount> slots_{};
};

} // namespace science_and_theology::gt
