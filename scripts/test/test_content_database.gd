extends SceneTree

func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)

func _run() -> void:
	var content_database: Node = root.get_node_or_null("ContentDatabase")
	if content_database == null:
		content_database = load("res://scripts/content/ContentDatabase.gd").new()
		root.add_child(content_database)
	_expect(content_database.has_method("load_content"),
			"ContentDatabase is missing load_content")
	content_database.call("load_content")

	_assert_crafting_content()
	_assert_machine_content()

	print("ContentDatabase test passed: %d crafting recipes, %d macerator recipes registered." %
			[GDCraftingManager.get_recipe_count(),
			GDRecipeDatabase.get_recipes_for_machine("macerator").size()])
	quit(0)

func _assert_crafting_content() -> void:
	_expect(GDCraftingManager.get_recipe_count() == 97,
			"expected 97 crafting recipes, got %d" % GDCraftingManager.get_recipe_count())

	var hammer := GDCraftingManager.find_recipe("craft_hammer")
	_expect(not hammer.is_empty(), "missing craft_hammer recipe")
	_expect(hammer.get("required_station", "") == "workbench",
			"craft_hammer should require workbench")
	_expect(hammer.get("output_item_id", -1) == _non_material(0),
			"craft_hammer output should be gt_hammer")

	var workbench := GDCraftingManager.find_recipe("craft_workbench")
	_expect(not workbench.is_empty(), "missing craft_workbench recipe")
	_expect(workbench.get("output_item_id", -1) == _non_material(52),
			"craft_workbench output should be workbench")

	var copper_block := GDCraftingManager.find_recipe("compress_copper_ingot_to_block")
	_expect(not copper_block.is_empty(), "missing copper ingot compression recipe")
	_expect(copper_block.get("output_item_id", -1) == _mat_item(5, 15),
			"copper compression output should be copper block")

	var hull := GDCraftingManager.find_recipe("craft_machine_hull_basic")
	_expect(not hull.is_empty(), "missing basic machine hull recipe")
	_expect(hull.get("output_item_id", -1) == _non_material(10),
			"machine hull output should be machine_hull_basic")

func _assert_machine_content() -> void:
	var recipes := GDRecipeDatabase.get_recipes_for_machine("macerator")
	_expect(recipes.size() == 2,
			"expected 2 macerator recipes, got %d" % recipes.size())

	var copper := GDRecipeDatabase.find_recipe("macerator", "macerate_copper_crushed")
	_expect(not copper.is_empty(), "missing copper macerator recipe")
	_expect(copper.get("duration_ticks", 0) == 100,
			"unexpected copper recipe duration")

	var outputs: Array = copper.get("outputs", [])
	_expect(outputs.size() == 1, "expected copper recipe to have 1 output")
	var output: Dictionary = outputs[0]
	_expect(output.get("item_id", -1) == _mat_item(5, 0),
			"unexpected copper recipe output item")
	_expect(output.get("amount", 0) == 2,
			"unexpected copper recipe output amount")

	var iron := GDRecipeDatabase.find_recipe("macerator", "macerate_iron_crushed")
	_expect(not iron.is_empty(), "missing iron macerator recipe")

func _expect(condition: bool, message: String) -> void:
	if condition:
		return
	push_error("ContentDatabase test failed: " + message)
	quit(1)

func _mat_item(material_id: int, form: int) -> int:
	return 1 + material_id * 31 + form

func _non_material(offset: int) -> int:
	return 1 + 113 * 31 + 1 + offset
