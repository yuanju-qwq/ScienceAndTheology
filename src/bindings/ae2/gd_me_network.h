#pragma once

#include <memory>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/ae2/ae2_me_network.hpp"
#include "core/ae2/ae2_storage_cell.hpp"

namespace science_and_theology {

// GDScript wrapper for the ME Network.
class GDMENetwork : public godot::Object {
    GDCLASS(GDMENetwork, godot::Object)

public:
    // MENodeType enum for GDScript.
    enum MENodeTypeConst {
        NODE_CONTROLLER = static_cast<int>(gt::MENodeType::Controller),
        NODE_SWITCH     = static_cast<int>(gt::MENodeType::Switch),
        NODE_DRIVE      = static_cast<int>(gt::MENodeType::Drive),
        NODE_STORAGE_BUS = static_cast<int>(gt::MENodeType::StorageBus),
        NODE_INTERFACE  = static_cast<int>(gt::MENodeType::Interface),
        NODE_TERMINAL   = static_cast<int>(gt::MENodeType::Terminal),
        NODE_CABLE      = static_cast<int>(gt::MENodeType::Cable),
    };
    GDMENetwork();
    ~GDMENetwork() override;

    uint32_t add_node(int64_t type);
    bool remove_node(uint32_t id);
    bool connect(uint32_t a, uint32_t b);
    bool disconnect(uint32_t a, uint32_t b);
    godot::Array connected_nodes(uint32_t id) const;

    // Storage attachment.
    // cell_config: Dictionary { byte_capacity: int, max_types: int }
    void attach_storage_cell(uint32_t node_id, const godot::Dictionary& cell_config);

    // External storage attachment via callbacks.
    // check_cb/extract_cb/insert_cb: Callable
    void attach_external_storage(uint32_t node_id,
                                 const godot::String& label,
                                 int64_t byte_capacity,
                                 const godot::Callable& check_cb,
                                 const godot::Callable& extract_cb,
                                 const godot::Callable& insert_cb);

    // Query.
    int64_t check_item(int64_t item_id, uint32_t context_node) const;
    int64_t check_fluid(int64_t fluid_id, uint32_t context_node) const;
    int64_t check_global_item(int64_t item_id) const;
    int64_t extract_item(int64_t item_id, int64_t amount, uint32_t context_node);
    int64_t insert_item(int64_t item_id, int64_t amount, uint32_t context_node);

    // --- Channel system ---
    void set_channel_count(uint32_t id, int64_t channels);
    int64_t network_total_channels(uint32_t id) const;
    int64_t network_online_devices(uint32_t id) const;
    bool is_node_online(uint32_t id) const;

    gt::MENetwork* network() { return &network_; }
    const gt::MENetwork* network() const { return &network_; }

protected:
    static void _bind_methods();

private:
    gt::MENetwork network_;
    int64_t next_cell_id_ = 1;
};

} // namespace science_and_theology
