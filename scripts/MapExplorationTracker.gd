class_name MapExplorationTracker
extends Node

signal cell_discovered(layer_id: StringName, cell_position: Vector2i)
signal exploration_changed

@export var reveal_radius := 0

var _visited_cells_by_layer := {}


func mark_visited(layer_id: StringName, cell_position: Vector2i) -> void:
	if reveal_radius <= 0:
		_mark_single_cell_visited(layer_id, cell_position)
		return

	for y in range(cell_position.y - reveal_radius, cell_position.y + reveal_radius + 1):
		for x in range(cell_position.x - reveal_radius, cell_position.x + reveal_radius + 1):
			_mark_single_cell_visited(layer_id, Vector2i(x, y))


func has_visited(layer_id: StringName, cell_position: Vector2i) -> bool:
	var layer_cells: Dictionary = _get_layer_cells(layer_id, false)
	if layer_cells.is_empty():
		return false

	return layer_cells.has(_make_cell_key(cell_position))


func get_visited_cells(layer_id: StringName) -> Array[Vector2i]:
	var visited_cells: Array[Vector2i] = []
	var layer_cells: Dictionary = _get_layer_cells(layer_id, false)

	for cell_position in layer_cells.values():
		visited_cells.append(cell_position)

	return visited_cells


func get_visited_count(layer_id: StringName) -> int:
	return _get_layer_cells(layer_id, false).size()


func clear_layer(layer_id: StringName) -> void:
	_visited_cells_by_layer.erase(layer_id)
	exploration_changed.emit()


func clear_all() -> void:
	_visited_cells_by_layer.clear()
	exploration_changed.emit()


func get_save_data() -> Dictionary:
	var save_data := {}

	for layer_key in _visited_cells_by_layer.keys():
		var serialized_cells := []
		for cell_position in _visited_cells_by_layer[layer_key].values():
			serialized_cells.append({
				"x": cell_position.x,
				"y": cell_position.y,
			})

		save_data[layer_key] = serialized_cells

	return save_data


func load_save_data(save_data: Dictionary) -> void:
	_visited_cells_by_layer.clear()

	for layer_key in save_data.keys():
		var layer_id := StringName(layer_key)
		var layer_cells: Dictionary = _get_layer_cells(layer_id, true)

		for cell_data in save_data[layer_key]:
			if not cell_data is Dictionary:
				continue

			var cell_position := Vector2i(int(cell_data.get("x", 0)), int(cell_data.get("y", 0)))
			layer_cells[_make_cell_key(cell_position)] = cell_position

	exploration_changed.emit()


func _mark_single_cell_visited(layer_id: StringName, cell_position: Vector2i) -> void:
	var layer_cells: Dictionary = _get_layer_cells(layer_id, true)
	var cell_key := _make_cell_key(cell_position)
	if layer_cells.has(cell_key):
		return

	layer_cells[cell_key] = cell_position
	cell_discovered.emit(layer_id, cell_position)
	exploration_changed.emit()


func _get_layer_cells(layer_id: StringName, create_if_missing: bool) -> Dictionary:
	var layer_key := String(layer_id)
	if not _visited_cells_by_layer.has(layer_key):
		if create_if_missing:
			_visited_cells_by_layer[layer_key] = {}
		else:
			return {}

	return _visited_cells_by_layer[layer_key]


func _make_cell_key(cell_position: Vector2i) -> String:
	return "%d:%d" % [cell_position.x, cell_position.y]
