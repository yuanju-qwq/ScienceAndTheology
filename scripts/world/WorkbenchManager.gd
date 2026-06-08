class_name WorkbenchManager extends Node

var _workbenches: Dictionary = {}  # "layer,x,y" -> true

signal workbench_placed(layer: StringName, cell: Vector2i)
signal workbench_removed(layer: StringName, cell: Vector2i)


func has_workbench(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	return _workbenches.has(key)


func place_workbench(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if _workbenches.has(key):
		return false
	_workbenches[key] = true
	workbench_placed.emit(layer, cell)
	return true


func remove_workbench(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if not _workbenches.erase(key):
		return false
	workbench_removed.emit(layer, cell)
	return true


func get_all_workbenches() -> Array:
	var result: Array = []
	for key in _workbenches.keys():
		var parts := String(key).split(",")
		if parts.size() == 3:
			result.append({
				"layer": parts[0],
				"cell": Vector2i(int(parts[1]), int(parts[2]))
			})
	return result


func clear() -> void:
	_workbenches.clear()
