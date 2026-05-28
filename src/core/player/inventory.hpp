#pragma once

#include <cstdint>
#include <vector>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

struct InventorySlot {
    ItemId item_id = kInvalidItemId;
    int32_t count = 0;
    int32_t durability = -1;

    bool is_empty() const {
        return count <= 0 || item_id == kInvalidItemId;
    }

    void clear() {
        item_id = kInvalidItemId;
        count = 0;
        durability = -1;
    }
};

class Inventory {
public:
    static constexpr int32_t kDefaultWidth = 9;
    static constexpr int32_t kDefaultHeight = 4;
    static constexpr int32_t kDefaultMaxStack = 64;

    Inventory();
    Inventory(int32_t width, int32_t height);

    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    int32_t slot_count() const { return width_ * height_; }

    const InventorySlot& get_slot(int32_t index) const;
    bool set_slot(int32_t index, ItemId item_id, int32_t count,
                  int32_t durability = -1);

    int32_t add_item(ItemId item_id, int32_t count, int32_t durability = -1);

    bool remove_from_slot(int32_t index, int32_t count);

    void swap_slots(int32_t a, int32_t b);

    bool split_stack(int32_t src_index, int32_t dst_index);

    int32_t count_item(ItemId item_id) const;

    int32_t find_item(ItemId item_id) const;

    bool has_enough(ItemId item_id, int32_t count) const;

    void clear();

    const std::vector<InventorySlot>& slots() const { return slots_; }

    int32_t max_stack() const { return max_stack_; }
    void set_max_stack(int32_t value) { max_stack_ = value; }

private:
    int32_t width_;
    int32_t height_;
    std::vector<InventorySlot> slots_;
    int32_t max_stack_ = kDefaultMaxStack;

    int32_t find_target_slot(ItemId item_id) const;
};

} // namespace science_and_theology::gt
