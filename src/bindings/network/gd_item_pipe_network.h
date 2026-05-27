#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/network/item_pipe_network.hpp"

namespace science_and_theology {

class GDItemPipeNetwork : public godot::Resource {
    GDCLASS(GDItemPipeNetwork, godot::Resource)

public:
    GDItemPipeNetwork();
    ~GDItemPipeNetwork() override;

    // --- Node lifecycle ---
    int64_t add_node(godot::Vector2i position);
    bool remove_node(int64_t node_id);
    godot::Dictionary get_node_info(int64_t node_id) const;
    int64_t get_node_count() const;

    // --- Edge lifecycle ---
    bool connect_nodes(int64_t node_a, int64_t node_b,
                       int64_t max_items_per_tick);
    bool disconnect_nodes(int64_t node_a, int64_t node_b);

    // --- Item insertion / extraction ---
    int64_t insert(int64_t node_id, int item_id, int64_t count);
    int64_t extract(int64_t node_id, int item_id, int64_t requested);
    int64_t count_item(int64_t node_id, int item_id) const;
    int64_t count_total_items(int64_t node_id) const;

    // --- Topology ---
    godot::PackedInt32Array find_connected_component(int64_t start_id) const;
    bool are_connected(int64_t node_a, int64_t node_b) const;

    // --- Network update ---
    void update_network();

    // --- Source / sink ---
    void set_source(int64_t node_id, bool is_source);
    void set_sink(int64_t node_id, bool is_sink);

    void clear();

protected:
    static void _bind_methods();

private:
    gt::ItemPipeNetwork network_;

    static gt::MapPosition _from_godot(godot::Vector2i pos);
    static godot::Vector2i _to_godot(const gt::MapPosition& pos);
};

} // namespace science_and_theology