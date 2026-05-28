#include "gd_me_network.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>

VARIANT_ENUM_CAST(science_and_theology::GDMENetwork::MENodeTypeConst);

namespace science_and_theology {

using namespace godot;
using namespace gt;

GDMENetwork::GDMENetwork() = default;
GDMENetwork::~GDMENetwork() = default;

uint32_t GDMENetwork::add_node(int64_t type) {
    auto nt = static_cast<MENodeType>(static_cast<uint8_t>(type));
    return network_.add_node(nt);
}

bool GDMENetwork::remove_node(uint32_t id) {
    return network_.remove_node(id);
}

bool GDMENetwork::connect(uint32_t a, uint32_t b) {
    return network_.connect(a, b);
}

bool GDMENetwork::disconnect(uint32_t a, uint32_t b) {
    return network_.disconnect(a, b);
}

Array GDMENetwork::connected_nodes(uint32_t id) const {
    auto nodes = network_.find_connected_nodes(id);
    Array arr;
    for (auto n : nodes) arr.append(n);
    return arr;
}

void GDMENetwork::attach_storage_cell(uint32_t node_id,
                                      const Dictionary& cell_config) {
    int64_t byte_cap = cell_config.get("byte_capacity", 1024).operator int64_t();
    int max_types = cell_config.get("max_types", 63).operator int64_t();
    int64_t bpt = cell_config.get("bytes_per_type", 8).operator int64_t();

    auto cell = std::make_unique<StorageCell>(byte_cap, max_types, bpt);
    network_.attach_storage(node_id, std::move(cell));
}

void GDMENetwork::attach_external_storage(uint32_t node_id,
                                          const String& label,
                                          int64_t byte_capacity,
                                          const Callable& check_cb,
                                          const Callable& extract_cb,
                                          const Callable& insert_cb) {
    ExternalStorage::CheckCallback check = [check_cb](const ResourceId& rid) -> int64_t {
        Array args;
        args.append(static_cast<int64_t>(rid.raw_id));
        args.append(reinterpret_cast<int64_t>(rid.type));
        return check_cb.callv(args).operator int64_t();
    };

    ExternalStorage::ExtractCallback take = [extract_cb](const ResourceId& rid, int64_t amt) -> int64_t {
        Array args;
        args.append(static_cast<int64_t>(rid.raw_id));
        args.append(reinterpret_cast<int64_t>(rid.type));
        args.append(amt);
        return extract_cb.callv(args).operator int64_t();
    };

    ExternalStorage::InsertCallback put = [insert_cb](const ResourceId& rid, int64_t amt) -> int64_t {
        Array args;
        args.append(static_cast<int64_t>(rid.raw_id));
        args.append(reinterpret_cast<int64_t>(rid.type));
        args.append(amt);
        return insert_cb.callv(args).operator int64_t();
    };

    ExternalStorage::TypesCallback types_cb = [label]() -> std::vector<ResourceId> {
        return {};
    };

    auto ext = std::make_unique<ExternalStorage>(
        label.utf8().get_data(), byte_capacity,
        std::move(check), std::move(take), std::move(put),
        std::move(types_cb));
    network_.attach_storage(node_id, std::move(ext));
}

int64_t GDMENetwork::check_item(int64_t item_id, uint32_t context_node) const {
    ItemKey key(static_cast<ItemId>(item_id));
    return network_.check(key, context_node);
}

int64_t GDMENetwork::check_fluid(int64_t fluid_id, uint32_t context_node) const {
    FluidKey key(static_cast<FluidId>(fluid_id));
    return network_.check(key, context_node);
}

int64_t GDMENetwork::check_global_item(int64_t item_id) const {
    ItemKey key(static_cast<ItemId>(item_id));
    return network_.check_global(key);
}

int64_t GDMENetwork::extract_item(int64_t item_id, int64_t amount,
                                  uint32_t context_node) {
    ItemKey key(static_cast<ItemId>(item_id));
    return network_.extract(key, amount, context_node);
}

int64_t GDMENetwork::insert_item(int64_t item_id, int64_t amount,
                                 uint32_t context_node) {
    ItemKey key(static_cast<ItemId>(item_id));
    return network_.insert(key, amount, context_node);
}

void GDMENetwork::set_channel_count(uint32_t id, int64_t channels) {
    network_.set_channel_count(id, static_cast<int>(channels));
}

int64_t GDMENetwork::network_total_channels(uint32_t id) const {
    return static_cast<int64_t>(network_.network_total_channels(id));
}

int64_t GDMENetwork::network_online_devices(uint32_t id) const {
    return static_cast<int64_t>(network_.network_online_devices(id));
}

bool GDMENetwork::is_node_online(uint32_t id) const {
    return network_.is_node_online(id);
}

void GDMENetwork::_bind_methods() {
    ClassDB::bind_method(D_METHOD("add_node", "type"),
                         &GDMENetwork::add_node);
    ClassDB::bind_method(D_METHOD("remove_node", "id"),
                         &GDMENetwork::remove_node);
    ClassDB::bind_method(D_METHOD("connect", "a", "b"),
                         &GDMENetwork::connect);
    ClassDB::bind_method(D_METHOD("disconnect", "a", "b"),
                         &GDMENetwork::disconnect);
    ClassDB::bind_method(D_METHOD("connected_nodes", "id"),
                         &GDMENetwork::connected_nodes);
    ClassDB::bind_method(D_METHOD("attach_storage_cell", "node_id", "cell_config"),
                         &GDMENetwork::attach_storage_cell);
    ClassDB::bind_method(D_METHOD("attach_external_storage",
                         "node_id", "label", "byte_capacity",
                         "check_cb", "extract_cb", "insert_cb"),
                         &GDMENetwork::attach_external_storage);
    ClassDB::bind_method(D_METHOD("check_item", "item_id", "context_node"),
                         &GDMENetwork::check_item);
    ClassDB::bind_method(D_METHOD("check_fluid", "fluid_id", "context_node"),
                         &GDMENetwork::check_fluid);
    ClassDB::bind_method(D_METHOD("check_global_item", "item_id"),
                         &GDMENetwork::check_global_item);
    ClassDB::bind_method(D_METHOD("extract_item", "item_id", "amount", "context_node"),
                         &GDMENetwork::extract_item);
    ClassDB::bind_method(D_METHOD("insert_item", "item_id", "amount", "context_node"),
                         &GDMENetwork::insert_item);

    // Channel system.
    ClassDB::bind_method(D_METHOD("set_channel_count", "id", "channels"),
                         &GDMENetwork::set_channel_count);
    ClassDB::bind_method(D_METHOD("network_total_channels", "id"),
                         &GDMENetwork::network_total_channels);
    ClassDB::bind_method(D_METHOD("network_online_devices", "id"),
                         &GDMENetwork::network_online_devices);
    ClassDB::bind_method(D_METHOD("is_node_online", "id"),
                         &GDMENetwork::is_node_online);

    // MENodeType constants for GDScript.
    BIND_ENUM_CONSTANT(NODE_CONTROLLER);
    BIND_ENUM_CONSTANT(NODE_SWITCH);
    BIND_ENUM_CONSTANT(NODE_DRIVE);
    BIND_ENUM_CONSTANT(NODE_STORAGE_BUS);
    BIND_ENUM_CONSTANT(NODE_INTERFACE);
    BIND_ENUM_CONSTANT(NODE_TERMINAL);
    BIND_ENUM_CONSTANT(NODE_CABLE);
}

} // namespace science_and_theology
