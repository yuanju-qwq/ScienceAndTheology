#include "gd_game_command_server.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "bindings/player/gd_player_equipment.h"
#include "bindings/player/gd_player_inventory.h"
#include "bindings/world/gd_furnace_manager.h"
#include "bindings/world/gd_world_data.h"
#include "bindings/crafting/gd_crafting.h"
#include "core/crafting/crafting.hpp"
#include "core/fuel/fuel_registry.hpp"
#include "core/material/material_item.hpp"
#include "core/universe/planet_build_frame.hpp"
#include "core/world/world_data.hpp"

namespace science_and_theology {

using namespace godot;

namespace {

StringName command_mine_block() { return StringName("mine_block"); }
StringName command_add_inventory_item() { return StringName("add_inventory_item"); }
StringName command_remove_inventory_item() { return StringName("remove_inventory_item"); }
StringName command_craft_recipe() { return StringName("craft_recipe"); }
StringName command_place_object() { return StringName("place_object"); }
StringName command_remove_object() { return StringName("remove_object"); }
StringName command_furnace_take_output() { return StringName("furnace_take_output"); }
StringName command_furnace_insert_input() { return StringName("furnace_insert_input"); }
StringName command_furnace_insert_fuel() { return StringName("furnace_insert_fuel"); }

StringName command_till_farmland() { return StringName("till_farmland"); }
StringName command_plant_crop() { return StringName("plant_crop"); }
StringName command_harvest_crop() { return StringName("harvest_crop"); }
StringName command_fertilize() { return StringName("fertilize"); }

StringName command_forage_wild() { return StringName("forage_wild"); }
StringName command_knapping_pickup() { return StringName("knapping_pickup"); }

StringName object_workbench() { return StringName("workbench"); }
StringName object_furnace() { return StringName("furnace"); }
StringName object_ladder() { return StringName("ladder"); }
StringName object_fence() { return StringName("fence"); }

Dictionary stack_dict(int64_t item_id, int32_t count) {
    Dictionary drop;
    drop["item_id"] = item_id;
    drop["count"] = count;
    return drop;
}

bool tool_tag_matches(const std::string& required_tool_tag,
                      gt::ToolType equipped_type) {
    if (required_tool_tag.empty()) {
        return true;
    }
    if (required_tool_tag == "pickaxe") {
        return equipped_type == gt::ToolType::PICKAXE;
    }
    if (required_tool_tag == "axe") {
        return equipped_type == gt::ToolType::AXE;
    }
    if (required_tool_tag == "shovel") {
        return equipped_type == gt::ToolType::SHOVEL;
    }
    if (required_tool_tag == "sword") {
        return equipped_type == gt::ToolType::SWORD;
    }
    return false;
}

bool recipe_stack_matches_dictionary(const gt::ResourceStack& stack,
                                     const Dictionary& dict) {
    return stack.is_item()
        && static_cast<int64_t>(stack.item_id()) == static_cast<int64_t>(dict.get("item_id", 0))
        && stack.amount == static_cast<int64_t>(dict.get("count", 0));
}

bool recipe_matches_dictionary(const gt::CraftingRecipe& recipe,
                               const Dictionary& dict) {
    if (String(recipe.name) != String(dict.get("name", ""))) return false;
    if (String(recipe.required_station) != String(dict.get("required_station", ""))) return false;
    if (static_cast<int64_t>(recipe.output.item_id()) !=
            static_cast<int64_t>(dict.get("output_item_id", 0))) {
        return false;
    }
    if (recipe.output.amount != static_cast<int64_t>(dict.get("output_count", 0))) {
        return false;
    }

    const String required_tool =
        recipe.required_tool != nullptr ? String(recipe.required_tool) : String("");
    if (required_tool != String(dict.get("required_tool", ""))) return false;

    Array inputs = dict.get("inputs", Array());
    if (inputs.size() != static_cast<int64_t>(recipe.inputs.size())) return false;

    for (const auto& input : recipe.inputs) {
        bool found = false;
        for (int64_t i = 0; i < inputs.size(); ++i) {
            Variant entry_v = inputs[i];
            if (entry_v.get_type() != Variant::DICTIONARY) continue;
            Dictionary entry = entry_v;
            if (recipe_stack_matches_dictionary(input, entry)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    return true;
}

bool direction_from_offset(const Vector3i& offset, Direction& out_direction) {
    if (offset == Vector3i(1, 0, 0)) {
        out_direction = Direction::PosX;
    } else if (offset == Vector3i(-1, 0, 0)) {
        out_direction = Direction::NegX;
    } else if (offset == Vector3i(0, 1, 0)) {
        out_direction = Direction::PosY;
    } else if (offset == Vector3i(0, -1, 0)) {
        out_direction = Direction::NegY;
    } else if (offset == Vector3i(0, 0, 1)) {
        out_direction = Direction::PosZ;
    } else if (offset == Vector3i(0, 0, -1)) {
        out_direction = Direction::NegZ;
    } else {
        return false;
    }
    return true;
}

} // namespace

void GDGameCommandServer::set_world_data(Resource* world) {
    world_data_ = Object::cast_to<GDWorldData>(world);
    if (world != nullptr && world_data_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: set_world_data received non-GDWorldData resource");
    }
}

Resource* GDGameCommandServer::get_world_data() const {
    return world_data_;
}

bool GDGameCommandServer::register_player(int64_t player_id,
                                          Resource* inventory,
                                          Resource* equipment) {
    PlayerId pid = static_cast<PlayerId>(player_id);
    if (pid == kInvalidPlayerId) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: register_player received invalid player_id 0");
        return false;
    }

    GDPlayerInventory* inv = Object::cast_to<GDPlayerInventory>(inventory);
    GDPlayerEquipment* eq = Object::cast_to<GDPlayerEquipment>(equipment);
    gt::Inventory* inv_ptr = inv ? &inv->get_inventory() : nullptr;
    gt::Equipment* eq_ptr = eq ? &eq->get_equipment() : nullptr;

    // Upsert: if the player is already registered, rebind the inventory
    // and equipment pointers (their underlying Godot Resources may have
    // been recreated). Otherwise create a new entry.
    if (player_manager_.has_player(pid)) {
        player_manager_.bind_inventory(pid, inv_ptr);
        player_manager_.bind_equipment(pid, eq_ptr);
        return true;
    }
    if (!player_manager_.register_player(pid, inv_ptr, eq_ptr)) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: register_player failed for player_id ",
            player_id);
        return false;
    }
    return true;
}

bool GDGameCommandServer::unregister_player(int64_t player_id) {
    PlayerId pid = static_cast<PlayerId>(player_id);
    return player_manager_.unregister_player(pid);
}

int64_t GDGameCommandServer::get_player_count() const {
    return static_cast<int64_t>(player_manager_.player_count());
}

PlayerState* GDGameCommandServer::resolve_command_player(
        const Dictionary& command, const StringName& command_type) {
    // Read player_id from the command. Default to kSinglePlayerId (1)
    // for backward compatibility with single-player callers that don't
    // include the field.
    const int64_t raw_id = static_cast<int64_t>(command.get("player_id",
        static_cast<int64_t>(kSinglePlayerId)));
    const PlayerId pid = static_cast<PlayerId>(raw_id);

    PlayerState* state = player_manager_.get_player(pid);
    if (state == nullptr) {
        reject(command_type, "player_id " + String::num_int64(raw_id) +
                             " is not registered");
        current_player_ = nullptr;
        return nullptr;
    }
    current_player_ = state;
    return state;
}

void GDGameCommandServer::set_furnace_manager(Node* manager) {
    furnace_manager_ = manager;
    furnace_manager_cpp_ = Object::cast_to<GDFurnaceManager>(manager);
    if (manager != nullptr && furnace_manager_cpp_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: furnace manager is not GDFurnaceManager; "
            "server-side furnace state will be unavailable");
    }
}

Dictionary GDGameCommandServer::submit_command(const Dictionary& command) {
    const StringName type = command_type(command);
    // Resolve the player for this command. The cmd_* methods rely on
    // current_player_ being set. Commands that don't touch player state
    // (none currently) could skip this, but all current commands do.
    if (resolve_command_player(command, type) == nullptr) {
        // resolve_command_player already emitted command_rejected.
        Dictionary result;
        result["ok"] = false;
        result["type"] = type;
        result["reason"] = "player not registered";
        return result;
    }
    if (type == command_mine_block()) return cmd_mine_block(command);
    if (type == command_add_inventory_item()) return cmd_add_inventory_item(command);
    if (type == command_remove_inventory_item()) return cmd_remove_inventory_item(command);
    if (type == command_craft_recipe()) return cmd_craft_recipe(command);
    if (type == command_place_object()) return cmd_place_object(command);
    if (type == command_remove_object()) return cmd_remove_object(command);
    if (type == command_furnace_take_output()) return cmd_furnace_take_output(command);
    if (type == command_furnace_insert_input()) return cmd_furnace_insert_input(command);
    if (type == command_furnace_insert_fuel()) return cmd_furnace_insert_fuel(command);
    if (type == command_till_farmland()) return cmd_till_farmland(command);
    if (type == command_plant_crop()) return cmd_plant_crop(command);
    if (type == command_harvest_crop()) return cmd_harvest_crop(command);
    if (type == command_fertilize()) return cmd_fertilize(command);
    if (type == command_forage_wild()) return cmd_forage_wild(command);
    if (type == command_knapping_pickup()) return cmd_knapping_pickup(command);

    // Custom mod commands.
    std::string type_str = String(type).utf8().get_data();
    auto it = custom_commands_.find(type_str);
    if (it != custom_commands_.end()) {
        const Callable& cb = it->second;
        if (cb.is_valid()) {
            Variant result = cb.call(command);
            if (result.get_type() == Variant::DICTIONARY) {
                return result;
            }
        }
        return reject(type, "custom command handler returned invalid result");
    }
    return reject(type, "unknown command");
}

bool GDGameCommandServer::register_command(const String& command_name,
                                             const Callable& callback) {
    if (command_name.is_empty() || !callback.is_valid()) {
        return false;
    }
    std::string key = command_name.utf8().get_data();
    if (custom_commands_.count(key) > 0) {
        return false;
    }
    custom_commands_[key] = callback;
    return true;
}

bool GDGameCommandServer::unregister_command(const String& command_name) {
    std::string key = command_name.utf8().get_data();
    return custom_commands_.erase(key) > 0;
}

Dictionary GDGameCommandServer::cmd_mine_block(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_mine_block(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i chunk = command.get("chunk", Vector3i());
    const Vector3i local = command.get("local", Vector3i());
    const int32_t expected_material =
        static_cast<int32_t>(static_cast<int64_t>(command.get("expected_material", -1)));

    Dictionary cell = world_data_->get_terrain_cell(
        String(dimension), chunk.x, chunk.y, chunk.z, local.x, local.y, local.z);
    if (cell.is_empty()) {
        return reject(command_mine_block(), "target cell is missing");
    }

    const int32_t material =
        static_cast<int32_t>(static_cast<int64_t>(cell.get("material", -1)));
    if (expected_material >= 0 && material != expected_material) {
        return reject(command_mine_block(), "target material changed");
    }
    const int32_t air_material = get_air_material_id();
    if (material == air_material) {
        return reject(command_mine_block(), "target cell is already air");
    }
    if (!static_cast<bool>(cell.get("is_mineable", false))) {
        return reject(command_mine_block(), "target cell is not mineable");
    }
    if (static_cast<bool>(cell.get("is_indestructible", false))) {
        return reject(command_mine_block(), "target cell is indestructible");
    }
    const auto eligibility = check_mining_eligibility(material);
    if (!eligibility.can_mine) {
        return reject(command_mine_block(), "mining level too low");
    }

    if (!world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, air_material)) {
        return reject(command_mine_block(), "failed to write terrain cell");
    }

    Array drops;
    if (eligibility.can_drop) {
        drops = get_terrain_drops(material);
    }
    for (int64_t i = 0; i < drops.size(); ++i) {
        Variant drop_v = drops[i];
        if (drop_v.get_type() != Variant::DICTIONARY) continue;
        Dictionary drop = drop_v;
        const int64_t item_id = drop.get("item_id", 0);
        const int32_t count = static_cast<int32_t>(static_cast<int64_t>(drop.get("count", 0)));
        if (item_id > 0 && count > 0) {
            add_inventory_item(item_id, count);
        }
    }

    emit_signal("terrain_cell_synced", dimension, chunk, local, material, air_material);

    // Enqueue a block physics event so BlockPhysicsSystem can check
    // gravity fall and collapse for neighboring blocks.
    {
        BlockPhysicsEvent evt;
        evt.dimension_id = String(dimension).utf8().get_data();
        evt.block_x = chunk.x * ChunkData::kChunkSize + local.x;
        evt.block_y = chunk.y * ChunkData::kChunkSize + local.y;
        evt.block_z = chunk.z * ChunkData::kChunkSize + local.z;
        world_data_->get_world_ptr()->push_physics_event(evt);
    }

    const float mine_time = eligibility.hardness / std::max(eligibility.effective_speed, 0.01f);

    Dictionary result;
    result["type"] = command_mine_block();
    result["material"] = material;
    result["drops"] = drops;
    result["mine_time"] = std::min(mine_time, 10.0f);
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_add_inventory_item(const Dictionary& command) {
    const int64_t item_id = command.get("item_id", 0);
    const int32_t count = static_cast<int32_t>(static_cast<int64_t>(command.get("count", 0)));
    const int32_t secondary_id =
        static_cast<int32_t>(static_cast<int64_t>(command.get("secondary_id", kSecondaryNone)));
    if (item_id <= 0 || count <= 0) {
        return reject(command_add_inventory_item(), "invalid item stack");
    }

    const int32_t overflow = add_inventory_item(item_id, count, secondary_id);
    Dictionary result;
    result["type"] = command_add_inventory_item();
    result["overflow"] = overflow;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_remove_inventory_item(const Dictionary& command) {
    const int64_t item_id = command.get("item_id", 0);
    const int32_t count = static_cast<int32_t>(static_cast<int64_t>(command.get("count", 0)));
    if (item_id <= 0 || count <= 0) {
        return reject(command_remove_inventory_item(), "invalid item stack");
    }
    if (!remove_inventory_item(item_id, count)) {
        return reject(command_remove_inventory_item(),
                      "inventory does not contain enough items");
    }
    emit_signal("inventory_synced");

    Dictionary result;
    result["type"] = command_remove_inventory_item();
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_craft_recipe(const Dictionary& command) {
    if (current_player_ == nullptr || current_player_->inventory == nullptr) {
        return reject(command_craft_recipe(), "inventory is not configured");
    }

    gt::Inventory* inv = current_player_->inventory;

    Dictionary recipe = command.get("recipe", Dictionary());
    if (recipe.is_empty()) {
        return reject(command_craft_recipe(), "recipe is missing");
    }
    if (!command_recipe_matches_registered_recipe(recipe)) {
        return reject(command_craft_recipe(), "recipe is not registered");
    }

    const String required_station = recipe.get("required_station", "");
    const String station = command.get("station", "");
    if (required_station != station) {
        return reject(command_craft_recipe(), "crafting station changed");
    }
    if (required_station == "workbench") {
        const StringName dimension = command.get("dimension", StringName());
        const Vector3i cell = command.get("cell", Vector3i());
        if (world_data_ == nullptr) {
            return reject(command_craft_recipe(), "world data is not available");
        }
        const int32_t workbench_material = get_workbench_material_id();
        if (workbench_material <= 0) {
            return reject(command_craft_recipe(), "workbench material is not registered");
        }
        bool near_workbench = false;
        const Vector3i offsets[] = {
            Vector3i(0, 0, 0), Vector3i(1, 0, 0), Vector3i(-1, 0, 0),
            Vector3i(0, 1, 0), Vector3i(0, -1, 0),
            Vector3i(0, 0, 1), Vector3i(0, 0, -1),
        };
        const int32_t chunk_size = 32;
        for (const Vector3i& offset : offsets) {
            const Vector3i neighbor = cell + offset;
            const Vector3i n_chunk(
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.x) / chunk_size)),
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.y) / chunk_size)),
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.z) / chunk_size)));
            const Vector3i n_local(
                neighbor.x - n_chunk.x * chunk_size,
                neighbor.y - n_chunk.y * chunk_size,
                neighbor.z - n_chunk.z * chunk_size);
            const godot::Dictionary neighbor_cell = world_data_->get_terrain_cell(
                String(dimension), n_chunk.x, n_chunk.y, n_chunk.z,
                n_local.x, n_local.y, n_local.z);
            const int32_t neighbor_material = static_cast<int32_t>(
                static_cast<int64_t>(neighbor_cell.get("material", -1)));
            if (neighbor_material == workbench_material) {
                near_workbench = true;
                break;
            }
        }
        if (!near_workbench) {
            return reject(command_craft_recipe(), "required workbench is not nearby");
        }
    }

    const int64_t out_id = recipe.get("output_item_id", 0);
    const int32_t out_count =
        static_cast<int32_t>(static_cast<int64_t>(recipe.get("output_count", 0)));
    if (out_id <= 0 || out_count <= 0) {
        return reject(command_craft_recipe(), "recipe has no output");
    }

    const String tool = recipe.get("required_tool", "");
    if (!tool.is_empty() && !player_has_tool_named(tool)) {
        return reject(command_craft_recipe(), "required crafting tool is missing");
    }

    Array inputs = recipe.get("inputs", Array());
    for (int64_t i = 0; i < inputs.size(); ++i) {
        Variant input_v = inputs[i];
        if (input_v.get_type() != Variant::DICTIONARY) {
            return reject(command_craft_recipe(), "recipe has invalid input");
        }
        Dictionary input = input_v;
        const int64_t in_id = input.get("item_id", 0);
        const int32_t in_count =
            static_cast<int32_t>(static_cast<int64_t>(input.get("count", 0)));
        if (in_id <= 0 || in_count <= 0) {
            return reject(command_craft_recipe(), "recipe has invalid input");
        }
        if (!inv->has_enough(static_cast<gt::ItemId>(in_id), in_count)) {
            return reject(command_craft_recipe(),
                          "inventory does not contain recipe inputs");
        }
    }

    for (int64_t i = 0; i < inputs.size(); ++i) {
        Dictionary input = inputs[i];
        const int64_t in_id = input.get("item_id", 0);
        const int32_t in_count =
            static_cast<int32_t>(static_cast<int64_t>(input.get("count", 0)));
        remove_inventory_item(in_id, in_count);
    }

    const int32_t overflow = add_inventory_item(out_id, out_count);
    Dictionary result;
    result["type"] = command_craft_recipe();
    result["item_id"] = out_id;
    result["crafted_count"] = out_count - overflow;
    result["overflow"] = overflow;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_place_object(const Dictionary& command) {
    const StringName object_type = command.get("object_type", StringName());
    const StringName dimension = command.get("dimension", StringName("overworld"));
    Vector3i cell;
    String placement_error;
    if (!resolve_placement_cell(command, dimension, cell, placement_error)) {
        return reject(command_place_object(), placement_error);
    }
    const int64_t item_id = command.get("item_id", 0);

    if (item_id <= 0) {
        return reject(command_place_object(), "placement item is invalid");
    }
    if (!inventory_has_item(item_id, 1)) {
        return reject(command_place_object(), "placement item is missing");
    }
    if (is_world_object_occupied(object_type, dimension, cell)) {
        return reject(command_place_object(), "target cell is already occupied");
    }

    if (!remove_inventory_item(item_id, 1)) {
        return reject(command_place_object(), "failed to consume placement item");
    }

    bool placed = false;
    if (object_type == object_workbench()) {
        // Workbenches are placed as terrain cells (snt:workbench material).
        if (world_data_ == nullptr) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "world data is not available");
        }
        const int32_t workbench_material = get_workbench_material_id();
        if (workbench_material <= 0) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "workbench material is not registered");
        }
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        const int32_t air_material = get_air_material_id();
        if (existing_material != air_material) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "target cell is not air");
        }
        placed = world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, workbench_material);
        if (placed) {
            emit_signal("terrain_cell_synced", dimension, chunk, local,
                        air_material, workbench_material);
        }
    } else if (object_type == object_furnace()) {
        if (furnace_manager_cpp_ == nullptr) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "C++ furnace manager is not available");
        }
        placed = furnace_manager_cpp_->place_furnace(dimension, cell);
        // Register furnace as a MACHINE block entity in the voxel grid
        // so the network/multiblock/render systems can query it.
        if (placed && world_data_ != nullptr &&
            world_data_->get_world_ptr() != nullptr) {
            world_data_->get_world_ptr()->block_entity_registry()
                .register_machine_entity(
                    std::string(String(dimension).utf8().get_data()),
                    cell.x, cell.y, cell.z,
                    "furnace", /*facing=*/0);
        }
    } else if (object_type == object_ladder()) {
        // Ladders are placed as terrain cells (snt:ladder material).
        if (world_data_ == nullptr) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "world data is not available");
        }
        const int32_t ladder_material = get_ladder_material_id();
        if (ladder_material <= 0) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "ladder material is not registered");
        }
        // Compute chunk/local coordinates from the cell position.
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        // Check that the target cell is currently air.
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        const int32_t air_material = get_air_material_id();
        if (existing_material != air_material) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "target cell is not air");
        }
        // Ladders require at least one adjacent solid wall (horizontal neighbors).
        const Vector3i horizontal_offsets[] = {
            Vector3i(1, 0, 0), Vector3i(-1, 0, 0),
            Vector3i(0, 0, 1), Vector3i(0, 0, -1),
        };
        bool has_wall = false;
        for (const Vector3i& offset : horizontal_offsets) {
            const Vector3i neighbor = cell + offset;
            const Vector3i n_chunk(
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.x) / chunk_size)),
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.y) / chunk_size)),
                static_cast<int32_t>(floorf(static_cast<float>(neighbor.z) / chunk_size)));
            const Vector3i n_local(
                neighbor.x - n_chunk.x * chunk_size,
                neighbor.y - n_chunk.y * chunk_size,
                neighbor.z - n_chunk.z * chunk_size);
            const godot::Dictionary neighbor_cell = world_data_->get_terrain_cell(
                String(dimension), n_chunk.x, n_chunk.y, n_chunk.z,
                n_local.x, n_local.y, n_local.z);
            const int32_t neighbor_material = static_cast<int32_t>(
                static_cast<int64_t>(neighbor_cell.get("material", -1)));
            if (neighbor_material != air_material && neighbor_material > 0) {
                has_wall = true;
                break;
            }
        }
        if (!has_wall) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "ladder requires an adjacent wall");
        }
        placed = world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, ladder_material);
        if (placed) {
            emit_signal("terrain_cell_synced", dimension, chunk, local,
                        air_material, ladder_material);
        }
    } else if (object_type == object_fence()) {
        // Fences are placed as terrain cells (snt:fence material).
        // Used to enclose areas for captive creature husbandry.
        if (world_data_ == nullptr) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "world data is not available");
        }
        const int32_t fence_material = get_fence_material_id();
        if (fence_material <= 0) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "fence material is not registered");
        }
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        const int32_t air_material = get_air_material_id();
        if (existing_material != air_material) {
            add_inventory_item(item_id, 1, kSecondaryNone, false);
            return reject(command_place_object(), "target cell is not air");
        }
        placed = world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, fence_material);
        if (placed) {
            emit_signal("terrain_cell_synced", dimension, chunk, local,
                        air_material, fence_material);
        }
    } else {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_place_object(), "unknown object type");
    }

    if (!placed) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_place_object(), "world object could not be placed");
    }

    // Enqueue a block physics event so BlockPhysicsSystem can check
    // if the newly placed block should fall (e.g., sand placed in mid-air).
    if (world_data_ != nullptr && world_data_->get_world_ptr() != nullptr) {
        BlockPhysicsEvent evt;
        evt.dimension_id = String(dimension).utf8().get_data();
        evt.block_x = cell.x;
        evt.block_y = cell.y;
        evt.block_z = cell.z;
        world_data_->get_world_ptr()->push_physics_event(evt);
    }

    emit_signal("inventory_synced");
    emit_signal("world_object_synced", object_type, StringName("placed"), dimension, cell);
    if (object_type == object_furnace()) {
        sync_furnace(dimension, cell, "place_object");
    }

    Dictionary result;
    result["type"] = command_place_object();
    result["object_type"] = object_type;
    return accept(result);
}

bool GDGameCommandServer::resolve_placement_cell(
        const Dictionary& command,
        const StringName& dimension,
        Vector3i& out_cell,
        String& out_error) const {
    if (!command.has("anchor_cell") ||
        !command.has("build_direction") ||
        !command.has("build_mode")) {
        out_error = "placement direction metadata is missing";
        return false;
    }

    const Vector3i anchor = command.get("anchor_cell", Vector3i());
    const Vector3i offset = command.get("build_direction", Vector3i());
    Direction direction = Direction::COUNT;
    if (!direction_from_offset(offset, direction)) {
        out_error = "placement direction is not a voxel axis neighbor";
        return false;
    }

    const int64_t mode_value = command.get(
        "build_mode", static_cast<int64_t>(BuildMode::PlanetLocal));
    if (mode_value != static_cast<int64_t>(BuildMode::PlanetLocal) &&
        mode_value != static_cast<int64_t>(BuildMode::GlobalAxes)) {
        out_error = "placement build mode is invalid";
        return false;
    }

    const Vector3i resolved_cell = anchor + offset;
    if (command.has("cell")) {
        const Vector3i requested_cell = command.get("cell", Vector3i());
        if (requested_cell != resolved_cell) {
            out_error = "placement cell does not match anchor and direction";
            return false;
        }
    }

    if (mode_value == static_cast<int64_t>(BuildMode::PlanetLocal)) {
        if (world_data_ == nullptr || world_data_->get_world_ptr() == nullptr) {
            out_error = "world data is not available for local placement";
            return false;
        }
        const auto config = world_data_->get_world_ptr()->worldgen_config();
        const std::string dimension_key(String(dimension).utf8().get_data());
        const PlanetConfig* planet =
            config ? config->find_planet_config(dimension_key) : nullptr;
        if (planet == nullptr || !planet->is_planet()) {
            out_error = "planet-local placement requires a planet dimension";
            return false;
        }

        const PlanetBuildFrame frame(
            planet->center_x, planet->center_y, planet->center_z);
        const auto semantic = frame.classify(
            PlanetLocalBlockPos{anchor.x, anchor.y, anchor.z}, direction);
        const int64_t requested_semantic = command.get("build_semantic", -1);
        if (requested_semantic != static_cast<int64_t>(semantic)) {
            out_error = "placement local direction semantic is invalid";
            return false;
        }
    }

    out_cell = resolved_cell;
    return true;
}

Dictionary GDGameCommandServer::cmd_remove_object(const Dictionary& command) {
    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    const StringName object_type = command.get("object_type", StringName());

    if (object_type == object_furnace()) {
        if (furnace_manager_cpp_ == nullptr) {
            return reject(command_remove_object(),
                          "C++ furnace manager is not available");
        }
        if (!furnace_manager_cpp_->has_furnace(dimension, cell)) {
            return reject(command_remove_object(), "no furnace at this cell");
        }

        // Recover furnace contents before removing.
        const Dictionary snapshot =
            furnace_manager_cpp_->get_furnace_snapshot(dimension, cell);
        const int64_t input_item =
            static_cast<int64_t>(snapshot.get("input_item_id", 0));
        const int32_t input_count = static_cast<int32_t>(
            static_cast<int64_t>(snapshot.get("input_count", 0)));
        const int64_t fuel_item =
            static_cast<int64_t>(snapshot.get("fuel_item_id", 0));
        const double fuel_remaining =
            static_cast<double>(snapshot.get("fuel_burn_remaining", 0.0));
        const int64_t output_item =
            static_cast<int64_t>(snapshot.get("output_item_id", 0));
        const int32_t output_count = static_cast<int32_t>(
            static_cast<int64_t>(snapshot.get("output_count", 0)));

        // Remove furnace from FurnaceManager.
        furnace_manager_cpp_->remove_furnace(dimension, cell);

        // Remove MACHINE block entity from BlockEntityRegistry.
        if (world_data_ != nullptr && world_data_->get_world_ptr() != nullptr) {
            auto& registry =
                world_data_->get_world_ptr()->block_entity_registry();
            const EntityId owner =
                registry.find_owner_at(cell.x, cell.y, cell.z);
            if (owner.is_valid()) {
                const auto* state = registry.get_machine_state(owner);
                if (state != nullptr && state->machine_type == "furnace") {
                    registry.remove_entity(owner);
                }
            }
        }

        // Return recovered items to player inventory.
        if (input_item > 0 && input_count > 0) {
            add_inventory_item(input_item, input_count);
        }
        if (fuel_item > 0 && fuel_remaining > 0.0) {
            add_inventory_item(fuel_item, 1);
        }
        if (output_item > 0 && output_count > 0) {
            add_inventory_item(output_item, output_count);
        }

        emit_signal("inventory_synced");
        emit_signal("world_object_synced", object_type,
                    StringName("removed"), dimension, cell);

        Dictionary result;
        result["type"] = command_remove_object();
        result["object_type"] = object_type;
        return accept(result);
    }

    return reject(command_remove_object(),
                  "unsupported object type for removal");
}

// ============================================================
// Farming commands (Tier 1 planting system)
// ============================================================

namespace {
// Helper: compute chunk + local coordinates from a world cell.
void world_to_chunk_local(const Vector3i& cell, Vector3i& chunk, Vector3i& local) {
    constexpr int32_t chunk_size = 32;
    chunk = Vector3i(
        static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
        static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
        static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
    local = Vector3i(
        cell.x - chunk.x * chunk_size,
        cell.y - chunk.y * chunk_size,
        cell.z - chunk.z * chunk_size);
}
} // namespace

Dictionary GDGameCommandServer::cmd_till_farmland(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_till_farmland(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());

    const int32_t farmland_material = get_farmland_material_id();
    if (farmland_material <= 0) {
        return reject(command_till_farmland(), "farmland material is not registered");
    }

    const int32_t dirt_material = get_dirt_material_id();
    const int32_t air_material = get_air_material_id();

    // Read the target cell.
    Vector3i chunk, local;
    world_to_chunk_local(cell, chunk, local);
    const Dictionary existing = world_data_->get_terrain_cell(
        String(dimension), chunk.x, chunk.y, chunk.z,
        local.x, local.y, local.z);
    const int32_t existing_material = static_cast<int32_t>(
        static_cast<int64_t>(existing.get("material", -1)));

    // Accept dirt or grass (grass shares dirt role in most configs).
    // For simplicity, accept dirt role material. A grass check could be
    // added by comparing against a grass material key.
    if (existing_material != dirt_material) {
        return reject(command_till_farmland(), "target cell is not dirt");
    }

    // Check that the cell above is air (so the player can reach the farmland).
    const Vector3i above = cell + Vector3i(0, 1, 0);
    Vector3i above_chunk, above_local;
    world_to_chunk_local(above, above_chunk, above_local);
    const Dictionary above_cell = world_data_->get_terrain_cell(
        String(dimension), above_chunk.x, above_chunk.y, above_chunk.z,
        above_local.x, above_local.y, above_local.z);
    const int32_t above_material = static_cast<int32_t>(
        static_cast<int64_t>(above_cell.get("material", -1)));
    if (above_material != air_material) {
        return reject(command_till_farmland(), "cell above is not air");
    }

    // Convert dirt to farmland.
    if (!world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, farmland_material)) {
        return reject(command_till_farmland(), "failed to write farmland cell");
    }

    // Register a FARMLAND block entity.
    WorldData* world = world_data_->get_world_ptr();
    if (world != nullptr) {
        const int64_t tick = world->current_tick();
        world->block_entity_registry().register_farmland_entity(
            std::string(String(dimension).utf8().get_data()),
            cell.x, cell.y, cell.z,
            0.5f, 0.7f, tick);
    }

    emit_signal("terrain_cell_synced", dimension, chunk, local,
                dirt_material, farmland_material);

    Dictionary result;
    result["type"] = command_till_farmland();
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_plant_crop(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_plant_crop(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    const int64_t item_id = command.get("item_id", 0);
    const String species_key_str = command.get("species_key", String());

    if (item_id <= 0) {
        return reject(command_plant_crop(), "seed item is invalid");
    }
    if (!inventory_has_item(item_id, 1)) {
        return reject(command_plant_crop(), "seed item is missing");
    }

    WorldData* world = world_data_->get_world_ptr();
    if (world == nullptr) {
        return reject(command_plant_crop(), "world data is not available");
    }

    const auto config = world->worldgen_config();
    if (!config) {
        return reject(command_plant_crop(), "worldgen config is not available");
    }

    const std::string species_key(species_key_str.utf8().get_data());
    const CropSpeciesDef* species = config->find_crop_species(species_key);
    if (!species) {
        return reject(command_plant_crop(), "crop species not found");
    }

    // The crop goes in the air cell above farmland.
    const int32_t air_material = get_air_material_id();
    Vector3i chunk, local;
    world_to_chunk_local(cell, chunk, local);
    const Dictionary existing = world_data_->get_terrain_cell(
        String(dimension), chunk.x, chunk.y, chunk.z,
        local.x, local.y, local.z);
    const int32_t existing_material = static_cast<int32_t>(
        static_cast<int64_t>(existing.get("material", -1)));
    if (existing_material != air_material) {
        return reject(command_plant_crop(), "target cell is not air");
    }

    // Check that the cell below is farmland.
    const int32_t farmland_material = get_farmland_material_id();
    const Vector3i below = cell + Vector3i(0, -1, 0);
    Vector3i below_chunk, below_local;
    world_to_chunk_local(below, below_chunk, below_local);
    const Dictionary below_cell = world_data_->get_terrain_cell(
        String(dimension), below_chunk.x, below_chunk.y, below_chunk.z,
        below_local.x, below_local.y, below_local.z);
    const int32_t below_material = static_cast<int32_t>(
        static_cast<int64_t>(below_cell.get("material", -1)));
    if (below_material != farmland_material) {
        return reject(command_plant_crop(), "cell below is not farmland");
    }

    // Look up the SEED stage material.
    const int32_t seed_material = static_cast<int32_t>(
        config->material_id_or(species->stage_material_keys[0], 0));
    if (seed_material <= 0) {
        return reject(command_plant_crop(), "crop seed material is not registered");
    }

    // Consume the seed item.
    if (!remove_inventory_item(item_id, 1)) {
        return reject(command_plant_crop(), "failed to consume seed item");
    }

    // Place the seed-stage crop block.
    if (!world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, seed_material)) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_plant_crop(), "failed to write crop cell");
    }

    // Register a CROP block entity.
    const int64_t tick = world->current_tick();
    std::vector<OwnedCell> owned_cells;
    owned_cells.push_back({cell.x, cell.y, cell.z});
    world->block_entity_registry().register_crop_entity(
        std::string(String(dimension).utf8().get_data()),
        cell.x, cell.y, cell.z,
        species_key,
        CropGrowthStage::SEED,
        tick,
        owned_cells);

    // Update farmland rotation history.
    const EntityId fl_owner = world->block_entity_registry().find_owner_at(
        below.x, below.y, below.z);
    if (fl_owner.id != 0) {
        FarmlandBlockEntityState* fl =
            world->block_entity_registry().get_farmland_state_mut(fl_owner);
        if (fl) {
            if (fl->last_crop_key == species_key) {
                fl->consecutive_same_crop += 1;
            } else {
                fl->last_crop_key = species_key;
                fl->consecutive_same_crop = 1;
            }
        }
    }

    emit_signal("terrain_cell_synced", dimension, chunk, local,
                air_material, seed_material);
    emit_signal("inventory_synced");

    Dictionary result;
    result["type"] = command_plant_crop();
    result["species_key"] = species_key_str;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_harvest_crop(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_harvest_crop(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());

    WorldData* world = world_data_->get_world_ptr();
    if (world == nullptr) {
        return reject(command_harvest_crop(), "world data is not available");
    }

    auto& registry = world->block_entity_registry();

    // Find the CROP entity at this cell.
    const EntityId crop_owner = registry.find_owner_at(cell.x, cell.y, cell.z);
    if (crop_owner.id == 0) {
        return reject(command_harvest_crop(), "no crop at this cell");
    }
    if (registry.get_entity_type(crop_owner) != BlockEntityType::CROP) {
        return reject(command_harvest_crop(), "entity at cell is not a crop");
    }

    const CropBlockEntityState* crop_state = registry.get_crop_state(crop_owner);
    if (!crop_state) {
        return reject(command_harvest_crop(), "crop state is missing");
    }
    if (crop_state->growth_stage != CropGrowthStage::MATURE) {
        return reject(command_harvest_crop(), "crop is not mature");
    }

    const auto config = world->worldgen_config();
    if (!config) {
        return reject(command_harvest_crop(), "worldgen config is not available");
    }

    const CropSpeciesDef* species = config->find_crop_species(crop_state->species_key);
    if (!species) {
        return reject(command_harvest_crop(), "crop species not found");
    }

    // Grant crop product.
    // Use a simple deterministic count (min) for Tier 1; randomization can be
    // added later. We grant crop_min of the main product.
    // The item_id for the crop product is resolved via GDScript side; here we
    // emit a signal so GDScript can grant the actual items. For now, we use
    // the byproduct approach: emit a harvest signal with species + count.
    const int crop_count = species->crop_min;

    // Update farmland fertility (consume nutrients).
    const Vector3i below = cell + Vector3i(0, -1, 0);
    const EntityId fl_owner = registry.find_owner_at(below.x, below.y, below.z);
    if (fl_owner.id != 0) {
        FarmlandBlockEntityState* fl = registry.get_farmland_state_mut(fl_owner);
        if (fl) {
            fl->fertility = std::max(0.0f, fl->fertility - 0.15f);
        }
    }

    Vector3i chunk, local;
    world_to_chunk_local(cell, chunk, local);
    const int32_t air_material = get_air_material_id();

    if (species->repeat_harvest) {
        // Reset crop to GROWING stage for regrowth.
        CropBlockEntityState* mut_state = registry.get_crop_state_mut(crop_owner);
        if (mut_state) {
            mut_state->growth_stage = CropGrowthStage::GROWING;
            mut_state->last_growth_tick = world->current_tick();
            mut_state->last_harvest_tick = world->current_tick();
        }
        // Update terrain to growing-stage material.
        const int32_t growing_material = static_cast<int32_t>(
            config->material_id_or(species->stage_material_keys[2], 0));
        if (growing_material > 0) {
            world_data_->set_terrain_cell(
                String(dimension), chunk.x, chunk.y, chunk.z,
                local.x, local.y, local.z, growing_material);
            emit_signal("terrain_cell_synced", dimension, chunk, local,
                        air_material, growing_material);
        }
    } else {
        // Remove the crop block and entity.
        world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, air_material);
        registry.remove_entity(crop_owner);
        emit_signal("terrain_cell_synced", dimension, chunk, local,
                    air_material, air_material);
    }

    // Emit harvest signal so GDScript can grant items (it owns the
    // item_id <-> item_key mapping via ItemDatabase).
    emit_signal("crop_harvested", dimension, cell,
                String(species->species_key.c_str()),
                crop_count,
                String(species->crop_item_key.c_str()),
                String(species->byproduct_item_key.c_str()),
                species->byproduct_count);
    emit_signal("inventory_synced");

    Dictionary result;
    result["type"] = command_harvest_crop();
    result["species_key"] = String(species->species_key.c_str());
    result["crop_count"] = crop_count;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_fertilize(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_fertilize(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    const int64_t item_id = command.get("item_id", 0);

    if (item_id <= 0) {
        return reject(command_fertilize(), "fertilizer item is invalid");
    }
    if (!inventory_has_item(item_id, 1)) {
        return reject(command_fertilize(), "fertilizer item is missing");
    }

    WorldData* world = world_data_->get_world_ptr();
    if (world == nullptr) {
        return reject(command_fertilize(), "world data is not available");
    }

    auto& registry = world->block_entity_registry();

    // Find the CROP entity at this cell.
    const EntityId crop_owner = registry.find_owner_at(cell.x, cell.y, cell.z);
    if (crop_owner.id == 0) {
        return reject(command_fertilize(), "no crop at this cell");
    }
    if (registry.get_entity_type(crop_owner) != BlockEntityType::CROP) {
        return reject(command_fertilize(), "entity at cell is not a crop");
    }

    CropBlockEntityState* crop_state = registry.get_crop_state_mut(crop_owner);
    if (!crop_state) {
        return reject(command_fertilize(), "crop state is missing");
    }
    if (crop_state->growth_stage == CropGrowthStage::MATURE) {
        return reject(command_fertilize(), "crop is already mature");
    }

    const auto config = world->worldgen_config();
    if (!config) {
        return reject(command_fertilize(), "worldgen config is not available");
    }
    const CropSpeciesDef* species = config->find_crop_species(crop_state->species_key);
    if (!species) {
        return reject(command_fertilize(), "crop species not found");
    }

    // Consume the fertilizer item.
    if (!remove_inventory_item(item_id, 1)) {
        return reject(command_fertilize(), "failed to consume fertilizer item");
    }

    // Advance one stage.
    const CropGrowthStage new_stage = static_cast<CropGrowthStage>(
        static_cast<int>(crop_state->growth_stage) + 1);
    const int32_t new_material = static_cast<int32_t>(
        config->material_id_or(
            species->stage_material_keys[static_cast<int>(new_stage)], 0));
    if (new_material <= 0) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_fertilize(), "crop stage material is not registered");
    }

    crop_state->growth_stage = new_stage;
    crop_state->last_growth_tick = world->current_tick();

    Vector3i chunk, local;
    world_to_chunk_local(cell, chunk, local);
    world_data_->set_terrain_cell(
        String(dimension), chunk.x, chunk.y, chunk.z,
        local.x, local.y, local.z, new_material);

    emit_signal("terrain_cell_synced", dimension, chunk, local,
                get_air_material_id(), new_material);
    emit_signal("inventory_synced");

    Dictionary result;
    result["type"] = command_fertilize();
    result["new_stage"] = static_cast<int64_t>(new_stage);
    return accept(result);
}

// ============================================================
// TFC expansion commands
// ============================================================

Dictionary GDGameCommandServer::cmd_forage_wild(const Dictionary& command) {
    if (world_data_ == nullptr) {
        return reject(command_forage_wild(), "world data is not available");
    }

    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());

    WorldData* world = world_data_->get_world_ptr();
    if (world == nullptr) {
        return reject(command_forage_wild(), "world data is not available");
    }

    auto& registry = world->block_entity_registry();
    const EntityId crop_owner = registry.find_owner_at(cell.x, cell.y, cell.z);
    if (crop_owner.id == 0) {
        return reject(command_forage_wild(), "no crop at this cell");
    }
    if (registry.get_entity_type(crop_owner) != BlockEntityType::CROP) {
        return reject(command_forage_wild(), "entity at cell is not a crop");
    }

    const CropBlockEntityState* crop_state = registry.get_crop_state(crop_owner);
    if (!crop_state) {
        return reject(command_forage_wild(), "crop state is missing");
    }
    if (crop_state->growth_stage != CropGrowthStage::MATURE) {
        return reject(command_forage_wild(), "crop is not mature");
    }

    // Check that the cell below is NOT farmland (wild crop).
    const Vector3i below = cell + Vector3i(0, -1, 0);
    const EntityId below_owner = registry.find_owner_at(below.x, below.y, below.z);
    if (below_owner.id != 0 &&
        registry.get_entity_type(below_owner) == BlockEntityType::FARMLAND) {
        return reject(command_forage_wild(), "crop is on farmland, not wild");
    }

    const auto config = world->worldgen_config();
    if (!config) {
        return reject(command_forage_wild(), "worldgen config is not available");
    }

    const CropSpeciesDef* species = config->find_crop_species(crop_state->species_key);
    if (!species) {
        return reject(command_forage_wild(), "crop species not found");
    }

    // Reset crop to SPROUT stage for regrowth.
    Vector3i chunk, local;
    world_to_chunk_local(cell, chunk, local);
    const int32_t sprout_material = static_cast<int32_t>(
        config->material_id_or(species->stage_material_keys[1], 0));
    if (sprout_material > 0) {
        world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, sprout_material);
    }

    CropBlockEntityState* mut_state = registry.get_crop_state_mut(crop_owner);
    if (mut_state) {
        mut_state->growth_stage = CropGrowthStage::SPROUT;
        mut_state->last_growth_tick = world->current_tick();
        mut_state->last_harvest_tick = world->current_tick();
    }

    // Grant items: harvest signal so GDScript can handle item keys.
    const int crop_count = species->crop_min;
    emit_signal("crop_harvested", dimension, cell,
                String(species->species_key.c_str()),
                crop_count,
                String(species->crop_item_key.c_str()),
                String(species->byproduct_item_key.c_str()),
                species->byproduct_count);
    emit_signal("inventory_synced");

    emit_signal("terrain_cell_synced", dimension, chunk, local,
                get_air_material_id(), sprout_material);

    Dictionary result;
    result["type"] = command_forage_wild();
    result["species_key"] = String(species->species_key.c_str());
    result["crop_count"] = crop_count;
    return accept(result);
}


Dictionary GDGameCommandServer::cmd_knapping_pickup(const Dictionary& command) {
    const String tool_head_key = command.get("tool_head_key", "");
    const int64_t stone_item_id = static_cast<int64_t>(command.get("stone_item_id", 0));
    if (tool_head_key.is_empty()) {
        return reject(command_knapping_pickup(), "no tool head key");
    }

    // Resolve tool head item ID from key via GDCraftingManager (GDScript bridge).
    const int64_t head_id = GDCraftingManager::get_item_id_by_key(tool_head_key);
    if (head_id <= 0) {
        return reject(command_knapping_pickup(), "unknown tool head");
    }

    // Consume 1 stone from inventory.
    if (stone_item_id > 0) {
        if (!remove_inventory_item(stone_item_id, 1)) {
            return reject(command_knapping_pickup(), "stone item not found");
        }
    }

    // Grant tool head.
    add_inventory_item(head_id, 1);
    emit_signal("inventory_synced");

    Dictionary result;
    result["type"] = command_knapping_pickup();
    result["tool_head_key"] = tool_head_key;
    return accept(result);
}


Dictionary GDGameCommandServer::cmd_furnace_take_output(const Dictionary& command) {
    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    if (furnace_manager_cpp_ == nullptr) {
        return reject(command_furnace_take_output(), "C++ furnace manager is not available");
    }

    Ref<GDFurnaceData> furnace_data = furnace_manager_cpp_->get_furnace(dimension, cell);
    if (furnace_data.is_null()) {
        return reject(command_furnace_take_output(), "furnace is missing");
    }

    const int64_t output_id = furnace_data->get_output_item_id();
    const int32_t output_count =
        furnace_data->get_output_count();
    if (output_id <= 0 || output_count <= 0) {
        return reject(command_furnace_take_output(), "furnace output is empty");
    }

    const int32_t overflow =
        add_inventory_item(output_id, output_count, kSecondaryNone, false);
    const int32_t accepted = output_count - overflow;
    if (accepted <= 0) {
        return reject(command_furnace_take_output(), "inventory has no space");
    }

    if (!furnace_manager_cpp_->take_output(dimension, cell, accepted)) {
        return reject(command_furnace_take_output(), "failed to update furnace output");
    }

    emit_signal("inventory_synced");
    Dictionary snapshot = sync_furnace(dimension, cell, "take_output");

    Dictionary result;
    result["type"] = command_furnace_take_output();
    result["taken"] = accepted;
    result["overflow"] = overflow;
    result["furnace"] = snapshot;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_furnace_insert_input(const Dictionary& command) {
    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    const int64_t item_id = command.get("item_id", 0);

    if (furnace_manager_cpp_ == nullptr) {
        return reject(command_furnace_insert_input(), "C++ furnace manager is not available");
    }
    Ref<GDFurnaceData> furnace_data = furnace_manager_cpp_->get_furnace(dimension, cell);
    if (furnace_data.is_null()) {
        return reject(command_furnace_insert_input(), "furnace is missing");
    }
    if (item_id <= 0) {
        return reject(command_furnace_insert_input(), "input item is invalid");
    }
    if (furnace_manager_cpp_->get_recipe_for(item_id).is_empty()) {
        return reject(command_furnace_insert_input(), "input item has no furnace recipe");
    }

    const int64_t current_input = furnace_data->get_input_item_id();
    if (current_input != 0 && current_input != item_id) {
        return reject(command_furnace_insert_input(),
                      "furnace input slot contains another item");
    }
    if (!remove_inventory_item(item_id, 1)) {
        return reject(command_furnace_insert_input(),
                      "input item is missing from inventory");
    }

    if (!furnace_manager_cpp_->insert_input(dimension, cell, item_id, 1)) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_furnace_insert_input(), "failed to update furnace input");
    }
    emit_signal("inventory_synced");
    Dictionary snapshot = sync_furnace(dimension, cell, "insert_input");

    Dictionary result;
    result["type"] = command_furnace_insert_input();
    result["furnace"] = snapshot;
    return accept(result);
}

Dictionary GDGameCommandServer::cmd_furnace_insert_fuel(const Dictionary& command) {
    const StringName dimension = command.get("dimension", StringName("overworld"));
    const Vector3i cell = command.get("cell", Vector3i());
    const int64_t item_id = command.get("item_id", 0);

    if (furnace_manager_cpp_ == nullptr) {
        return reject(command_furnace_insert_fuel(), "C++ furnace manager is not available");
    }
    Ref<GDFurnaceData> furnace_data = furnace_manager_cpp_->get_furnace(dimension, cell);
    if (furnace_data.is_null()) {
        return reject(command_furnace_insert_fuel(), "furnace is missing");
    }
    if (item_id <= 0) {
        return reject(command_furnace_insert_fuel(), "fuel item is invalid");
    }
    if (gt::FuelRegistry::get_item_burn_ticks(static_cast<gt::ItemId>(item_id)) <= 0) {
        return reject(command_furnace_insert_fuel(), "item is not fuel");
    }

    const int64_t current_fuel = furnace_data->get_fuel_item_id();
    if (current_fuel != 0 && current_fuel != item_id) {
        return reject(command_furnace_insert_fuel(),
                      "furnace fuel slot contains another item");
    }
    if (!remove_inventory_item(item_id, 1)) {
        return reject(command_furnace_insert_fuel(),
                      "fuel item is missing from inventory");
    }

    if (!furnace_manager_cpp_->insert_fuel(dimension, cell, item_id)) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_furnace_insert_fuel(), "failed to update furnace fuel");
    }
    emit_signal("inventory_synced");
    Dictionary snapshot = sync_furnace(dimension, cell, "insert_fuel");

    Dictionary result;
    result["type"] = command_furnace_insert_fuel();
    result["furnace"] = snapshot;
    return accept(result);
}

int32_t GDGameCommandServer::add_inventory_item(
        int64_t item_id, int32_t count, int32_t secondary_id, bool emit_sync) {
    if (current_player_ == nullptr || current_player_->inventory == nullptr) {
        reject(command_add_inventory_item(), "inventory is not configured");
        return count;
    }
    const int32_t overflow = current_player_->inventory->add_item(
        static_cast<gt::ItemId>(item_id), count, secondary_id);
    if (emit_sync) {
        emit_signal("inventory_synced");
    }
    return overflow;
}

bool GDGameCommandServer::remove_inventory_item(int64_t item_id, int32_t count) {
    if (current_player_ == nullptr || current_player_->inventory == nullptr || count <= 0) {
        return false;
    }
    gt::Inventory* inv = current_player_->inventory;
    const gt::ItemId id = static_cast<gt::ItemId>(item_id);
    if (!inv->has_enough(id, count)) return false;

    int32_t remaining = count;
    while (remaining > 0) {
        const int32_t index = inv->find_item(id);
        if (index < 0) return false;

        const gt::InventorySlot& slot = inv->get_slot(index);
        const int32_t slot_count = slot.count;
        const int32_t take = remaining < slot_count ? remaining : slot_count;
        if (!inv->remove_from_slot(index, take)) return false;
        remaining -= take;
    }

    return true;
}

bool GDGameCommandServer::inventory_has_item(int64_t item_id, int32_t count) const {
    return current_player_ != nullptr
        && current_player_->inventory != nullptr
        && current_player_->inventory->has_enough(
            static_cast<gt::ItemId>(item_id), count);
}

Dictionary GDGameCommandServer::sync_furnace(
        const StringName& dimension, const Vector3i& cell, const char* reason) {
    (void)reason;
    Dictionary snapshot;
    if (furnace_manager_cpp_ != nullptr) {
        snapshot = furnace_manager_cpp_->get_furnace_snapshot(dimension, cell);
    }

    emit_signal("furnace_synced", dimension, cell);
    return snapshot;
}

bool GDGameCommandServer::is_world_object_occupied(
        const StringName& object_type, const StringName& dimension,
        const Vector3i& cell) const {
    if (object_type == object_workbench()) {
        // Workbenches occupy terrain cells; check if the cell has workbench material.
        if (world_data_ == nullptr) return true;
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        return existing_material == get_workbench_material_id();
    }
    if (object_type == object_furnace()) {
        return furnace_manager_cpp_ != nullptr &&
            furnace_manager_cpp_->has_furnace(dimension, cell);
    }
    if (object_type == object_ladder()) {
        // Ladders occupy terrain cells; check if the cell is non-air.
        if (world_data_ == nullptr) return true;
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        return existing_material != get_air_material_id();
    }
    if (object_type == object_fence()) {
        // Fences occupy terrain cells; check if the cell is non-air.
        if (world_data_ == nullptr) return true;
        const int32_t chunk_size = 32;
        const Vector3i chunk(
            static_cast<int32_t>(floorf(static_cast<float>(cell.x) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.y) / chunk_size)),
            static_cast<int32_t>(floorf(static_cast<float>(cell.z) / chunk_size)));
        const Vector3i local(
            cell.x - chunk.x * chunk_size,
            cell.y - chunk.y * chunk_size,
            cell.z - chunk.z * chunk_size);
        const godot::Dictionary existing = world_data_->get_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z);
        const int32_t existing_material = static_cast<int32_t>(
            static_cast<int64_t>(existing.get("material", -1)));
        return existing_material != get_air_material_id();
    }
    return false;
}

bool GDGameCommandServer::node_bool_call(
        Node* node, const StringName& method, const StringName& dimension,
        const Vector3i& cell) const {
    if (node == nullptr || !node->has_method(method)) return false;
    return static_cast<bool>(node->call(method, dimension, cell));
}

gt::ToolStats GDGameCommandServer::get_mining_stats() const {
    const int64_t item_id = get_equipped_item();
    gt::ToolStats stats;
    stats.speed_multiplier = 0.3f;
    if (item_id <= 0) return stats;

    // Query GDScript autoload singleton for tool stats.
    // Use a simpler approach: try ClassDB singleton lookup.
    Node* item_db = nullptr;
    {
        // Access autoload through the scene tree root.
        MainLoop* loop = godot::Engine::get_singleton()->get_main_loop();
        SceneTree* st = Object::cast_to<SceneTree>(loop);
        if (st != nullptr) {
            Window* root_win = st->get_root();
            if (root_win != nullptr) {
                item_db = root_win->get_node<godot::Node>(NodePath("/root/ItemDatabase"));
            }
        }
    }
    if (item_db == nullptr) return stats;

    const Variant result = item_db->call("get_tool_stats", item_id);
    if (result.get_type() != Variant::OBJECT) return stats;

    godot::Ref<godot::Resource> res = result;
    if (res.is_null()) return stats;

    // Extract fields from the ToolDef Resource.
    stats.mining_level = static_cast<uint8_t>(
        static_cast<int64_t>(res->get("mining_level")));
    stats.speed_multiplier = static_cast<float>(
        static_cast<double>(res->get("speed")));
    stats.attack_damage = static_cast<float>(
        static_cast<double>(res->get("attack_damage")));

    const int64_t tool_type_raw = static_cast<int64_t>(res->get("tool_type"));
    stats.type = static_cast<gt::ToolType>(tool_type_raw);

    return stats;
}

GDGameCommandServer::MiningEligibility
GDGameCommandServer::check_mining_eligibility(int32_t terrain_material) const {
    MiningEligibility result;
    if (world_data_ == nullptr) return result;

    const auto snapshot = world_data_->get_worldgen_snapshot();
    const auto* material = snapshot->find_material(
        static_cast<TerrainMaterialId>(terrain_material));
    if (material == nullptr) {
        result.can_mine = true;
        result.can_drop = true;
        return result;
    }

    result.required_mining_level = material->required_mining_level;
    result.hardness = material->hardness;

    const gt::ToolStats stats = get_mining_stats();

    if (stats.mining_level < material->required_mining_level) {
        return result;
    }

    result.can_mine = true;

    const bool tag_matches = tool_tag_matches(material->required_tool_tag, stats.type);

    float speed = stats.speed_multiplier;
    if (!material->required_tool_tag.empty() && !tag_matches
        && stats.type != gt::ToolType::NONE) {
        speed *= 0.3f;
    }
    result.effective_speed = std::max(speed, 0.01f);

    result.can_drop = (material->required_mining_level == 0) || tag_matches;

    return result;
}

bool GDGameCommandServer::player_has_tool_named(const String& tool_name) const {
    if (tool_name.is_empty()) return true;
    if (current_player_ == nullptr ||
        current_player_->inventory == nullptr ||
        current_player_->equipment == nullptr) {
        return false;
    }

    const String tool_lower = tool_name.to_lower();
    gt::Inventory* inv = current_player_->inventory;
    for (int32_t i = 0; i < inv->slot_count(); ++i) {
        const gt::InventorySlot& slot = inv->get_slot(i);
        const int64_t item_id = static_cast<int64_t>(slot.item_id);
        if (item_id > 0 && tool_name_matches(item_id, tool_lower)) return true;
    }

    gt::Equipment* eq = current_player_->equipment;
    for (int32_t slot = 0; slot < 6; ++slot) {
        const int64_t item_id = static_cast<int64_t>(
            eq->get_equipped(static_cast<gt::EquipmentSlot>(slot)));
        if (item_id > 0 && tool_name_matches(item_id, tool_lower)) return true;
    }

    return false;
}

int64_t GDGameCommandServer::get_equipped_item() const {
    if (current_player_ == nullptr || current_player_->equipment == nullptr) {
        return 0;
    }
    return static_cast<int64_t>(
        current_player_->equipment->get_equipped(gt::EquipmentSlot::MAIN_HAND));
}

int32_t GDGameCommandServer::get_air_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->roles.air);
}

int32_t GDGameCommandServer::get_ladder_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->runtime_ids.ladder);
}

int32_t GDGameCommandServer::get_workbench_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->runtime_ids.workbench);
}

int32_t GDGameCommandServer::get_fence_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->runtime_ids.fence);
}

int32_t GDGameCommandServer::get_farmland_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->runtime_ids.farmland);
}

int32_t GDGameCommandServer::get_dirt_material_id() const {
    if (world_data_ == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(world_data_->get_worldgen_snapshot()->roles.dirt);
}

int32_t GDGameCommandServer::get_material_id_by_key(const String& key) const {
    if (world_data_ == nullptr) {
        return 0;
    }
    const auto snapshot = world_data_->get_worldgen_snapshot();
    if (!snapshot) return 0;
    return static_cast<int32_t>(
        snapshot->material_id_or(std::string(key.utf8().get_data()), 0));
}

Array GDGameCommandServer::get_terrain_drops(int32_t terrain_material) const {
    Array drops;
    if (world_data_ == nullptr) {
        return drops;
    }

    const auto snapshot = world_data_->get_worldgen_snapshot();
    const auto* material = snapshot->find_material(
        static_cast<TerrainMaterialId>(terrain_material));
    if (material == nullptr) {
        return drops;
    }

    for (const auto& drop : material->drops) {
        if (drop.item_id == gt::kInvalidItemId) continue;
        if (drop.chance <= 0.0f) continue;
        if (drop.chance < 1.0f) {
            const float roll = static_cast<float>(UtilityFunctions::randf());
            if (roll > drop.chance) continue;
        }

        int32_t count = std::max(1, drop.count);
        const int32_t min_count = std::max(1, drop.min_count);
        const int32_t max_count = std::max(min_count, drop.max_count);
        if (max_count > min_count) {
            count = min_count + static_cast<int32_t>(
                UtilityFunctions::randi() % static_cast<uint64_t>(max_count - min_count + 1));
        }
        drops.append(stack_dict(static_cast<int64_t>(drop.item_id), count));
    }
    return drops;
}

bool GDGameCommandServer::tool_name_matches(int64_t item_id,
                                            const String& tool_lower) {
    const char* name =
        gt::ItemRegistry::get_item_title_key(static_cast<gt::ItemId>(item_id));
    if (name == nullptr) return false;
    return String(name).to_lower().contains(tool_lower);
}

bool GDGameCommandServer::command_recipe_matches_registered_recipe(
        const Dictionary& recipe) {
    const String name = recipe.get("name", "");
    if (name.is_empty()) return false;
    for (const auto& registered : gt::CraftingManager::get_registry()) {
        if (recipe_matches_dictionary(registered, recipe)) return true;
    }
    return false;
}

StringName GDGameCommandServer::command_type(const Dictionary& command) {
    return command.get("type", StringName());
}

Dictionary GDGameCommandServer::accept(const Dictionary& data) const {
    Dictionary result = data.duplicate();
    result["ok"] = true;
    return result;
}

Dictionary GDGameCommandServer::reject(const StringName& command_type,
                                       const String& reason) {
    emit_signal("command_rejected", command_type, reason);

    Dictionary result;
    result["ok"] = false;
    result["type"] = command_type;
    result["reason"] = reason;
    return result;
}

void GDGameCommandServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_world_data", "world"),
                         &GDGameCommandServer::set_world_data);
    ClassDB::bind_method(D_METHOD("get_world_data"),
                         &GDGameCommandServer::get_world_data);
    ClassDB::bind_method(D_METHOD("register_player", "player_id", "inventory", "equipment"),
                         &GDGameCommandServer::register_player);
    ClassDB::bind_method(D_METHOD("unregister_player", "player_id"),
                         &GDGameCommandServer::unregister_player);
    ClassDB::bind_method(D_METHOD("get_player_count"),
                         &GDGameCommandServer::get_player_count);
    ClassDB::bind_method(D_METHOD("set_furnace_manager", "manager"),
                         &GDGameCommandServer::set_furnace_manager);
    ClassDB::bind_method(D_METHOD("submit_command", "command"),
                         &GDGameCommandServer::submit_command);
    ClassDB::bind_method(D_METHOD("register_command", "command_name", "callback"),
                         &GDGameCommandServer::register_command);
    ClassDB::bind_method(D_METHOD("unregister_command", "command_name"),
                         &GDGameCommandServer::unregister_command);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_data",
                              PROPERTY_HINT_RESOURCE_TYPE, "GDWorldData"),
                 "set_world_data", "get_world_data");

    ADD_SIGNAL(MethodInfo("terrain_cell_synced",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "chunk"),
        PropertyInfo(Variant::VECTOR3I, "local"),
        PropertyInfo(Variant::INT, "old_material"),
        PropertyInfo(Variant::INT, "new_material")));
    ADD_SIGNAL(MethodInfo("inventory_synced"));
    ADD_SIGNAL(MethodInfo("equipment_synced",
        PropertyInfo(Variant::INT, "slot"),
        PropertyInfo(Variant::INT, "old_item_id"),
        PropertyInfo(Variant::INT, "new_item_id")));
    ADD_SIGNAL(MethodInfo("world_object_synced",
        PropertyInfo(Variant::STRING_NAME, "kind"),
        PropertyInfo(Variant::STRING_NAME, "action"),
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("furnace_synced",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("command_rejected",
        PropertyInfo(Variant::STRING_NAME, "command_type"),
        PropertyInfo(Variant::STRING, "reason")));
    ADD_SIGNAL(MethodInfo("crop_harvested",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell"),
        PropertyInfo(Variant::STRING, "species_key"),
        PropertyInfo(Variant::INT, "crop_count"),
        PropertyInfo(Variant::STRING, "crop_item_key"),
        PropertyInfo(Variant::STRING, "byproduct_item_key"),
        PropertyInfo(Variant::INT, "byproduct_count")));
}

} // namespace science_and_theology
