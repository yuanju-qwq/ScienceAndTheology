class_name PlayerMining extends Node

signal block_mined(cell: Vector2i, layer: StringName, drops: Array)

const REACH := 4.0
const EQUIP_MAIN_HAND := 0

var _player: Node2D
var _player_controller: Node
var _command_server: GameCommandServer


func set_player(p: Node) -> void:
	_player = p as Node2D
	_player_controller = p
	if p and p.has_method(&"get_command_server"):
		_command_server = p.get_command_server()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
			_try_mine()


func _find_chunk_bridge() -> ChunkRendererBridge:
	var node: Node = get_parent()
	while node:
		if node is ChunkRendererBridge:
			return node
		node = node.get_parent()
	return null


func _get_map_layer() -> Node:
	var node: Node = get_parent()
	while node:
		if node.has_method(&"get_current_tile_layer"):
			return node
		node = node.get_parent()
	return null


func _get_crosshair() -> PlayerCrosshair:
	if _player_controller and _player_controller.has_node("../UI/Crosshair"):
		return _player_controller.get_node("../UI/Crosshair") as PlayerCrosshair
	return null


func _try_mine() -> void:
	if _player == null:
		return

	if _command_server == null:
		return

	var crosshair: PlayerCrosshair = _get_crosshair()
	if crosshair == null or not crosshair.has_target:
		return

	var cell: Vector2i = crosshair.target_cell
	var layer: StringName = crosshair.target_layer
	var cx: int = crosshair.target_chunk.x
	var cy: int = crosshair.target_chunk.y
	var lx: int = crosshair.target_local.x
	var ly: int = crosshair.target_local.y

	var map_layer: Node = _get_map_layer()
	if map_layer == null:
		return
	var tile_layer: TileMapLayer = map_layer.get_current_tile_layer() if map_layer.has_method(&"get_current_tile_layer") else null
	if tile_layer == null:
		return
	var world_pos := tile_layer.to_global(tile_layer.map_to_local(cell))
	var dist := _player.global_position.distance_to(world_pos)
	if dist > REACH:
		return

	var world_data: GDWorldData = _command_server.get_world_data()
	if world_data == null:
		return

	var dict: Dictionary = world_data.get_terrain_cell(layer, cx, cy, lx, ly)
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

	var result: Dictionary = _command_server.submit_command({
		"type": GameCommandServer.COMMAND_MINE_BLOCK,
		"layer": layer,
		"chunk": Vector2i(cx, cy),
		"local": Vector2i(lx, ly),
		"cell": cell,
		"expected_material": material,
	})
	if not bool(result.get("ok", false)):
		return

	block_mined.emit(cell, layer, result.get("drops", []))


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
