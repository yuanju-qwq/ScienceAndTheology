class_name PlayerMining extends Node

signal block_mined(cell: Vector2i, layer: StringName, item_id: int, count: int)

const REACH := 4.0
const EQUIP_MAIN_HAND := 0

var _player: Node2D
var _player_controller: Node


func set_player(p: Node) -> void:
	_player = p as Node2D
	_player_controller = p


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			_try_mine()


func _get_world_data():
	var bridge := _find_chunk_bridge()
	if bridge and bridge.has_method(&"get_world_data"):
		return bridge.get_world_data()
	return null


func _find_chunk_bridge():
	var node = get_parent()
	while node:
		if node is ChunkRendererBridge:
			return node
		node = node.get_parent()
	return null


func _get_map_layer():
	var node = get_parent()
	while node:
		if node.has_method(&"get_current_tile_layer"):
			return node
		node = node.get_parent()
	return null


func _get_crosshair():
	if _player_controller and _player_controller.has_node("../UI/Crosshair"):
		return _player_controller.get_node("../UI/Crosshair")
	return null


func _try_mine() -> void:
	if _player == null:
		return

	var world_data := _get_world_data()
	if world_data == null:
		return

	var crosshair := _get_crosshair()
	if crosshair == null or not crosshair.has_target:
		return

	var cell := crosshair.target_cell
	var layer := crosshair.target_layer
	var cx := crosshair.target_chunk.x
	var cy := crosshair.target_chunk.y
	var lx := crosshair.target_local.x
	var ly := crosshair.target_local.y

	var map_layer := _get_map_layer()
	if map_layer == null:
		return
	var tile_layer := map_layer.get_current_tile_layer() if map_layer.has_method(&"get_current_tile_layer") else null
	if tile_layer == null:
		return
	var world_pos := tile_layer.to_global(tile_layer.map_to_local(cell))
	var dist := _player.global_position.distance_to(world_pos)
	if dist > REACH:
		return

	var dict := world_data.get_terrain_cell(layer, cx, cy, lx, ly)
	if dict.is_empty():
		return
	var material := int(dict["material"])
	var mineable := bool(dict["is_mineable"])
	if not mineable or material == GDWorldData.MAT_AIR:
		return

	var required_tool_type := TerrainDropTable.get_required_tool_type(material)
	var required_level := TerrainDropTable.get_required_mining_level(material)

	var has_right_tool := true
	if required_tool_type >= 0:
		has_right_tool = false
		if _check_has_tool(required_tool_type, required_level):
			has_right_tool = true
	if not has_right_tool:
		return

	var ok := world_data.set_terrain_cell(layer, cx, cy, lx, ly, GDWorldData.MAT_AIR)
	if not ok:
		return

	var drops := TerrainDropTable.get_drops(material)
	for drop in drops:
		var item_id := int(drop.item_id)
		var count := int(drop.count)
		if item_id <= 0:
			continue
		block_mined.emit(cell, layer, item_id, count)


func _check_has_tool(tool_type: int, min_level: int) -> bool:
	var item_id := _get_equipped_item()
	if item_id <= 0:
		return false
	var stats := ItemDatabase.get_tool_stats(item_id)
	if stats == null:
		return false
	var matches_type := false
	match tool_type:
		ItemDatabase.ITEM_WOODEN_PICKAXE:
			matches_type = stats.tool_type == ToolDef.ToolType.PICKAXE
		ItemDatabase.ITEM_WOODEN_AXE:
			matches_type = stats.tool_type == ToolDef.ToolType.AXE
		ItemDatabase.ITEM_WOODEN_SHOVEL:
			matches_type = stats.tool_type == ToolDef.ToolType.SHOVEL
		_:
			matches_type = true
	return matches_type and stats.mining_level >= min_level


func _get_equipped_item() -> int:
	if _player_controller and _player_controller.has_method(&"get_equipped_item_id"):
		return _player_controller.get_equipped_item_id()
	return 0
