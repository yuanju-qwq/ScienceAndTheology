extends Node

const TIER_ULV := 0
const TIER_LV := 1

var _loaded := false

func _ready() -> void:
	load_content()

func load_content() -> void:
	if _loaded:
		return
	_loaded = true

	GDCraftingManager.clear_load_report()
	GDRecipeDatabase.clear_load_report()

	var crafting_added := GDCraftingManager.register_recipes(_crafting_recipes())
	var machine_added := GDRecipeDatabase.register_recipes(_machine_recipes())

	print("ContentDatabase: registered %d crafting recipes, %d machine recipes." %
			[crafting_added, machine_added])
	_print_report("crafting", GDCraftingManager.get_load_report())
	_print_report("machine", GDRecipeDatabase.get_load_report())

func _print_report(label: String, report: Array) -> void:
	for entry in report:
		push_warning("ContentDatabase %s: %s" % [label, entry])

func _item(item_id: int, count: int = 1) -> Dictionary:
	return {
		"type": "item",
		"item_id": item_id,
		"count": count,
		"amount": count,
	}

func _item_key(item_key: String, count: int = 1) -> Dictionary:
	return {
		"type": "item",
		"item_key": item_key,
		"count": count,
		"amount": count,
	}

func _mat(form: String, material: String, count: int = 1) -> Dictionary:
	return _item_key("%s.%s" % [form, material], count)

func _fluid(name: String, amount: int) -> Dictionary:
	return {
		"type": "fluid",
		"fluid_name": name,
		"amount": amount,
	}

func _recipe(
		name: String,
		category: String,
		required_station: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	var data := {
		"name": name,
		"category": category,
		"required_station": required_station,
		"inputs": inputs,
		"output": output,
	}
	if not required_tool.is_empty():
		data["required_tool"] = required_tool
	return data

func _hand(
		name: String,
		category: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	return _recipe(name, category, "", inputs, output, required_tool)

func _bench(
		name: String,
		category: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	return _recipe(name, category, "workbench", inputs, output, required_tool)

func _add_recipe_if_valid(
		recipes: Array,
		recipe: Dictionary,
		warn_on_skip: bool = true) -> void:
	if _recipe_items_are_valid(recipe):
		recipes.append(recipe)
		return
	if warn_on_skip:
		push_warning("ContentDatabase crafting: skipped invalid recipe '%s'" %
				recipe.get("name", "<unnamed>"))

func _recipe_items_are_valid(recipe: Dictionary) -> bool:
	if not _stack_is_valid(recipe.get("output", {})):
		return false
	for input in recipe.get("inputs", []):
		if not _stack_is_valid(input):
			return false
	return true

func _stack_is_valid(stack: Dictionary) -> bool:
	if stack.has("item_id"):
		return GDCraftingManager.is_valid_item(int(stack["item_id"]))
	if stack.has("item_key"):
		return _has_key(str(stack["item_key"]))
	return false

func _has_key(item_key: String) -> bool:
	return GDCraftingManager.get_item_id_by_key(item_key) > 0

func _crafting_recipes() -> Array:
	var recipes := []
	_add_material_compression_recipes(recipes)
	_add_tool_recipes(recipes)
	_add_part_recipes(recipes)
	_add_wire_recipes(recipes)
	_add_cable_recipes(recipes)
	_add_circuit_recipes(recipes)
	_add_machine_recipes(recipes)
	_add_misc_recipes(recipes)
	return recipes

func _add_material_compression_recipes(recipes: Array) -> void:
	var compressible_metals := [
		"iron", "copper", "tin", "lead", "bronze", "steel",
		"gold", "silver", "nickel", "zinc", "aluminum", "brass",
		"invar", "electrum", "wrought_iron", "bismuth", "antimony",
	]
	for material in compressible_metals:
		_add_recipe_if_valid(recipes, _hand(
				"compress_%s_nugget_to_ingot" % material,
				"materials",
				[_mat("nugget", material, 9)],
				_mat("ingot", material, 1)), false)
		_add_recipe_if_valid(recipes, _hand(
				"decompress_%s_ingot_to_nugget" % material,
				"materials",
				[_mat("ingot", material, 1)],
				_mat("nugget", material, 9)), false)
		_add_recipe_if_valid(recipes, _hand(
				"compress_%s_ingot_to_block" % material,
				"materials",
				[_mat("ingot", material, 9)],
				_mat("block", material, 1)), false)
		_add_recipe_if_valid(recipes, _hand(
				"decompress_%s_block_to_ingot" % material,
				"materials",
				[_mat("block", material, 1)],
				_mat("ingot", material, 9)), false)

	var dust_materials := [
		"coal", "diamond", "emerald", "lapis", "redstone",
		"glowstone", "sulfur", "saltpeter",
	]
	for material in dust_materials:
		_add_recipe_if_valid(recipes, _hand(
				"compress_%s_dust_to_block" % material,
				"materials",
				[_mat("dust", material, 4)],
				_mat("block", material, 1)), false)

	_add_recipe_if_valid(recipes, _hand(
			"decompress_coal_block",
			"materials",
			[_mat("block", "coal", 1)],
			_mat("gem", "coal", 9)), false)

func _add_tool_recipes(recipes: Array) -> void:
	_add_recipe_if_valid(recipes, _bench(
			"craft_hammer",
			"tools",
			[_mat("ingot", "iron", 4), _mat("rod", "wood", 2)],
			_item_key("gt_hammer", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_wrench",
			"tools",
			[_mat("ingot", "iron", 3), _mat("rod", "wood", 1)],
			_item_key("gt_wrench", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_screwdriver",
			"tools",
			[_mat("ingot", "iron", 1), _mat("rod", "wood", 1)],
			_item_key("gt_screwdriver", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_crowbar",
			"tools",
			[_mat("ingot", "iron", 2), _mat("rod", "wood", 1)],
			_item_key("gt_crowbar", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_saw",
			"tools",
			[_mat("plate", "iron", 2), _mat("ingot", "iron", 1), _mat("rod", "wood", 1)],
			_item_key("gt_saw", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_file",
			"tools",
			[_mat("plate", "iron", 2), _mat("rod", "wood", 1)],
			_item_key("gt_file", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_wire_cutter",
			"tools",
			[_mat("plate", "iron", 3), _mat("rod", "wood", 1)],
			_item_key("gt_wire_cutter", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_soft_mallet",
			"tools",
			[_mat("plate", "wood", 3), _mat("rod", "wood", 1)],
			_item_key("gt_soft_mallet", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_hard_hammer",
			"tools",
			[_mat("block", "iron", 3), _mat("rod", "wood", 1)],
			_item_key("gt_hard_hammer", 1)))

func _add_part_recipes(recipes: Array) -> void:
	_add_recipe_if_valid(recipes, _bench(
			"craft_iron_rod",
			"parts",
			[_mat("ingot", "iron", 1)],
			_mat("rod", "iron", 2),
			"file"))
	_add_recipe_if_valid(recipes, _bench(
			"craft_iron_plate",
			"parts",
			[_mat("ingot", "iron", 1)],
			_mat("plate", "iron", 2),
			"hammer"))
	_add_recipe_if_valid(recipes, _bench(
			"craft_iron_screw",
			"parts",
			[_mat("rod", "iron", 1)],
			_mat("screw", "iron", 4),
			"file"))

func _add_wire_recipes(recipes: Array) -> void:
	var wire_metals := ["copper", "tin", "gold", "silver", "aluminum",
			"nickel", "lead", "zinc", "iron", "steel"]
	for material in wire_metals:
		_add_recipe_if_valid(recipes, _bench(
				"craft_%s_wire" % material,
				"wires",
				[_mat("ingot", material, 1)],
				_mat("wire", material, 2),
				"wire_cutter"), false)

func _add_cable_recipes(recipes: Array) -> void:
	var cable_metals := ["copper", "tin", "gold", "silver",
			"aluminum", "nickel", "iron", "steel"]
	for material in cable_metals:
		_add_recipe_if_valid(recipes, _bench(
				"craft_%s_cable" % material,
				"cables",
				[_mat("wire", material, 1), _mat("plate", "rubber", 1)],
				_mat("wire", material, 1)), false)

func _add_circuit_recipes(recipes: Array) -> void:
	_add_recipe_if_valid(recipes, _bench(
			"craft_vacuum_tube",
			"circuits",
			[_mat("plate", "glass", 3), _mat("wire", "copper", 4),
			 _mat("rod", "iron", 1), _mat("dust", "redstone", 2)],
			_item_key("vacuum_tube", 1)), false)
	_add_recipe_if_valid(recipes, _bench(
			"craft_primitive_circuit",
			"circuits",
			[_mat("wire", "copper", 4), _mat("dust", "redstone", 4),
			 _item_key("stone_plate", 1), _mat("rod", "iron", 1)],
			_item_key("circuit_primitive", 1)), false)
	_add_recipe_if_valid(recipes, _bench(
			"craft_basic_circuit",
			"circuits",
			[_mat("wire", "gold", 4), _mat("dust", "redstone", 4),
			 _item_key("circuit_primitive", 1), _mat("plate", "polyethylene", 1)],
			_item_key("circuit_basic", 1)), false)
	if _has_key("dust.redstone"):
		_add_recipe_if_valid(recipes, _bench(
				"craft_good_circuit",
				"circuits",
				[_mat("wire", "copper", 4), _mat("dust", "lapis", 4),
				 _item_key("circuit_basic", 1), _mat("plate", "steel", 1)],
				_item_key("circuit_good", 1)), false)

func _add_machine_recipes(recipes: Array) -> void:
	_add_recipe_if_valid(recipes, _bench(
			"craft_machine_hull_basic",
			"machines",
			[_mat("plate", "iron", 8)],
			_item_key("machine_hull_basic", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_machine_hull_advanced",
			"machines",
			[_mat("plate", "steel", 8)],
			_item_key("machine_hull_advanced", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_electric_motor_lv",
			"machines",
			[_mat("wire", "copper", 2), _mat("rod", "iron", 2), _mat("ingot", "iron", 1)],
			_item_key("electric_motor_lv", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_electric_piston_lv",
			"machines",
			[_mat("plate", "iron", 1), _mat("rod", "iron", 1), _item_key("electric_motor_lv", 1)],
			_item_key("electric_piston_lv", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_robot_arm_lv",
			"machines",
			[_item_key("electric_piston_lv", 1), _item_key("electric_motor_lv", 1),
			 _item_key("circuit_primitive", 1), _mat("rod", "iron", 3)],
			_item_key("robot_arm_lv", 1)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_conveyor_lv",
			"machines",
			[_mat("plate", "rubber", 2), _item_key("electric_motor_lv", 1)],
			_item_key("conveyor_module_lv", 1)), false)
	_add_recipe_if_valid(recipes, _bench(
			"craft_pump_lv",
			"machines",
			[_mat("plate", "iron", 2), _mat("plate", "rubber", 1), _item_key("electric_motor_lv", 1)],
			_item_key("pump_lv", 1)), false)
	_add_recipe_if_valid(recipes, _bench(
			"craft_fluid_cell",
			"machines",
			[_mat("plate", "tin", 4)],
			_item_key("empty_fluid_cell", 4)))

func _add_misc_recipes(recipes: Array) -> void:
	_add_recipe_if_valid(recipes, _hand(
			"craft_stone_plate",
			"misc",
			[_mat("dust", "stone", 1)],
			_item_key("stone_plate", 1),
			"hammer"))
	_add_recipe_if_valid(recipes, _hand(
			"craft_wood_plank_hand",
			"misc",
			[_mat("dust", "wood", 1)],
			_mat("plate", "wood", 2)))
	_add_recipe_if_valid(recipes, _bench(
			"craft_wood_plank",
			"misc",
			[_mat("dust", "wood", 1)],
			_mat("plate", "wood", 4),
			"saw"))
	_add_recipe_if_valid(recipes, _hand(
			"craft_workbench",
			"misc",
			[_mat("plate", "wood", 4)],
			_item_key("workbench", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"craft_stick",
			"misc",
			[_mat("plate", "wood", 2)],
			_mat("rod", "wood", 4)))
	_add_recipe_if_valid(recipes, _hand(
			"craft_coal_block",
			"misc",
			[_mat("gem", "coal", 9)],
			_mat("block", "coal", 1)), false)
	_add_recipe_if_valid(recipes, _hand(
			"craft_firebrick",
			"misc",
			[_mat("ingot", "brick", 4), _mat("dust", "coal", 1)],
			_item_key("firebrick", 1)), false)
	_add_recipe_if_valid(recipes, _bench(
			"craft_furnace",
			"misc",
			[_mat("dust", "stone", 8)],
			_item_key("stone_furnace", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"craft_ladder",
			"misc",
			[_mat("rod", "wood", 4)],
			_item_key("ladder", 1)))

func _machine_recipes() -> Array:
	return [
		{
			"name": "macerate_copper_crushed",
			"machine_type": "macerator",
			"category": "ore_processing",
			"min_tier": TIER_LV,
			"eu_per_tick": 2,
			"duration_ticks": 100,
			"inputs": [_mat("crushed", "copper", 1)],
			"outputs": [_mat("dust", "copper", 2)],
		},
		{
			"name": "macerate_iron_crushed",
			"machine_type": "macerator",
			"category": "ore_processing",
			"min_tier": TIER_LV,
			"eu_per_tick": 2,
			"duration_ticks": 100,
			"inputs": [_mat("crushed", "iron", 1)],
			"outputs": [_mat("dust", "iron", 2)],
		},
	]
