# LadderManager — tracks ladder positions in 3D voxel coordinates.
# Provides 3D teleportation between ladder endpoints.
class_name LadderManager extends Node

const OVERWORLD: StringName = &"overworld"
const LADDER_HEIGHT_OFFSET := 3

signal ladder_placed(dimension: StringName, cell: Vector3i)
signal ladder_removed(dimension: StringName, cell: Vector3i)

var _ladders: Dictionary = {}


func place_ladder(dimension: StringName, cell: Vector3i) -> bool:
	if not _is_valid_dimension(dimension):
		return false

	var key := _make_key(dimension, cell)
	if _ladders.has(key):
		return false

	_ladders[key] = true
	ladder_placed.emit(dimension, cell)
	return true


func remove_ladder(dimension: StringName, cell: Vector3i) -> bool:
	var key := _make_key(dimension, cell)
	if not _ladders.has(key):
		return false

	_ladders.erase(key)
	ladder_removed.emit(dimension, cell)
	return true


func has_ladder(dimension: StringName, cell: Vector3i) -> bool:
	return _ladders.has(_make_key(dimension, cell))


func get_all_ladders() -> Array:
	var result: Array = []
	for key in _ladders.keys():
		var parts := String(key).split(",")
		if parts.size() == 4:
			result.append({
				"dimension": parts[0],
				"cell": Vector3i(int(parts[1]), int(parts[2]), int(parts[3]))
			})
	return result


# Returns the other side of a ladder connection.
# A ladder at cell (x, y, z) connects to (x, y + LADDER_HEIGHT_OFFSET, z).
# If the current cell is the bottom, the top is returned; if at the top, the bottom is returned.
func get_other_side(dimension: StringName, cell: Vector3i) -> Dictionary:
	var top_cell := Vector3i(cell.x, cell.y + LADDER_HEIGHT_OFFSET, cell.z)
	if has_ladder(dimension, top_cell):
		return { "dimension": dimension, "cell": top_cell }

	var bottom_cell := Vector3i(cell.x, cell.y - LADDER_HEIGHT_OFFSET, cell.z)
	if has_ladder(dimension, bottom_cell):
		return { "dimension": dimension, "cell": bottom_cell }

	return { "dimension": dimension, "cell": cell }


func clear() -> void:
	_ladders.clear()


func _is_valid_dimension(dimension: StringName) -> bool:
	return dimension == OVERWORLD


func _make_key(dimension: StringName, cell: Vector3i) -> String:
	return "%s,%d,%d,%d" % [dimension, cell.x, cell.y, cell.z]
