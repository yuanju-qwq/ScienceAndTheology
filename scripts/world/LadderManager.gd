class_name LadderManager extends Node

const SURFACE: StringName = &"surface"

signal ladder_placed(layer: StringName, cell: Vector2i)
signal ladder_removed(layer: StringName, cell: Vector2i)

var _ladders: Dictionary = {}


func place_ladder(layer: StringName, cell: Vector2i) -> bool:
	if not _is_valid_layer(layer):
		return false

	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if _ladders.has(key):
		return false

	_ladders[key] = true
	ladder_placed.emit(layer, cell)
	return true


func remove_ladder(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if not _ladders.has(key):
		return false

	_ladders.erase(key)
	ladder_removed.emit(layer, cell)
	return true


func has_ladder(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	return _ladders.has(key)


func get_all_ladders() -> Array:
	var result: Array = []
	for key in _ladders.keys():
		var parts := String(key).split(",")
		if parts.size() == 3:
			result.append({
				"layer": parts[0],
				"cell": Vector2i(int(parts[1]), int(parts[2]))
			})
	return result


func get_other_side(layer: StringName, cell: Vector2i) -> Dictionary:
	return { "layer": layer, "cell": cell }


func clear() -> void:
	_ladders.clear()


func _is_valid_layer(layer: StringName) -> bool:
	return layer == SURFACE
