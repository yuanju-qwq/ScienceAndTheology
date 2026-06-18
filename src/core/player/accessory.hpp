#pragma once

#include <cstdint>
#include <array>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// ============================================================
// AccessorySlot — Curios/Trinkets-style accessory slot types
// ============================================================
//
// Accessories are non-armor wearable items (necklaces, rings, belts,
// charms, etc.) that exist alongside the Equipment system.
// V0.3 only uses HANDS (source law weapon); other slots are reserved
// for future versions.

enum class AccessorySlot : uint8_t {
    NECKLACE = 0,
    BACK,       // Mobility pack, glider
    BRACELET,
    RING,
    BELT,       // Mana container, utility belt
    CHARM,      // Source charm, amulet
    COUNT
};

inline constexpr size_t kAccessorySlotCount =
    static_cast<size_t>(AccessorySlot::COUNT);

inline constexpr const char* kAccessorySlotNames[] = {
    "Necklace",
    "Back",
    "Bracelet",
    "Ring",
    "Belt",
    "Charm",
};

// ============================================================
// Accessory — manages accessory slot items
// ============================================================

class Accessory {
public:
    Accessory() = default;

    bool equip(AccessorySlot slot, ItemId item_id);
    ItemId unequip(AccessorySlot slot);
    ItemId get_equipped(AccessorySlot slot) const;

    void clear();

    const std::array<ItemId, kAccessorySlotCount>& slots() const {
        return slots_;
    }

private:
    std::array<ItemId, kAccessorySlotCount> slots_{};
};

} // namespace science_and_theology::gt
