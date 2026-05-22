#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "core/gt/fluid/fluid_network.hpp"

namespace science_and_theology {

class GDFluidNetwork : public godot::Resource {
    GDCLASS(GDFluidNetwork, godot::Resource)

public:
    // Pipe type constants exposed to GDScript.
    enum PipeType {
        PIPE_LIQUID = 0,
        PIPE_GAS    = 1,
    };

    GDFluidNetwork();
    ~GDFluidNetwork() override;

    // --- Node lifecycle ---
    int64_t add_node(godot::Vector2i position, int pipe_type = PIPE_LIQUID);
    bool remove_node(int64_t node_id);
    godot::Dictionary get_node_info(int64_t node_id) const;
    int64_t get_node_count() const;

    // --- Edge lifecycle ---
    bool connect_nodes(int64_t node_a, int64_t node_b, int64_t max_flow_rate);
    bool disconnect_nodes(int64_t node_a, int64_t node_b);

    // --- Fluid type ---
    bool set_node_fluid_type(int64_t node_id, int fluid_type_id);
    int get_component_fluid_type(int64_t node_id) const;

    // --- Producer / consumer ---
    void set_producer(int64_t node_id, int64_t amount);
    void set_consumer(int64_t node_id, int64_t demand);

    // --- Network update ---
    void update_network();

    // --- Topology queries ---
    godot::PackedInt32Array find_connected_component(int64_t start_id) const;
    bool are_connected(int64_t node_a, int64_t node_b) const;

    // --- Flow queries ---
    int64_t get_available_flow(int64_t node_id) const;
    int64_t get_component_total_production(int64_t node_id) const;
    int64_t get_component_total_demand(int64_t node_id) const;

    // How much fluid was delivered to this node in the last tick.
    int64_t get_delivered(int64_t node_id) const;

    void clear();

    // --- Static helpers ---
    static godot::Dictionary get_fluid_info(int fluid_id);
    static godot::Dictionary get_fluid_by_name(const godot::String& name);
    static int get_fluid_id(const godot::String& name);
    static godot::PackedStringArray get_all_fluid_names();
    static int get_fluid_count();

protected:
    static void _bind_methods();

private:
    gt::FluidNetwork network_;

    static gt::MapPosition _from_godot(godot::Vector2i pos);
    static godot::Vector2i _to_godot(const gt::MapPosition& pos);
};

} // namespace science_and_theology