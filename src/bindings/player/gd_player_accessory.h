#pragma once

#include <godot_cpp/classes/resource.hpp>

#include "core/player/accessory.hpp"

namespace science_and_theology {

class GDPlayerAccessory : public godot::Resource {
    GDCLASS(GDPlayerAccessory, godot::Resource)

public:
    enum SlotConst {
        SLOT_NECKLACE = 0,
        SLOT_BACK     = 1,
        SLOT_BRACELET = 2,
        SLOT_RING     = 3,
        SLOT_BELT     = 4,
        SLOT_CHARM    = 5,
    };

    GDPlayerAccessory();
    ~GDPlayerAccessory() override;

    bool equip(int32_t slot, int64_t item_id);
    int64_t unequip(int32_t slot);
    int64_t get_equipped(int32_t slot) const;

    void clear();

    gt::Accessory& get_accessory() { return accessory_; }
    const gt::Accessory& get_accessory() const { return accessory_; }

protected:
    static void _bind_methods();

private:
    gt::Accessory accessory_;
};

} // namespace science_and_theology
