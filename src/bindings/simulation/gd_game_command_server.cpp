#include "gd_game_command_server.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "bindings/player/gd_player_equipment.h"
#include "bindings/player/gd_player_inventory.h"
#include "bindings/world/gd_furnace_manager.h"
#include "bindings/world/gd_world_data.h"
#include "core/crafting/crafting.hpp"
#include "core/fuel/fuel_registry.hpp"
#include "core/material/material_item.hpp"
#include "core/material/tool_items.hpp"

namespace science_and_theology {

using namespace godot;

namespace {

StringName command_mine_block() { return StringName("mine_block"); }
StringName command_add_inventory_item() { return StringName("add_inventory_item"); }
StringName command_remove_inventory_item() { return StringName("remove_inventory_item"); }
StringName command_craft_recipe() { return StringName("craft_recipe"); }
StringName command_place_object() { return StringName("place_object"); }
StringName command_furnace_take_output() { return StringName("furnace_take_output"); }
StringName command_furnace_insert_input() { return StringName("furnace_insert_input"); }
StringName command_furnace_insert_fuel() { return StringName("furnace_insert_fuel"); }

StringName object_workbench() { return StringName("workbench"); }
StringName object_furnace() { return StringName("furnace"); }
StringName object_ladder() { return StringName("ladder"); }

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

void GDGameCommandServer::configure_player(Resource* inventory, Resource* equipment) {
    inventory_ = Object::cast_to<GDPlayerInventory>(inventory);
    equipment_ = Object::cast_to<GDPlayerEquipment>(equipment);
    if (inventory != nullptr && inventory_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: configure_player received non-GDPlayerInventory");
    }
    if (equipment != nullptr && equipment_ == nullptr) {
        UtilityFunctions::push_warning(
            "GDGameCommandServer: configure_player received non-GDPlayerEquipment");
    }
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
    if (type == command_mine_block()) return cmd_mine_block(command);
    if (type == command_add_inventory_item()) return cmd_add_inventory_item(command);
    if (type == command_remove_inventory_item()) return cmd_remove_inventory_item(command);
    if (type == command_craft_recipe()) return cmd_craft_recipe(command);
    if (type == command_place_object()) return cmd_place_object(command);
    if (type == command_furnace_take_output()) return cmd_furnace_take_output(command);
    if (type == command_furnace_insert_input()) return cmd_furnace_insert_input(command);
    if (type == command_furnace_insert_fuel()) return cmd_furnace_insert_fuel(command);
    return reject(type, "unknown command");
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
    if (!has_required_tool(material)) {
        return reject(command_mine_block(), "required tool is not equipped");
    }

    if (!world_data_->set_terrain_cell(
            String(dimension), chunk.x, chunk.y, chunk.z,
            local.x, local.y, local.z, air_material)) {
        return reject(command_mine_block(), "failed to write terrain cell");
    }

    Array drops = get_terrain_drops(material);
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

    Dictionary result;
    result["type"] = command_mine_block();
    result["material"] = material;
    result["drops"] = drops;
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
    if (inventory_ == nullptr) {
        return reject(command_craft_recipe(), "inventory is not configured");
    }

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
        if (!inventory_->has_enough(in_id, in_count)) {
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
    const Vector3i cell = command.get("cell", Vector3i());
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
    } else {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_place_object(), "unknown object type");
    }

    if (!placed) {
        add_inventory_item(item_id, 1, kSecondaryNone, false);
        return reject(command_place_object(), "world object could not be placed");
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
    if (inventory_ == nullptr) {
        reject(command_add_inventory_item(), "inventory is not configured");
        return count;
    }
    const int32_t overflow = inventory_->add_item(item_id, count, secondary_id);
    if (emit_sync) {
        emit_signal("inventory_synced");
    }
    return overflow;
}

bool GDGameCommandServer::remove_inventory_item(int64_t item_id, int32_t count) {
    if (inventory_ == nullptr || count <= 0) return false;
    if (!inventory_->has_enough(item_id, count)) return false;

    int32_t remaining = count;
    while (remaining > 0) {
        const int32_t index = inventory_->find_item(item_id);
        if (index < 0) return false;

        Dictionary slot = inventory_->get_slot(index);
        const int32_t slot_count =
            static_cast<int32_t>(static_cast<int64_t>(slot.get("count", 0)));
        const int32_t take = remaining < slot_count ? remaining : slot_count;
        if (!inventory_->remove_from_slot(index, take)) return false;
        remaining -= take;
    }

    return true;
}

bool GDGameCommandServer::inventory_has_item(int64_t item_id, int32_t count) const {
    return inventory_ != nullptr && inventory_->has_enough(item_id, count);
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
    return false;
}

bool GDGameCommandServer::node_bool_call(
        Node* node, const StringName& method, const StringName& dimension,
        const Vector3i& cell) const {
    if (node == nullptr || !node->has_method(method)) return false;
    return static_cast<bool>(node->call(method, dimension, cell));
}

bool GDGameCommandServer::has_required_tool(int32_t terrain_material) const {
    if (world_data_ == nullptr) return false;

    const auto snapshot = world_data_->get_worldgen_snapshot();
    const auto* material = snapshot->find_material(
        static_cast<TerrainMaterialId>(terrain_material));
    if (material == nullptr || material->required_tool_tag.empty()) {
        return true;
    }

    const int64_t item_id = get_equipped_item();
    if (item_id <= 0) return false;

    gt::ToolStats stats;
    if (!get_tool_stats_for_item(item_id, stats)) return false;

    return tool_tag_matches(material->required_tool_tag, stats.type)
        && stats.mining_level >= material->required_mining_level;
}

bool GDGameCommandServer::player_has_tool_named(const String& tool_name) const {
    if (tool_name.is_empty()) return true;
    if (inventory_ == nullptr || equipment_ == nullptr) return false;

    const String tool_lower = tool_name.to_lower();
    for (int32_t i = 0; i < inventory_->get_slot_count(); ++i) {
        Dictionary slot = inventory_->get_slot(i);
        const int64_t item_id = slot.get("item_id", 0);
        if (item_id > 0 && tool_name_matches(item_id, tool_lower)) return true;
    }

    for (int32_t slot = 0; slot < 6; ++slot) {
        const int64_t item_id = equipment_->get_equipped(slot);
        if (item_id > 0 && tool_name_matches(item_id, tool_lower)) return true;
    }

    return false;
}

int64_t GDGameCommandServer::get_equipped_item() const {
    if (equipment_ == nullptr) return 0;
    return equipment_->get_equipped(GDPlayerEquipment::SLOT_MAIN_HAND);
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

bool GDGameCommandServer::get_tool_stats_for_item(int64_t item_id,
                                                  gt::ToolStats& out_stats) {
    switch (static_cast<gt::ItemId>(item_id)) {
        case gt::WOODEN_PICKAXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::PICKAXE, 0);
            out_stats.mining_level = 0;
            return true;
        case gt::STONE_PICKAXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::PICKAXE, 1);
            out_stats.mining_level = 1;
            return true;
        case gt::IRON_PICKAXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::PICKAXE, 2);
            out_stats.mining_level = 2;
            return true;
        case gt::WOODEN_AXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::AXE, 0);
            out_stats.mining_level = 0;
            return true;
        case gt::STONE_AXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::AXE, 1);
            out_stats.mining_level = 1;
            return true;
        case gt::IRON_AXE:
            out_stats = gt::ToolStats::default_for(gt::ToolType::AXE, 2);
            out_stats.mining_level = 2;
            return true;
        case gt::WOODEN_SHOVEL:
            out_stats = gt::ToolStats::default_for(gt::ToolType::SHOVEL, 0);
            out_stats.mining_level = 0;
            return true;
        case gt::STONE_SHOVEL:
            out_stats = gt::ToolStats::default_for(gt::ToolType::SHOVEL, 1);
            out_stats.mining_level = 1;
            return true;
        case gt::IRON_SHOVEL:
            out_stats = gt::ToolStats::default_for(gt::ToolType::SHOVEL, 2);
            out_stats.mining_level = 2;
            return true;
        default:
            return false;
    }
}

bool GDGameCommandServer::tool_name_matches(int64_t item_id,
                                            const String& tool_lower) {
    const char* name =
        gt::ItemRegistry::get_item_display_name(static_cast<gt::ItemId>(item_id));
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
    ClassDB::bind_method(D_METHOD("configure_player", "inventory", "equipment"),
                         &GDGameCommandServer::configure_player);
    ClassDB::bind_method(D_METHOD("set_furnace_manager", "manager"),
                         &GDGameCommandServer::set_furnace_manager);
    ClassDB::bind_method(D_METHOD("submit_command", "command"),
                         &GDGameCommandServer::submit_command);

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
}

} // namespace science_and_theology
