# PlanetShellChunkRendererBridge — shell-aware chunk streaming for large planets.
#
# This subclass keeps ChunkRendererBridge's rendering/meshing path intact and
# only replaces the planet chunk-selection step. It streams a local tangent disk
# around the player, then clips it by the active planet's surface-relative shell
# band. Deep underground chunks outside that shell are intentionally left to
# later on-demand generation.
class_name PlanetShellChunkRendererBridge
extends ChunkRendererBridge

# Enable the large-planet shell streamer. Disable to use the parent bridge's
# original GDChunkHelper.compute_visible_chunks() behavior.
@export var use_planet_shell_streaming := true

# If true, request chunk data out to loaded_radius but only mesh chunks inside
# view_radius. The overridden _on_chunk_ready() prevents prefetch chunks from
# creating views until they enter the visible shell.
@export var shell_prefetch_loaded_radius := true

# Last visible shell set. Used by _on_chunk_ready() to avoid meshing prefetched
# chunks that are outside view_radius.
var _shell_wanted_visible: Dictionary = {}


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0

	# Space stations already use build-aware loading in the parent class.
	if _is_station_dimension and _active_station != null:
		_refresh_station_chunks()
		return

	if not use_planet_shell_streaming:
		super._refresh_chunks(player_chunk)
		return

	var player := get_node_or_null(player_node_path) as Node3D
	var planet := _get_active_streaming_planet()
	if player == null or planet == null:
		super._refresh_chunks(player_chunk)
		return

	var visible_order := _compute_shell_chunk_order(player.global_position, planet, view_radius)
	var wanted_visible: Dictionary = {}
	for chunk in visible_order:
		wanted_visible[chunk] = true
	_shell_wanted_visible = wanted_visible

	var load_order := visible_order
	if shell_prefetch_loaded_radius and loaded_radius > view_radius:
		load_order = _compute_shell_chunk_order(player.global_position, planet, loaded_radius)

	# Request data for the larger loading shell. The overridden _on_chunk_ready()
	# only enqueues a view when the chunk is currently in wanted_visible.
	for chunk in load_order:
		_ensure_chunk_loaded(chunk)

	for key: Vector3i in _visible_chunks.keys():
		if not wanted_visible.has(key):
			_remove_chunk_view(key)

	for chunk in visible_order:
		if _visible_chunks.has(chunk):
			continue
		if not _pending_view_queue.has(chunk):
			_enqueue_chunk_view(chunk)

	_process_visible_queue()


# Keep prefetched chunks data-only until they enter the visible shell.
func _on_chunk_ready(dimension: String, chunk_x: int, chunk_y: int, chunk_z: int) -> void:
	if StringName(dimension) != active_dimension:
		return
	var chunk := Vector3i(chunk_x, chunk_y, chunk_z)
	_tracked_chunks[chunk] = true

	if _shell_wanted_visible.is_empty() or _shell_wanted_visible.has(chunk):
		if not _pending_view_queue.has(chunk):
			_enqueue_chunk_view(chunk)

	# Existing neighbors may have been meshed while this chunk was absent.
	# Rebuild only visible neighbors; prefetched data-only chunks stay unmeshed.
	for offset in FACE_NEIGHBOR_OFFSETS:
		var neighbor := chunk + offset
		if not _visible_chunks.has(neighbor):
			continue
		_enqueue_chunk_rebuild(neighbor)


func _get_active_streaming_planet() -> PlanetDescriptor:
	if _universe_manager == null:
		_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	if _universe_manager == null:
		return null
	var planet := _universe_manager.active_planet
	if planet == null or planet.is_star:
		return null
	if planet.dimension_id != active_dimension:
		return null
	return planet


func _compute_shell_chunk_order(
		player_pos: Vector3,
		planet: PlanetDescriptor,
		radius_chunks: int) -> Array[Vector3i]:
	var result: Array[Vector3i] = []
	if radius_chunks < 0:
		return result

	var center := planet.local_center
	var up := player_pos - center
	if up.length_squared() < 0.0001:
		up = Vector3.UP
	else:
		up = up.normalized()

	var surface_point := center + up * planet.planet_radius
	var tangent_a := _make_tangent_axis(up)
	var tangent_b := up.cross(tangent_a).normalized()

	var above_layers := maxi(0, int(ceil(planet.active_shell_above / float(CHUNK_SIZE))))
	var below_layers := maxi(0, int(ceil(planet.active_shell_below / float(CHUNK_SIZE))))
	# Always keep the current chunk's immediate vertical neighbors available so
	# spawn/building transitions cannot fall through gaps while shell parameters
	# are small.
	above_layers = maxi(above_layers, 1)
	below_layers = maxi(below_layers, 1)

	var seen: Dictionary = {}
	for ring in range(radius_chunks + 1):
		for tx in range(-ring, ring + 1):
			for tz in range(-ring, ring + 1):
				if maxi(abs(tx), abs(tz)) != ring:
					continue
				if tx * tx + tz * tz > radius_chunks * radius_chunks:
					continue
				for h in range(-below_layers, above_layers + 1):
					var sample := surface_point \
							+ tangent_a * float(tx * CHUNK_SIZE) \
							+ tangent_b * float(tz * CHUNK_SIZE) \
							+ up * float(h * CHUNK_SIZE)
					var chunk := world_position_to_chunk(sample)
					if seen.has(chunk):
						continue
					if not _chunk_intersects_active_shell(chunk, planet):
						continue
					seen[chunk] = true
					result.append(chunk)
	return result


func _make_tangent_axis(up: Vector3) -> Vector3:
	var reference := Vector3.FORWARD
	if absf(reference.dot(up)) > 0.95:
		reference = Vector3.RIGHT
	return reference.cross(up).normalized()


func _chunk_intersects_active_shell(chunk: Vector3i, planet: PlanetDescriptor) -> bool:
	var center := _chunk_center(chunk)
	var altitude := planet.local_surface_altitude_at(center)
	var half_diag := sqrt(3.0) * float(CHUNK_SIZE) * 0.5
	return altitude >= -planet.active_shell_below - half_diag \
			and altitude <= planet.active_shell_above + half_diag


func _chunk_center(chunk: Vector3i) -> Vector3:
	return Vector3(
		(float(chunk.x) + 0.5) * float(CHUNK_SIZE),
		(float(chunk.y) + 0.5) * float(CHUNK_SIZE),
		(float(chunk.z) + 0.5) * float(CHUNK_SIZE))
