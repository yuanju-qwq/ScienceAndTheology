class_name PlayerCrosshair extends Control

var target_cell: Vector2i = Vector2i.ZERO
var target_layer: StringName = &""
var target_chunk: Vector2i = Vector2i.ZERO
var target_local: Vector2i = Vector2i.ZERO
var has_target := false

@onready var world_data_provider := _find_world_data_provider()


func _find_world_data_provider():
	var node = get_parent()
	while node:
		if node.has_method(&"get_world_data"):
			return node
		node = node.get_parent()
	return null


func get_world_data():
	if world_data_provider and world_data_provider.has_method(&"get_world_data"):
		return world_data_provider.get_world_data()
	return null


func get_map_layer():
	var node = get_parent()
	while node:
		if node is MapLayerController or (node.has_method(&"is_current_layer") and "current_layer" in node):
			return node
		node = node.get_parent()
	return null


func update_target(global_mouse_pos: Vector2) -> void:
	var map_layer := get_map_layer()
	if map_layer == null:
		has_target = false
		return

	var layer_name := map_layer.current_layer
	var tile_layer := map_layer.get_current_tile_layer() if map_layer.has_method(&"get_current_tile_layer") else null
	if tile_layer == null:
		has_target = false
		return

	var cell := tile_layer.local_to_map(tile_layer.to_local(global_mouse_pos))
	target_cell = cell
	target_layer = layer_name

	var chunk_size := 32
	var cx := int(floor(float(cell.x) / chunk_size))
	var cy := int(floor(float(cell.y) / chunk_size))
	var lx := cell.x - cx * chunk_size
	var ly := cell.y - cy * chunk_size
	target_chunk = Vector2i(cx, cy)
	target_local = Vector2i(lx, ly)

	has_target = true
	queue_redraw()


func _draw() -> void:
	if not has_target:
		return
	var map_layer := get_map_layer()
	if map_layer == null:
		return
	var tile_layer := map_layer.get_current_tile_layer() if map_layer.has_method(&"get_current_tile_layer") else null
	if tile_layer == null:
		return

	var world_pos := tile_layer.to_global(tile_layer.map_to_local(target_cell))
	var cam := get_viewport().get_camera_2d()
	var screen_pos := (world_pos - cam.global_position) * cam.zoom + get_viewport().size / 2
	var local_pos := screen_pos - global_position

	var cell_size := Vector2(32, 32) * cam.zoom
	var rect := Rect2(local_pos - cell_size / 2, cell_size)
	draw_rect(rect, Color(1, 1, 1, 0.3), false, 2.0)
