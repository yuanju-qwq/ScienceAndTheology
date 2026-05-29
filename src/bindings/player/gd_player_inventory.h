#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "core/player/inventory.hpp"

namespace science_and_theology {

class GDPlayerInventory : public godot::Resource {
    GDCLASS(GDPlayerInventory, godot::Resource)

public:
    GDPlayerInventory();
    ~GDPlayerInventory() override;

    void init(int32_t width, int32_t height);

    int32_t get_width() const;
    int32_t get_height() const;
    int32_t get_slot_count() const;

    godot::Dictionary get_slot(int32_t index) const;

    void set_slot(int32_t index, int64_t item_id, int32_t count,
                  int32_t secondary_id = -1);

    int32_t add_item(int64_t item_id, int32_t count,
                     int32_t secondary_id = -1);

    bool remove_from_slot(int32_t index, int32_t count);

    void swap_slots(int32_t a, int32_t b);

    bool split_stack(int32_t src_index, int32_t dst_index);

    int32_t count_item(int64_t item_id) const;

    int32_t find_item(int64_t item_id, int32_t secondary_id = -1) const;

    bool has_enough(int64_t item_id, int32_t count) const;

    void clear();

    int32_t get_max_stack() const;
    void set_max_stack(int32_t value);

    gt::Inventory& get_inventory() { return inventory_; }
    const gt::Inventory& get_inventory() const { return inventory_; }

protected:
    static void _bind_methods();

private:
    static godot::Dictionary slot_to_dict(const gt::InventorySlot& slot);

    gt::Inventory inventory_;
};

} // namespace science_and_theology
