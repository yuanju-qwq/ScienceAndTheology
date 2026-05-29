#include "gd_player_inventory.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

GDPlayerInventory::GDPlayerInventory()
    : inventory_(gt::Inventory::kDefaultWidth, gt::Inventory::kDefaultHeight) {
}

GDPlayerInventory::~GDPlayerInventory() = default;

void GDPlayerInventory::init(int32_t width, int32_t height) {
    inventory_ = gt::Inventory(width, height);
}

int32_t GDPlayerInventory::get_width() const {
    return inventory_.width();
}

int32_t GDPlayerInventory::get_height() const {
    return inventory_.height();
}

int32_t GDPlayerInventory::get_slot_count() const {
    return inventory_.slot_count();
}

Dictionary GDPlayerInventory::get_slot(int32_t index) const {
    return slot_to_dict(inventory_.get_slot(index));
}

void GDPlayerInventory::set_slot(int32_t index, int64_t item_id,
                                  int32_t count, int32_t secondary_id) {
    inventory_.set_slot(index, static_cast<gt::ItemId>(item_id),
                        count, secondary_id);
}

int32_t GDPlayerInventory::add_item(int64_t item_id, int32_t count,
                                     int32_t secondary_id) {
    return inventory_.add_item(static_cast<gt::ItemId>(item_id),
                               count, secondary_id);
}

bool GDPlayerInventory::remove_from_slot(int32_t index, int32_t count) {
    return inventory_.remove_from_slot(index, count);
}

void GDPlayerInventory::swap_slots(int32_t a, int32_t b) {
    inventory_.swap_slots(a, b);
}

bool GDPlayerInventory::split_stack(int32_t src_index, int32_t dst_index) {
    return inventory_.split_stack(src_index, dst_index);
}

int32_t GDPlayerInventory::count_item(int64_t item_id) const {
    return inventory_.count_item(static_cast<gt::ItemId>(item_id));
}

int32_t GDPlayerInventory::find_item(int64_t item_id, int32_t secondary_id) const {
    return inventory_.find_item(static_cast<gt::ItemId>(item_id), secondary_id);
}

bool GDPlayerInventory::has_enough(int64_t item_id, int32_t count) const {
    return inventory_.has_enough(static_cast<gt::ItemId>(item_id), count);
}

void GDPlayerInventory::clear() {
    inventory_.clear();
}

int32_t GDPlayerInventory::get_max_stack() const {
    return inventory_.max_stack();
}

void GDPlayerInventory::set_max_stack(int32_t value) {
    inventory_.set_max_stack(value);
}

Dictionary GDPlayerInventory::slot_to_dict(const gt::InventorySlot& slot) {
    Dictionary d;
    if (slot.is_empty()) {
        d["item_id"] = 0;
        d["count"] = 0;
        d["secondary_id"] = -1;
        d["empty"] = true;
    } else {
        d["item_id"] = static_cast<int64_t>(slot.item_id);
        d["count"] = slot.count;
        d["secondary_id"] = slot.secondary_id;
        d["empty"] = false;
    }
    return d;
}

void GDPlayerInventory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("init", "width", "height"),
                         &GDPlayerInventory::init);
    ClassDB::bind_method(D_METHOD("get_width"),
                         &GDPlayerInventory::get_width);
    ClassDB::bind_method(D_METHOD("get_height"),
                         &GDPlayerInventory::get_height);
    ClassDB::bind_method(D_METHOD("get_slot_count"),
                         &GDPlayerInventory::get_slot_count);

    ClassDB::bind_method(D_METHOD("get_slot", "index"),
                         &GDPlayerInventory::get_slot);
    ClassDB::bind_method(D_METHOD("set_slot", "index", "item_id",
        "count", "secondary_id"),
        &GDPlayerInventory::set_slot, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("add_item", "item_id", "count",
        "secondary_id"),
        &GDPlayerInventory::add_item, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("remove_from_slot", "index", "count"),
                         &GDPlayerInventory::remove_from_slot);
    ClassDB::bind_method(D_METHOD("swap_slots", "a", "b"),
                         &GDPlayerInventory::swap_slots);
    ClassDB::bind_method(D_METHOD("split_stack", "src_index", "dst_index"),
                         &GDPlayerInventory::split_stack);
    ClassDB::bind_method(D_METHOD("count_item", "item_id"),
                         &GDPlayerInventory::count_item);
    ClassDB::bind_method(D_METHOD("find_item", "item_id", "secondary_id"),
                         &GDPlayerInventory::find_item, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("has_enough", "item_id", "count"),
                         &GDPlayerInventory::has_enough);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDPlayerInventory::clear);
    ClassDB::bind_method(D_METHOD("get_max_stack"),
                         &GDPlayerInventory::get_max_stack);
    ClassDB::bind_method(D_METHOD("set_max_stack", "value"),
                         &GDPlayerInventory::set_max_stack);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_stack"),
                 "set_max_stack", "get_max_stack");
}

} // namespace science_and_theology
