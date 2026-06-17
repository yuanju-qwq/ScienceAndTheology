class_name WorkbenchManager extends Node

var _workbenches: Dictionary = {}  # "dimension,x,y,z" -> true

signal workbench_placed(dimension: StringName, cell: Vector3i)
signal workbench_removed(dimension: StringName, cell: Vector3i)


func has_workbench(dimension: StringName, cell: Vector3i) -> bool:
	return _workbenches.has(_make_key(dimension, cell))


func place_workbench(dimension: StringName, cell: Vector3i) -> bool:
	var key := _make_key(dimension, cell)
	if _workbenches.has(key):
		return false
	_workbenches[key] = true
	workbench_placed.emit(dimension, cell)
	return true


func remove_workbench(dimension: StringName, cell: Vector3i) -> bool:
	var key := _make_key(dimension, cell)
	if not _workbenches.erase(key):
		return false
	workbench_removed.emit(dimension, cell)
	return true


func get_all_workbenches() -> Array:
	var result: Array = []
	for key in _workbenches.keys():
		var parts := String(key).split(",")
		if parts.size() == 4:
			result.append({
				"dimension": parts[0],
				"cell": Vector3i(int(parts[1]), int(parts[2]), int(parts[3]))
			})
	return result


func clear() -> void:
	_workbenches.clear()


func _make_key(dimension: StringName, cell: Vector3i) -> String:
	return "%s,%d,%d,%d" % [dimension, cell.x, cell.y, cell.z]
