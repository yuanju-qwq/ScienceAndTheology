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

	BuiltinFluids.register_all()
	_register_fuels()
	_register_fluid_fuels()
	BuiltinRunes.register_all()
	BuiltinGlyphs.register_all()
	BuiltinRitualRecipes.register_all()
	BuiltinSublimationPaths.register_all()
	BuiltinElixirs.register_all()
	BuiltinDroppedOrgans.register_all()
	_register_species()
	BuiltinBiomeOverrides.register_all()
	_register_machine_types()

	GDCraftingManager.clear_load_report()
	GDRecipeDatabase.clear_load_report()

	var crafting_registered: int = GDCraftingManager.register_recipes(_crafting_recipes())
	var processing_registered: int = GDRecipeDatabase.register_recipes(_processing_recipes())
	var recipe_maps: PackedStringArray = GDRecipeDatabase.get_machine_types()
	print("ContentDatabase: loaded crafting=%d processing=%d recipe_maps=%s" %
			[crafting_registered, processing_registered, str(recipe_maps)])
	print("ContentDatabase: processing recipe split fuel_furnace=%d electric_macerator=%d" %
			[GDRecipeDatabase.get_recipes_for_machine("furnace").size(),
			GDRecipeDatabase.get_recipes_for_machine("macerator").size()])

	var crafting_report: Array = GDCraftingManager.get_load_report()
	if not crafting_report.is_empty():
		push_warning("ContentDatabase crafting load report: %s" % str(crafting_report))
	var machine_report: Array = GDRecipeDatabase.get_load_report()
	if not machine_report.is_empty():
		push_warning("ContentDatabase processing load report: %s" % str(machine_report))

# 热重载入口：复位所有 C++ registry + GD 缓存，然后重新执行完整注册流程。
# 适用于 GDScript 改动 BuiltinXxx 后不重启工程刷新内容。
# 注意：地形内容 registry（planet configs/biome rules/ore veins）已 freeze 为
# 不可变快照，不在此流程内重建；其引用的 material ID 由注册顺序决定，只要
# MaterialDefinitions._ALL_MATERIALS 顺序不变即保持稳定。
func reload_content() -> void:
	print("ContentDatabase: reload_content start")
	# 1. 复位所有 C++ registry（material/item/fluid/fuel/magic/source_law/species/biome）
	GDRegistryBank.reset_all()
	# 2. 清空 recipe 缓存
	GDCraftingManager.clear()
	GDRecipeDatabase.clear()
	# 3. 重新注册材质（reset_all 已复位 MaterialRegistry，需重新 register + finalize）
	MaterialDefinitions.register_all()
	MaterialDefinitions.register_compounds()
	GDMaterialRegistry.finalize()
	# 4. 重新注册 GD 端 item 缓存
	ItemDatabase.reload()
	# 5. 重新执行内容注册
	_loaded = false
	load_content()
	print("ContentDatabase: reload_content complete")

# Register solid item fuels (coal, wood) from GDScript.
# These were previously hardcoded in C++ FuelRegistry::register_builtin_fuels().
func _register_fuels() -> void:
	var _reg := func(item_key: String, name: String, title_key: String, burn_ticks: int) -> void:
		var item_id := GDCraftingManager.get_item_id_by_key(item_key)
		if item_id <= 0:
			push_warning("ContentDatabase: fuel item_key '%s' not found" % item_key)
			return
		GDFuelRegistry.register_fuel({
			"name": name,
			"item_id": item_id,
			"burn_ticks": burn_ticks,
			"title_key": title_key,
			"category": 0,  # SOLID
		})

	# Coal gem
	_reg.call("gem.coal", "coal", "fuel.coal", 200)
	# Wood items — log (dust.wood), plank (plate.wood), stick (rod.wood)
	_reg.call("dust.wood", "wood_log", "fuel.wood_log", 120)
	_reg.call("plate.wood", "wood_plank", "fuel.wood_plank", 60)
	_reg.call("rod.wood", "stick", "fuel.stick", 30)


# Register liquid/gas fuels (migrated from C++ FuelRegistry::register_builtin_fluid_fuels()).
# Fluid fuels reference fluid ids by name, so this must run after
# BuiltinFluids.register_all().
# category: 1 = LIQUID, 2 = GAS (see FuelCategory enum in fuel_def.hpp).
func _register_fluid_fuels() -> void:
	var _reg := func(fluid_name: String, name: String, title_key: String, burn_ticks: int, category: int) -> void:
		if GDFluidRegistry.get_fluid_id(fluid_name) < 0:
			push_warning("ContentDatabase: fluid fuel '%s' references unknown fluid '%s'" % [name, fluid_name])
			return
		GDFuelRegistry.register_fuel({
			"name": name,
			"fluid_name": fluid_name,
			"burn_ticks": burn_ticks,
			"title_key": title_key,
			"category": category,
		})

	# --- Liquid fuels (category 1) ---
	_reg.call("lava", "lava_fuel", "fuel.lava", 10000, 1)
	_reg.call("fuel_diesel", "diesel_fuel", "fuel.diesel", 5000, 1)
	_reg.call("oil", "oil_fuel", "fuel.oil", 3000, 1)

	# --- Gaseous fuels (category 2) ---
	_reg.call("natural_gas", "natural_gas_fuel", "fuel.natural_gas", 3000, 2)
	_reg.call("hydrogen", "hydrogen_fuel", "fuel.hydrogen", 1000, 2)


# Register all built-in creature species via GDScript.
func _register_species() -> void:
	BuiltinSpecies.register_all()


# Register machine type metadata for built-in machines.
func _register_machine_types() -> void:
	# Campfire: uses furnace processing logic but with campfire GUI.
	if not GDMachineDefinitionRegistry.has_definition("campfire"):
		GDMachineDefinitionRegistry.register_definition({
			"type_key": "campfire",
			"display_name": "Campfire",
			"input_slots": 1,
			"output_slots": 1,
		})


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

func _fluid(fluid_name: String, amount: int) -> Dictionary:
	return {
		"type": "fluid",
		"fluid_name": fluid_name,
		"amount": amount,
	}

func _recipe(
		recipe_name: String,
		category: String,
		required_station: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	var data := {
		"name": recipe_name,
		"category": category,
		"required_station": required_station,
		"inputs": inputs,
		"output": output,
	}
	if not required_tool.is_empty():
		data["required_tool"] = required_tool
	return data

func _hand(
		recipe_name: String,
		category: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	return _recipe(recipe_name, category, "", inputs, output, required_tool)

func _bench(
		recipe_name: String,
		category: String,
		inputs: Array,
		output: Dictionary,
		required_tool: String = "") -> Dictionary:
	return _recipe(recipe_name, category, "workbench", inputs, output, required_tool)

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
	var output_dict: Dictionary = recipe.get("output", {})
	if not _stack_is_valid(output_dict):
		return false
	for input: Dictionary in recipe.get("inputs", []):
		if not _stack_is_valid(input as Dictionary):
			return false
	return true

func _stack_is_valid(stack: Dictionary) -> bool:
	if stack.has("item_id"):
		@warning_ignore("unsafe_cast")
		var item_id_value: int = stack["item_id"] as int
		return GDCraftingManager.is_valid_item(item_id_value)
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
	_add_crop_recipes(recipes)
	_add_tfc_recipes(recipes)
	_add_sfm_recipes(recipes)
	return recipes

# SFM (Steve's Factory Manager) — Manager block and inventory cable recipes.
func _add_sfm_recipes(recipes: Array) -> void:
	# Flow Manager: crafted on a workbench using iron plates, a circuit, and redstone.
	_add_recipe_if_valid(recipes, _bench(
			"craft_sfm_manager",
			"misc",
			[_mat("plate", "iron", 4),
			 _item_key("circuit.basic", 1),
			 _mat("dust", "redstone", 2)],
			_item_key("sfm_manager", 1)))
	# Inventory Cable: crafted by hand using iron nuggets and string.
	_add_recipe_if_valid(recipes, _hand(
			"craft_sfm_cable",
			"misc",
			[_mat("nugget", "iron", 3),
			 _mat("rod", "wood", 1)],
			_item_key("sfm_cable", 4)))

func _add_material_compression_recipes(recipes: Array) -> void:
	var compressible_metals := [
		"iron", "copper", "tin", "lead", "bronze", "steel",
		"gold", "silver", "nickel", "zinc", "aluminum", "brass",
		"invar", "electrum", "wrought_iron", "bismuth", "antimony",
	]
	for material: String in compressible_metals:
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
	for material: String in dust_materials:
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
	for material: String in wire_metals:
		_add_recipe_if_valid(recipes, _bench(
				"craft_%s_wire" % material,
				"wires",
				[_mat("ingot", material, 1)],
				_mat("wire", material, 2),
				"wire_cutter"), false)

func _add_cable_recipes(recipes: Array) -> void:
	var cable_metals := ["copper", "tin", "gold", "silver",
			"aluminum", "nickel", "iron", "steel"]
	for material: String in cable_metals:
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
			[_mat("plate", "iron", 2), _mat("plate", "rubber", 1),
					_item_key("electric_motor_lv", 1)],
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
			"craft_campfire",
			"misc",
			[_mat("rod", "wood", 4), _mat("dust", "coal", 1)],
			_item_key("campfire", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"craft_ladder",
			"misc",
			[_mat("rod", "wood", 4)],
			_item_key("ladder", 1)))

	# Tree species wood processing: 1 log -> 4 planks (hand: 2, bench+saw: 4).
	var tree_species := [
		["oak", "log.oak", "plank.oak"],
		["birch", "log.birch", "plank.birch"],
		["spruce", "log.spruce", "plank.spruce"],
		["acacia", "log.acacia", "plank.acacia"],
		["maple", "log.maple", "plank.maple"],
		["sequoia", "log.sequoia", "plank.sequoia"],
		["cherry", "log.cherry", "plank.cherry"],
		["olive", "log.olive", "plank.olive"],
	]
	for species: Array in tree_species:
		var sp_name: String = species[0]
		var log_key: String = species[1]
		var plank_key: String = species[2]
		_add_recipe_if_valid(recipes, _hand(
				"craft_%s_plank_hand" % sp_name,
				"misc",
				[_item_key(log_key, 1)],
				_item_key(plank_key, 2)))
		_add_recipe_if_valid(recipes, _bench(
				"craft_%s_plank" % sp_name,
				"misc",
				[_item_key(log_key, 1)],
				_item_key(plank_key, 4),
				"saw"))

# Crop processing recipes (Tier 1 planting system).
# Wheat → flour (hand + hammer), cotton → fiber → cloth (workbench).
func _add_crop_recipes(recipes: Array) -> void:
	# Mill wheat into flour (hand recipe, requires hammer).
	_add_recipe_if_valid(recipes, _hand(
			"mill_wheat_to_flour",
			"crops",
			[_item_key("crop.wheat", 1)],
			_item_key("flour", 1),
			"hammer"))
	# Spin cotton into fiber (workbench).
	_add_recipe_if_valid(recipes, _bench(
			"spin_cotton_to_fiber",
			"crops",
			[_item_key("crop.cotton", 3)],
			_item_key("fiber.cotton", 1)))
	# Weave fiber into cloth (workbench).
	_add_recipe_if_valid(recipes, _bench(
			"weave_fiber_to_cloth",
			"crops",
			[_item_key("fiber.cotton", 2)],
			_item_key("cloth", 1)))

# --- TFC expansion crafting recipes ---
func _add_tfc_recipes(recipes: Array) -> void:
	# Knapping tool assembly: tool head + stick → tool
	_add_recipe_if_valid(recipes, _hand(
			"assemble_stone_axe",
			"tfc_tools",
			[_item_key("stone_axe_head", 1), _mat("rod", "wood", 1)],
			_item_key("stone_axe", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"assemble_stone_shovel",
			"tfc_tools",
			[_item_key("stone_shovel_head", 1), _mat("rod", "wood", 1)],
			_item_key("stone_shovel", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"assemble_stone_hoe",
			"tfc_tools",
			[_item_key("stone_hoe_head", 1), _mat("rod", "wood", 1)],
			_item_key("stone_hoe", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"assemble_stone_knife",
			"tfc_tools",
			[_item_key("stone_knife_head", 1), _mat("rod", "wood", 1)],
			_item_key("stone_knife", 1)))

	# Clay ball → unfired pottery (hand shaping)
	_add_recipe_if_valid(recipes, _hand(
			"shape_clay_bowl",
			"tfc_pottery",
			[_item_key("clay_ball", 3)],
			_item_key("unfired_bowl", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"shape_clay_jug",
			"tfc_pottery",
			[_item_key("clay_ball", 5)],
			_item_key("unfired_jug", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"shape_clay_crucible",
			"tfc_pottery",
			[_item_key("clay_ball", 7)],
			_item_key("unfired_crucible", 1)))
	_add_recipe_if_valid(recipes, _hand(
			"shape_clay_brick",
			"tfc_pottery",
			[_item_key("clay_ball", 2)],
			_item_key("unfired_brick", 1)))

	# Anvil forging: iron bloom + hammer → wrought iron ingot
	_add_recipe_if_valid(recipes, _hand(
			"forge_wrought_iron",
			"tfc_metallurgy",
			[_item_key("iron_bloom", 1)],
			_item_key("wrought_iron_ingot", 1),
			"hammer"))

	# Crucible steel: wrought iron + coal dust → steel ingot
	_add_recipe_if_valid(recipes, _hand(
			"crucible_steel",
			"tfc_metallurgy",
			[_item_key("wrought_iron_ingot", 1), _item_key("coal_dust", 1)],
			_item_key("steel_ingot", 1)))


func _processing_recipes() -> Array:
	# Processing recipe maps include fuel executors and electric machines.
	# The furnace map is fuel-fired; electric machines consume EU separately.
	return [
		{
			"name": "smelt_copper_crushed_to_ingot",
			"machine_type": "furnace",
			"category": "smelting",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 100,
			"inputs": [_mat("crushed", "copper", 1)],
			"outputs": [_mat("ingot", "copper", 1)],
		},
		{
			"name": "smelt_iron_crushed_to_ingot",
			"machine_type": "furnace",
			"category": "smelting",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 100,
			"inputs": [_mat("crushed", "iron", 1)],
			"outputs": [_mat("ingot", "iron", 1)],
		},
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
		# Bake flour into bread (furnace, Tier 1 planting system).
		{
			"name": "bake_flour_to_bread",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 80,
			"inputs": [_item_key("flour", 1)],
			"outputs": [_item_key("bread", 1)],
		},
		# Campfire cooking: raw meat → cooked meat (furnace machine_type).
		{
			"name": "cook_glow_deer",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 200,
			"inputs": [_item_key("meat.raw.glow_deer", 1)],
			"outputs": [_item_key("meat.cooked.glow_deer", 1)],
		},
		{
			"name": "cook_rock_lizard",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 300,
			"inputs": [_item_key("meat.raw.rock_lizard", 1)],
			"outputs": [_item_key("meat.cooked.rock_lizard", 1)],
		},
		{
			"name": "cook_thunderbird",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 200,
			"inputs": [_item_key("meat.raw.thunderbird", 1)],
			"outputs": [_item_key("meat.cooked.thunderbird", 1)],
		},
		{
			"name": "cook_sea_serpent",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 400,
			"inputs": [_item_key("meat.raw.sea_serpent", 1)],
			"outputs": [_item_key("meat.cooked.sea_serpent", 1)],
		},
		{
			"name": "cook_blaze_beast",
			"machine_type": "furnace",
			"category": "food",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 300,
			"inputs": [_item_key("meat.raw.blaze_beast", 1)],
			"outputs": [_item_key("meat.cooked.blaze_beast", 1)],
		},
		# TFC: charcoal pit (machine_type: "charcoal_pit")
		{
			"name": "charcoal_burn",
			"machine_type": "charcoal_pit",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 24000,
			"inputs": [_mat("dust", "wood", 16)],
			"outputs": [_item_key("charcoal", 8)],
		},
		# TFC: pit kiln pottery firing
		{
			"name": "fire_unfired_bowl",
			"machine_type": "pit_kiln",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 8000,
			"inputs": [_item_key("unfired_bowl", 1)],
			"outputs": [_item_key("fired_bowl", 1)],
		},
		{
			"name": "fire_unfired_jug",
			"machine_type": "pit_kiln",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 8000,
			"inputs": [_item_key("unfired_jug", 1)],
			"outputs": [_item_key("fired_jug", 1)],
		},
		{
			"name": "fire_unfired_crucible",
			"machine_type": "pit_kiln",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 12000,
			"inputs": [_item_key("unfired_crucible", 1)],
			"outputs": [_item_key("fired_crucible", 1)],
		},
		{
			"name": "fire_unfired_brick",
			"machine_type": "pit_kiln",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 6000,
			"inputs": [_item_key("unfired_brick", 1)],
			"outputs": [_item_key("refractory_brick", 1)],
		},
		# TFC: bloomery iron smelting
		{
			"name": "bloomery_iron",
			"machine_type": "bloomery",
			"category": "tfc_processing",
			"min_tier": TIER_ULV,
			"eu_per_tick": 0,
			"duration_ticks": 12000,
			"inputs": [_mat("crushed", "iron", 5), _item_key("charcoal", 5)],
			"outputs": [_item_key("iron_bloom", 1)],
		},
	]
