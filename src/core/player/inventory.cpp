#include "inventory.hpp"

#include <algorithm>

namespace science_and_theology::gt {

Inventory::Inventory()
    : width_(kDefaultWidth), height_(kDefaultHeight) {
    slots_.resize(static_cast<size_t>(width_ * height_));
}

Inventory::Inventory(int32_t width, int32_t height)
    : width_(width), height_(height) {
    slots_.resize(static_cast<size_t>(width * height));
}

const InventorySlot& Inventory::get_slot(int32_t index) const {
    if (index < 0 || index >= slot_count()) {
        static InventorySlot empty;
        return empty;
    }
    return slots_[static_cast<size_t>(index)];
}

bool Inventory::set_slot(int32_t index, ItemId item_id, int32_t count,
                          int32_t secondary_id) {
    if (index < 0 || index >= slot_count()) return false;
    if (count < 0) return false;
    if (count == 0 || item_id == kInvalidItemId) {
        slots_[static_cast<size_t>(index)].clear();
        return true;
    }
    auto& slot = slots_[static_cast<size_t>(index)];
    // Items with secondary data are non-stackable (max count = 1)
    int32_t effective_max = (secondary_id != kNoSecondaryData) ? 1 : max_stack_;
    slot.item_id = item_id;
    slot.count = std::min(count, effective_max);
    slot.secondary_id = secondary_id;
    return true;
}

int32_t Inventory::add_item(ItemId item_id, int32_t count,
                             int32_t secondary_id) {
    if (count <= 0 || item_id == kInvalidItemId) return 0;

    int32_t remaining = count;
    bool has_secondary = (secondary_id != kNoSecondaryData);

    // Try existing stacks first — only merge if can_stack() matches
    // (secondary items never match can_stack, so they skip this step)
    if (!has_secondary) {
        for (auto& slot : slots_) {
            if (slot.is_empty()) continue;
            if (!can_stack(slot.item_id, slot.secondary_id,
                           item_id, secondary_id)) continue;
            if (slot.count >= max_stack_) continue;

            int32_t space = max_stack_ - slot.count;
            int32_t to_add = std::min(space, remaining);
            slot.count += to_add;
            remaining -= to_add;
            if (remaining <= 0) return 0;
        }
    }

    // Fill empty slots (secondary items get count=1 per slot)
    for (auto& slot : slots_) {
        if (!slot.is_empty()) continue;

        int32_t to_add = has_secondary ? 1 : std::min(max_stack_, remaining);
        slot.item_id = item_id;
        slot.count = to_add;
        slot.secondary_id = secondary_id;
        remaining -= to_add;
        if (remaining <= 0) return 0;
    }

    return remaining;
}

bool Inventory::remove_from_slot(int32_t index, int32_t count) {
    if (index < 0 || index >= slot_count()) return false;
    auto& slot = slots_[static_cast<size_t>(index)];
    if (slot.count < count) return false;

    slot.count -= count;
    if (slot.count <= 0) {
        slot.clear();
    }
    return true;
}

void Inventory::swap_slots(int32_t a, int32_t b) {
    if (a < 0 || a >= slot_count() || b < 0 || b >= slot_count()) return;
    std::swap(slots_[static_cast<size_t>(a)], slots_[static_cast<size_t>(b)]);
}

bool Inventory::split_stack(int32_t src_index, int32_t dst_index) {
    if (src_index < 0 || src_index >= slot_count()) return false;
    if (dst_index < 0 || dst_index >= slot_count()) return false;

    auto& src = slots_[static_cast<size_t>(src_index)];
    if (src.count < 2) return false;

    auto& dst = slots_[static_cast<size_t>(dst_index)];
    if (!dst.is_empty()) return false;

    int32_t half = src.count / 2;
    dst.item_id = src.item_id;
    dst.count = half;
    dst.secondary_id = src.secondary_id;
    src.count -= half;
    return true;
}

int32_t Inventory::count_item(ItemId item_id) const {
    int32_t total = 0;
    for (const auto& slot : slots_) {
        if (slot.item_id == item_id) {
            total += slot.count;
        }
    }
    return total;
}

int32_t Inventory::find_item(ItemId item_id, int32_t secondary_id) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        const auto& slot = slots_[i];
        if (slot.count <= 0) continue;
        if (slot.item_id != item_id) continue;
        if (secondary_id != kNoSecondaryData &&
            slot.secondary_id != secondary_id) continue;
        return static_cast<int32_t>(i);
    }
    return -1;
}

bool Inventory::has_enough(ItemId item_id, int32_t count) const {
    return count_item(item_id) >= count;
}

void Inventory::clear() {
    for (auto& slot : slots_) {
        slot.clear();
    }
}

int32_t Inventory::find_target_slot(ItemId item_id,
                                     int32_t secondary_id) const {
    // Find first slot with matching item_id + secondary_id and space
    for (size_t i = 0; i < slots_.size(); ++i) {
        const auto& slot = slots_[i];
        if (slot.is_empty()) continue;
        if (!can_stack(slot.item_id, slot.secondary_id,
                       item_id, secondary_id)) continue;
        if (slot.count < max_stack_) {
            return static_cast<int32_t>(i);
        }
    }
    // Find first empty slot
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].is_empty()) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

} // namespace science_and_theology::gt
