extends SceneTree

const OVERWORLD: StringName = &"overworld"

var _failed := false
var _synced := 0
var _last_synced_cell := Vector3i.ZERO

func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var content_database: Node = root.get_node_or_null("ContentDatabase")
	if content_database == null:
		content_database = load("res://scripts/content/ContentDatabase.gd").new()
		root.add_child(content_database)
	content_database.call("load_content")

	var furnace_manager := FurnaceManager.new()
	var command_server := GameCommandServer.new()
	root.add_child(furnace_manager)
	root.add_child(command_server)

	var inventory := GDPlayerInventory.new()
	inventory.init(9, 4)
	var equipment := GDPlayerEquipment.new()
	command_server.register_player(GameCommandServer.LOCAL_PLAYER_ID, inventory, equipment)
	command_server.set_furnace_manager(furnace_manager)

	command_server.furnace_synced.connect(func(dimension: StringName, cell: Vector3i) -> void:
		if dimension == OVERWORLD:
			_synced += 1
			_last_synced_cell = cell
	)

	var cell := Vector3i(2, 0, 3)
	var furnace_item := _non_material(53)
	var copper_crushed := _mat_item(5, 5)
	var copper_ingot := _mat_item(5, 12)
	var fuel_item := _find_test_fuel()
	if not _expect(fuel_item > 0, "no registered item fuel found for furnace test"):
		return

	inventory.set_slot(0, furnace_item, 1)
	inventory.set_slot(1, copper_crushed, 1)
	inventory.set_slot(2, fuel_item, 1)

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_OBJECT,
		"player_id": GameCommandServer.LOCAL_PLAYER_ID,
		"object_type": GameCommandServer.OBJECT_FURNACE,
		"dimension": OVERWORLD,
		"cell": cell,
		"item_id": furnace_item,
	})
	if not _expect(not result.get("ok", false),
			"legacy cell-only placement command was accepted"):
		return
	if not _expect(inventory.count_item(furnace_item) == 1,
			"rejected legacy placement consumed its item"):
		return

	result = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_OBJECT,
		"player_id": GameCommandServer.LOCAL_PLAYER_ID,
		"object_type": GameCommandServer.OBJECT_FURNACE,
		"dimension": OVERWORLD,
		"cell": cell,
		"anchor_cell": cell + Vector3i.DOWN,
		"build_direction": Vector3i.UP,
		"build_mode": GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES,
		"item_id": furnace_item,
	})
	if not _expect(result.get("ok", false), "place furnace rejected: %s" % str(result)):
		return
	if not _expect(furnace_manager.has_furnace(OVERWORLD, cell),
			"furnace was not placed in C++ state"):
		return

	result = command_server.submit_command({
		"type": GameCommandServer.COMMAND_FURNACE_INSERT_INPUT,
		"player_id": GameCommandServer.LOCAL_PLAYER_ID,
		"dimension": OVERWORLD,
		"cell": cell,
		"item_id": copper_crushed,
	})
	if not _expect(result.get("ok", false), "insert input rejected: %s" % str(result)):
		return

	result = command_server.submit_command({
		"type": GameCommandServer.COMMAND_FURNACE_INSERT_FUEL,
		"player_id": GameCommandServer.LOCAL_PLAYER_ID,
		"dimension": OVERWORLD,
		"cell": cell,
		"item_id": fuel_item,
	})
	if not _expect(result.get("ok", false), "insert fuel rejected: %s" % str(result)):
		return

	furnace_manager.tick_all(5.1)
	var data = furnace_manager.get_furnace(OVERWORLD, cell)
	if not _expect(data != null, "furnace data missing after tick"):
		return
	if not _expect(data.output_item_id == copper_ingot,
			"expected copper ingot output, got item %d" % data.output_item_id):
		return
	if not _expect(data.output_count == 1,
			"expected one copper ingot, got %d" % data.output_count):
		return

	result = command_server.submit_command({
		"type": GameCommandServer.COMMAND_FURNACE_TAKE_OUTPUT,
		"player_id": GameCommandServer.LOCAL_PLAYER_ID,
		"dimension": OVERWORLD,
		"cell": cell,
	})
	if not _expect(result.get("ok", false), "take output rejected: %s" % str(result)):
		return
	if not _expect(inventory.count_item(copper_ingot) == 1,
			"inventory did not receive smelted output"):
		return
	if not _expect(_synced >= 4,
			"expected furnace_synced from command chain, got %d" % _synced):
		return
	if not _expect(_last_synced_cell == cell, "last furnace sync cell mismatch"):
		return
	if not _expect(furnace_manager.get_dirty_furnaces().size() > 0,
			"C++ furnace dirty state was not recorded"):
		return

	print("Furnace command server test passed: GameCommandServer -> C++ state -> sync.")
	quit(0)


func _expect(condition: bool, message: String) -> bool:
	if condition:
		return true
	if _failed:
		return false
	_failed = true
	push_error("Furnace command server test failed: " + message)
	quit(1)
	return false


func _find_test_fuel() -> int:
	var candidates := [
		_mat_item(2, 8), # coal gem
		_mat_item(112, 0), # wood log
		_mat_item(112, 16), # wood plank
		_mat_item(112, 19), # stick
	]
	for item_id in candidates:
		if GDFuelRegistry.get_burn_ticks(item_id) > 0:
			return item_id
	return 0


func _mat_item(material_id: int, form: int) -> int:
	return 1 + material_id * 31 + form


func _non_material(offset: int) -> int:
	return 1 + 113 * 31 + 1 + offset
