#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>

#include "core/player/tool_def.hpp"

namespace science_and_theology {

class GDPlayerEquipment;
class GDPlayerInventory;
class GDFurnaceManager;
class GDWorldData;

class GDGameCommandServer : public godot::Node {
    GDCLASS(GDGameCommandServer, godot::Node)

public:
    GDGameCommandServer() = default;
    ~GDGameCommandServer() override = default;

    void set_world_data(godot::Resource* world);
    godot::Resource* get_world_data() const;

    void configure_player(godot::Resource* inventory, godot::Resource* equipment);
    void set_workbench_manager(godot::Node* manager);
    void set_furnace_manager(godot::Node* manager);
    void set_ladder_manager(godot::Node* manager);

    godot::Dictionary submit_command(const godot::Dictionary& command);

protected:
    static void _bind_methods();

private:
    static constexpr int32_t kSecondaryNone = -1;

    godot::Dictionary cmd_mine_block(const godot::Dictionary& command);
    godot::Dictionary cmd_add_inventory_item(const godot::Dictionary& command);
    godot::Dictionary cmd_remove_inventory_item(const godot::Dictionary& command);
    godot::Dictionary cmd_craft_recipe(const godot::Dictionary& command);
    godot::Dictionary cmd_place_object(const godot::Dictionary& command);
    godot::Dictionary cmd_furnace_take_output(const godot::Dictionary& command);
    godot::Dictionary cmd_furnace_insert_input(const godot::Dictionary& command);
    godot::Dictionary cmd_furnace_insert_fuel(const godot::Dictionary& command);
    godot::Dictionary sync_furnace(
        const godot::StringName& dimension, const godot::Vector3i& cell,
        const char* reason);

    int32_t add_inventory_item(int64_t item_id, int32_t count,
                               int32_t secondary_id = kSecondaryNone,
                               bool emit_sync = true);
    bool remove_inventory_item(int64_t item_id, int32_t count);
    bool inventory_has_item(int64_t item_id, int32_t count) const;

    bool is_world_object_occupied(const godot::StringName& object_type,
                                  const godot::StringName& dimension,
                                  const godot::Vector3i& cell) const;
    bool node_bool_call(godot::Node* node, const godot::StringName& method,
                        const godot::StringName& dimension,
                        const godot::Vector3i& cell) const;

    bool has_required_tool(int32_t terrain_material) const;
    bool player_has_tool_named(const godot::String& tool_name) const;
    int64_t get_equipped_item() const;
    int32_t get_air_material_id() const;

    godot::Array get_terrain_drops(int32_t terrain_material) const;
    static bool get_tool_stats_for_item(int64_t item_id, gt::ToolStats& out_stats);
    static bool tool_name_matches(int64_t item_id, const godot::String& tool_lower);
    static bool command_recipe_matches_registered_recipe(const godot::Dictionary& recipe);

    static godot::StringName command_type(const godot::Dictionary& command);
    godot::Dictionary accept(const godot::Dictionary& data = godot::Dictionary()) const;
    godot::Dictionary reject(const godot::StringName& command_type,
                             const godot::String& reason);

    GDWorldData* world_data_ = nullptr;
    GDPlayerInventory* inventory_ = nullptr;
    GDPlayerEquipment* equipment_ = nullptr;
    godot::Node* workbench_manager_ = nullptr;
    godot::Node* furnace_manager_ = nullptr;
    GDFurnaceManager* furnace_manager_cpp_ = nullptr;
    godot::Node* ladder_manager_ = nullptr;
};

} // namespace science_and_theology
