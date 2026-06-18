extends SceneTree

var _failed := false

func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)

func _run() -> void:
	GDCraftingManager.clear()
	GDCraftingManager.clear_load_report()

	var ok := GDCraftingManager.register_recipe({
		"name": "test_non_material_item_key",
		"category": "test",
		"inputs": [{"item_key": "gt_hammer", "count": 1}],
		"output": {"item_key": "workbench", "count": 1},
	})

	_expect(ok, "register_recipe returned false: %s" %
			str(GDCraftingManager.get_load_report()))

	var recipe := GDCraftingManager.find_recipe("test_non_material_item_key")
	_expect(not recipe.is_empty(), "registered recipe was not found")
	var workbench_id := GDCraftingManager.get_item_id_by_key("workbench")
	_expect(workbench_id > 0, "workbench key did not resolve")
	_expect(recipe.get("output_item_id", -1) == workbench_id,
			"workbench key resolved to an unexpected item id")

	var inputs: Array = recipe.get("inputs", [])
	_expect(inputs.size() == 1, "expected one input")
	var hammer_id := GDCraftingManager.get_item_id_by_key("gt_hammer")
	_expect(hammer_id > 0, "gt_hammer key did not resolve")
	_expect(inputs[0].get("item_id", -1) == hammer_id,
			"gt_hammer key resolved to an unexpected item id")

	if _failed:
		return
	print("Item key lookup test passed: non-material item_key values resolve.")
	quit(0)

func _expect(condition: bool, message: String) -> void:
	if condition:
		return
	_failed = true
	push_error("Item key lookup test failed: " + message)
	quit(1)
