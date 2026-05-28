#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/player/equipment.hpp"

namespace science_and_theology {

class GDPlayerEquipment : public godot::Resource {
    GDCLASS(GDPlayerEquipment, godot::Resource)

public:
    enum SlotConst {
        SLOT_MAIN_HAND = 0,
        SLOT_OFF_HAND  = 1,
        SLOT_HEAD      = 2,
        SLOT_CHEST     = 3,
        SLOT_LEGS      = 4,
        SLOT_FEET      = 5,
    };

    GDPlayerEquipment();
    ~GDPlayerEquipment() override;

    bool equip(int32_t slot, int64_t item_id);
    int64_t unequip(int32_t slot);
    int64_t get_equipped(int32_t slot) const;

    godot::Dictionary get_tool_stats() const;

    void clear();

    gt::Equipment& get_equipment() { return equipment_; }
    const gt::Equipment& get_equipment() const { return equipment_; }

protected:
    static void _bind_methods();

private:
    gt::Equipment equipment_;
};

} // namespace science_and_theology
