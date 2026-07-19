#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <unordered_map>

#include "core/player/player_handle.hpp"
#include "core/player/player_manager.hpp"
#include "core/player/tool_def.hpp"

namespace science_and_theology {

class GDPlayerEquipment;
class GDPlayerInventory;
class GDWorldData;

class GDGameCommandServer : public godot::Node {
    GDCLASS(GDGameCommandServer, godot::Node)

public:
    GDGameCommandServer() = default;
    ~GDGameCommandServer() override = default;

    void set_world_data(godot::Resource* world);
    godot::Resource* get_world_data() const;

    // Register or update a player with an explicit id.
    // Single-player mode uses player_handle = 1 (kSinglePlayerHandle).
    // If the player is already registered, rebinds the inventory/equipment
    // pointers (upsert). Returns false only if player_handle is invalid.
    bool register_player(int64_t player_handle,
                         godot::Resource* inventory,
                         godot::Resource* equipment);

    // Unregister a player (multi-player).
    bool unregister_player(int64_t player_handle);

    // Returns the number of registered players.
    int64_t get_player_count() const;

    godot::Dictionary submit_command(const godot::Dictionary& command);

    // Register a custom command handler from GDScript (for content packs).
    // command_name must be unique; returns false if already registered.
    // The callback receives the full command Dictionary and must return
    // a Dictionary with at least {"ok": bool}.
    bool register_command(const godot::String& command_name,
                          const godot::Callable& callback);

    // Unregister a custom command handler.
    bool unregister_command(const godot::String& command_name);

protected:
    static void _bind_methods();

private:
    static constexpr int32_t kSecondaryNone = -1;

    // Resolve the player for the current command. Reads "player_handle" from
    // the command dict (default kSinglePlayerHandle). Sets current_player_.
    // Returns nullptr and emits a rejection if the player is not registered.
    PlayerState* resolve_command_player(const godot::Dictionary& command,
                                        const godot::StringName& command_type);

    godot::Dictionary cmd_mine_block(const godot::Dictionary& command);
    godot::Dictionary cmd_place_block(const godot::Dictionary& command);
    godot::Dictionary cmd_add_inventory_item(const godot::Dictionary& command);
    godot::Dictionary cmd_remove_inventory_item(const godot::Dictionary& command);
    godot::Dictionary cmd_craft_recipe(const godot::Dictionary& command);
    godot::Dictionary cmd_place_object(const godot::Dictionary& command);
    godot::Dictionary cmd_till_farmland(const godot::Dictionary& command);
    godot::Dictionary cmd_plant_crop(const godot::Dictionary& command);
    godot::Dictionary cmd_harvest_crop(const godot::Dictionary& command);
    godot::Dictionary cmd_fertilize(const godot::Dictionary& command);

    // TFC expansion commands
    godot::Dictionary cmd_forage_wild(const godot::Dictionary& command);
    godot::Dictionary cmd_knapping_pickup(const godot::Dictionary& command);

    // Helpers operate on current_player_ (set by resolve_command_player).
    int32_t add_inventory_item(int64_t item_id, int32_t count,
                               int32_t secondary_id = kSecondaryNone,
                               bool emit_sync = true);
    bool remove_inventory_item(int64_t item_id, int32_t count);
    bool inventory_has_item(int64_t item_id, int32_t count) const;

    bool is_world_object_occupied(const godot::StringName& object_type,
                                  const godot::StringName& dimension,
                                  const godot::Vector3i& cell) const;
    bool resolve_placement_cell(const godot::Dictionary& command,
                                const godot::StringName& dimension,
                                godot::Vector3i& out_cell,
                                godot::String& out_error) const;
    bool node_bool_call(godot::Node* node, const godot::StringName& method,
                        const godot::StringName& dimension,
                        const godot::Vector3i& cell) const;

    struct MiningEligibility {
        bool can_mine = false;
        bool can_drop = false;
        float effective_speed = 1.0f;
        float hardness = 1.0f;
        int32_t required_mining_level = 0;
    };

    MiningEligibility check_mining_eligibility(int32_t terrain_material) const;
    gt::ToolStats get_mining_stats() const;
    bool player_has_tool_named(const godot::String& tool_name) const;
    int64_t get_equipped_item() const;
    int32_t get_air_material_id() const;
    int32_t get_ladder_material_id() const;
    int32_t get_workbench_material_id() const;
    int32_t get_fence_material_id() const;
    int32_t get_farmland_material_id() const;
    int32_t get_material_id_by_key(const godot::String& key) const;
    int32_t get_dirt_material_id() const;

    godot::Array get_terrain_drops(int32_t terrain_material) const;
    static bool tool_name_matches(int64_t item_id, const godot::String& tool_lower);
    static bool command_recipe_matches_registered_recipe(const godot::Dictionary& recipe);

    static godot::StringName command_type(const godot::Dictionary& command);
    godot::Dictionary accept(const godot::Dictionary& data = godot::Dictionary()) const;
    godot::Dictionary reject(const godot::StringName& command_type,
                             const godot::String& reason);

    GDWorldData* world_data_ = nullptr;

    // Multi-player player registry. Owns PlayerState entries (raw pointers
    // to inventory/equipment are owned by the GDPlayerInventory /
    // GDPlayerEquipment Godot Resources).
    PlayerManager player_manager_;

    // The player resolved for the current submit_command call.
    // Set by resolve_command_player, used by the helper methods.
    PlayerState* current_player_ = nullptr;

    // Custom command handlers registered by content packs.
    // Keyed by command name (lowercased StringName).
    std::unordered_map<std::string, godot::Callable> custom_commands_;
};

} // namespace science_and_theology
