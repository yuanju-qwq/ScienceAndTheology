# SurfaceColumnIndex — runtime index for planet-surface columns touched below/above
# the active shell.
#
# A column is identified by projecting a chunk center radially onto the planet
# surface, then converting that surface point to a chunk coordinate. This works
# for a spherical planet better than a global X/Z column because the local up
# direction changes around the planet.
class_name SurfaceColumnIndex
extends Resource

const DEFAULT_CHUNK_SIZE := 32

# dimension -> column_key -> column_data
# column_data = {
#   "surface_chunk": Vector3i,
#   "min_layer": int,
#   "max_layer": int,
#   "last_touched_msec": int,
#   "layers": { layer_int: true },
#   "chunks": { "x,y,z": { "chunk": Vector3i, "layer": int, ... } }
# }
var _columns_by_dimension: Dictionary = {}
var _chunk_to_column: Dictionary = {}


func clear() -> void:
	_columns_by_dimension.clear()
	_chunk_to_column.clear()


func is_empty() -> bool:
	return _columns_by_dimension.is_empty()


func get_column_count(dimension: StringName = &"") -> int:
	if dimension == &"":
		var total := 0
		for dim in _columns_by_dimension.keys():
			var columns: Dictionary = _columns_by_dimension[dim]
			total += columns.size()
		return total
	var dim_key := String(dimension)
	if not _columns_by_dimension.has(dim_key):
		return 0
	var columns: Dictionary = _columns_by_dimension[dim_key]
	return columns.size()


func get_indexed_chunk_count(dimension: StringName = &"") -> int:
	if dimension == &"":
		return _chunk_to_column.size()
	var prefix := String(dimension) + "|"
	var total := 0
	for key in _chunk_to_column.keys():
		if String(key).begins_with(prefix):
			total += 1
	return total


func record_chunk(
		dimension: StringName,
		chunk: Vector3i,
		planet_center: Vector3,
		planet_radius: float,
		chunk_size: int = DEFAULT_CHUNK_SIZE,
		reason: StringName = &"deep_access") -> void:
	if planet_radius <= 0.0 or chunk_size <= 0:
		return
	var projection := project_chunk_to_surface_column(
			chunk, planet_center, planet_radius, chunk_size)
	if projection.is_empty():
		return
	var dim_key := String(dimension)
	if not _columns_by_dimension.has(dim_key):
		_columns_by_dimension[dim_key] = {}
	var columns: Dictionary = _columns_by_dimension[dim_key]

	var surface_chunk: Vector3i = projection["surface_chunk"]
	var layer := int(projection["radial_layer"])
	var column_key := _column_key(surface_chunk)
	var chunk_key := _chunk_key(chunk)
	var now := Time.get_ticks_msec()

	if not columns.has(column_key):
		columns[column_key] = {
			"surface_chunk": surface_chunk,
			"min_layer": layer,
			"max_layer": layer,
			"last_touched_msec": now,
			"layers": {},
			"chunks": {},
		}

	var column: Dictionary = columns[column_key]
	column["min_layer"] = mini(int(column.get("min_layer", layer)), layer)
	column["max_layer"] = maxi(int(column.get("max_layer", layer)), layer)
	column["last_touched_msec"] = now

	var layers: Dictionary = column.get("layers", {})
	layers[layer] = true
	column["layers"] = layers

	var chunks: Dictionary = column.get("chunks", {})
	chunks[chunk_key] = {
		"chunk": chunk,
		"layer": layer,
		"altitude": float(projection["altitude"]),
		"reason": String(reason),
		"last_touched_msec": now,
	}
	column["chunks"] = chunks
	columns[column_key] = column
	_columns_by_dimension[dim_key] = columns
	_chunk_to_column[_dimension_chunk_key(dimension, chunk)] = column_key


func has_chunk(dimension: StringName, chunk: Vector3i) -> bool:
	return _chunk_to_column.has(_dimension_chunk_key(dimension, chunk))


func get_nearby_indexed_chunks(
		dimension: StringName,
		player_position: Vector3,
		planet_center: Vector3,
		planet_radius: float,
		chunk_size: int = DEFAULT_CHUNK_SIZE,
		column_radius: int = 1) -> Array[Vector3i]:
	var result: Array[Vector3i] = []
	if planet_radius <= 0.0 or chunk_size <= 0:
		return result
	var player_chunk := _world_position_to_chunk(player_position, chunk_size)
	var projection := project_chunk_to_surface_column(
			player_chunk, planet_center, planet_radius, chunk_size)
	if projection.is_empty():
		return result
	var dim_key := String(dimension)
	if not _columns_by_dimension.has(dim_key):
		return result
	var columns: Dictionary = _columns_by_dimension[dim_key]
	var center_surface_chunk: Vector3i = projection["surface_chunk"]
	var radius := maxi(0, column_radius)
	var seen: Dictionary = {}

	for sx in range(center_surface_chunk.x - radius, center_surface_chunk.x + radius + 1):
		for sy in range(center_surface_chunk.y - radius, center_surface_chunk.y + radius + 1):
			for sz in range(center_surface_chunk.z - radius, center_surface_chunk.z + radius + 1):
				var column_key := _column_key(Vector3i(sx, sy, sz))
				if not columns.has(column_key):
					continue
				var column: Dictionary = columns[column_key]
				var chunks: Dictionary = column.get("chunks", {})
				for chunk_key in chunks.keys():
					var entry: Dictionary = chunks[chunk_key]
					var chunk: Vector3i = entry.get("chunk", Vector3i.ZERO)
					if seen.has(chunk):
						continue
					seen[chunk] = true
					result.append(chunk)
	return result


func project_chunk_to_surface_column(
		chunk: Vector3i,
		planet_center: Vector3,
		planet_radius: float,
		chunk_size: int = DEFAULT_CHUNK_SIZE) -> Dictionary:
	if planet_radius <= 0.0 or chunk_size <= 0:
		return {}
	var center := _chunk_center(chunk, chunk_size)
	var radial := center - planet_center
	if radial.length_squared() < 0.0001:
		return {}
	var up := radial.normalized()
	var surface_point := planet_center + up * planet_radius
	var surface_chunk := _world_position_to_chunk(surface_point, chunk_size)
	var altitude := radial.length() - planet_radius
	var layer := int(floor(altitude / float(chunk_size)))
	return {
		"surface_chunk": surface_chunk,
		"altitude": altitude,
		"radial_layer": layer,
	}


func to_dict() -> Dictionary:
	return {
		"schema": 1,
		"columns_by_dimension": _columns_by_dimension,
	}


func from_dict(data: Dictionary) -> void:
	clear()
	var columns_data: Dictionary = data.get("columns_by_dimension", {})
	_columns_by_dimension = columns_data.duplicate(true)
	_rebuild_chunk_lookup()


func _rebuild_chunk_lookup() -> void:
	_chunk_to_column.clear()
	for dim_key in _columns_by_dimension.keys():
		var columns: Dictionary = _columns_by_dimension[dim_key]
		for column_key in columns.keys():
			var column: Dictionary = columns[column_key]
			var chunks: Dictionary = column.get("chunks", {})
			for chunk_key in chunks.keys():
				var entry: Dictionary = chunks[chunk_key]
				var chunk: Vector3i = entry.get("chunk", Vector3i.ZERO)
				_chunk_to_column["%s|%s" % [dim_key, _chunk_key(chunk)]] = column_key


func _chunk_center(chunk: Vector3i, chunk_size: int) -> Vector3:
	return Vector3(
		(float(chunk.x) + 0.5) * float(chunk_size),
		(float(chunk.y) + 0.5) * float(chunk_size),
		(float(chunk.z) + 0.5) * float(chunk_size))


func _world_position_to_chunk(position: Vector3, chunk_size: int) -> Vector3i:
	return Vector3i(
		int(floor(position.x / float(chunk_size))),
		int(floor(position.y / float(chunk_size))),
		int(floor(position.z / float(chunk_size))))


func _column_key(surface_chunk: Vector3i) -> String:
	return "%d,%d,%d" % [surface_chunk.x, surface_chunk.y, surface_chunk.z]


func _chunk_key(chunk: Vector3i) -> String:
	return "%d,%d,%d" % [chunk.x, chunk.y, chunk.z]


func _dimension_chunk_key(dimension: StringName, chunk: Vector3i) -> String:
	return "%s|%s" % [String(dimension), _chunk_key(chunk)]
