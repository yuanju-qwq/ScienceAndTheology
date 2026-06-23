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

# Cache shell candidate orders while the player remains in the same chunk.
# This avoids re-enumerating the tangent disk every frame.
@export var shell_order_cache_enabled := true
@export var shell_order_cache_max_entries := 128

# Keep chunks that were directly modified/accessed by gameplay alive while the
# player remains nearby. This is the first DeepUndergroundChunk on-demand hook:
# a chunk outside the active surface shell can still be loaded/rendered when the
# player is digging or interacting with it.
@export var deep_chunk_keepalive_enabled := true
@export var deep_chunk_keepalive_radius := 4
@export var deep_player_chunk_radius := 1
@export var deep_chunk_max_forced_entries := 512

# Enable per-chunk horizontal LOD inside the visible shell. Near chunks show
# LOD0_Full with collision; outer chunks show LOD1_Simplified without collision.
@export var horizontal_lod_enabled := true

# Radius, in chunk units on the local tangent plane, that keeps full collision.
# Chunks outside this radius remain visible but switch to simplified mesh.
@export var horizontal_lod0_radius := 4

# Optional visible mesh cap, in chunk units. 0 means use the current view_radius.
# This is a safety valve for testing before a real heightfield/clipmap LOD exists.
@export var horizontal_lod1_radius := 0

# Last visible shell set. Used by _on_chunk_ready() to avoid meshing prefetched
# chunks that are outside view_radius.
var _shell_wanted_visible: Dictionary = {}

# Candidate order cache. Keyed by dimension, player chunk, radius, planet radius,
# local center, and active shell band.
var _shell_order_cache: Dictionary = {}

# Chunks explicitly touched by gameplay. These bypass surface-shell clipping while
# the player stays close, then age out through the max-entry cap or dimension switch.
var _forced_shell_chunks: Dictionary = {}

# Per-frame shell/horizontal LOD counters exposed through get_streaming_metrics().
var _last_shell_visible_candidates := 0
var _last_shell_load_candidates := 0
var _last_shell_order_cache_hits := 0
var _last_shell_order_cache_misses := 0
var _last_forced_keepalive_chunks := 0
var _last_deep_player_chunks := 0
var _last_horizontal_lod0_chunks := 0
var _last_horizontal_lod1_chunks := 0
var _last_horizontal_hidden_chunks := 0


func set_active_dimension(dimension_id: StringName) -> void:
	_shell_wanted_visible.clear()
	_shell_order_cache.clear()
	_forced_shell_chunks.clear()
	super.set_active_dimension(dimension_id)


func get_streaming_metrics() -> Dictionary:
	var metrics := super.get_streaming_metrics()
	metrics["shell_streaming_enabled"] = use_planet_shell_streaming
	metrics["shell_prefetch_loaded_radius"] = shell_prefetch_loaded_radius
	metrics["shell_order_cache_enabled"] = shell_order_cache_enabled
	metrics["shell_order_cache_entries"] = _shell_order_cache.size()
	metrics["shell_order_cache_hits"] = _last_shell_order_cache_hits
	metrics["shell_order_cache_misses"] = _last_shell_order_cache_misses
	metrics["shell_visible_candidates"] = _last_shell_visible_candidates
	metrics["shell_load_candidates"] = _last_shell_load_candidates
	metrics["deep_chunk_keepalive_enabled"] = deep_chunk_keepalive_enabled
	metrics["forced_shell_chunks"] = _forced_shell_chunks.size()
	metrics["forced_keepalive_chunks"] = _last_forced_keepalive_chunks
	metrics["deep_player_chunks"] = _last_deep_player_chunks
	metrics["horizontal_lod_enabled"] = horizontal_lod_enabled
	metrics["horizontal_lod0_radius"] = horizontal_lod0_radius
	metrics["horizontal_lod1_radius"] = horizontal_lod1_radius if horizontal_lod1_radius > 0 else view_radius
	metrics["horizontal_lod0_chunks"] = _last_horizontal_lod0_chunks
	metrics["horizontal_lod1_chunks"] = _last_horizontal_lod1_chunks
	metrics["horizontal_hidden_chunks"] = _last_horizontal_hidden_chunks
	return metrics


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0
	_last_shell_order_cache_hits = 0
	_last_shell_order_cache_misses = 0
	_last_forced_keepalive_chunks = 0
	_last_deep_player_chunks = 0

	# Space stations already use build-aware loading in the parent class.
	if _is_station_dimension and _active_station != null:
		_shell_wanted_visible.clear()
		_last_shell_visible_candidates = 0
		_last_shell_load_candidates = 0
		_refresh_station_chunks()
		return

	if not use_planet_shell_streaming:
		_shell_wanted_visible.clear()
		_last_shell_visible_candidates = 0
		_last_shell_load_candidates = 0
		super._refresh_chunks(player_chunk)
		return

	var player := get_node_or_null(player_node_path) as Node3D
	var planet := _get_active_streaming_planet()
	if player == null or planet == null:
		_shell_wanted_visible.clear()
		_last_shell_visible_candidates = 0
		_last_shell_load_candidates = 0
		super._refresh_chunks(player_chunk)
		return

	var visible_order := _get_shell_chunk_order_cached(
			player_chunk, player.global_position, planet, view_radius)
	_last_shell_visible_candidates = visible_order.size()
	var wanted_visible: Dictionary = {}
	for chunk in visible_order:
		wanted_visible[chunk] = true

	var load_order := visible_order
	if shell_prefetch_loaded_radius and loaded_radius > view_radius:
		load_order = _get_shell_chunk_order_cached(
				player_chunk, player.global_position, planet, loaded_radius)
	_last_shell_load_candidates = load_order.size()

	# Add on-demand deep chunks after the normal shell pass. These chunks are not
	# required to intersect the surface shell, but they must remain close to the
	# player so old tunnels do not stay rendered forever.
	var keepalive_chunks := _collect_keepalive_chunks(player_chunk, player.global_position, planet)
	for chunk in keepalive_chunks:
		wanted_visible[chunk] = true
		if not load_order.has(chunk):
			load_order.append(chunk)

	_shell_wanted_visible = wanted_visible

	# Request data for the larger loading shell. The overridden _on_chunk_ready()
	# only enqueues a view when the chunk is currently in wanted_visible.
	for chunk in load_order:
		_ensure_chunk_loaded(chunk)

	for key: Vector3i in _visible_chunks.keys():
		if not wanted_visible.has(key):
			_remove_chunk_view(key)

	for chunk: Vector3i in wanted_visible.keys():
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


func on_terrain_cell_synced(dimension: StringName, chunk: Vector3i, local: Vector3i,
		old_material: int, new_material: int) -> void:
	if deep_chunk_keepalive_enabled and dimension == active_dimension:
		_mark_forced_shell_chunk(chunk, &"terrain_cell_synced")
	super.on_terrain_cell_synced(dimension, chunk, local, old_material, new_material)


func notify_block_placed(dimension: StringName, cell: Vector3i) -> void:
	if deep_chunk_keepalive_enabled and dimension == active_dimension \
			and not _is_station_dimension:
		notify_deep_access_cell(dimension, cell, 0)
	super.notify_block_placed(dimension, cell)


func notify_deep_access_cell(dimension: StringName, cell: Vector3i,
		radius_chunks: int = 0) -> void:
	if not deep_chunk_keepalive_enabled or dimension != active_dimension:
		return
	var center_chunk := cell_to_chunk(cell)
	var radius := maxi(0, radius_chunks)
	for dx in range(-radius, radius + 1):
		for dy in range(-radius, radius + 1):
			for dz in range(-radius, radius + 1):
				_mark_forced_shell_chunk(
						center_chunk + Vector3i(dx, dy, dz), &"deep_access")


# Apply horizontal chunk LOD every frame because the local tangent distance changes
# continuously as the player moves. Planet-level LOD 2+ still hides chunk roots so
# the existing proxy sphere / low-poly / billboard system remains authoritative.
func _update_chunk_visibility_for_lod() -> void:
	if not horizontal_lod_enabled:
		_last_horizontal_lod0_chunks = 0
		_last_horizontal_lod1_chunks = 0
		_last_horizontal_hidden_chunks = 0
		_enable_all_visible_chunk_collisions()
		super._update_chunk_visibility_for_lod()
		return

	if _universe_manager != null:
		_refresh_lod_manager_from_universe()

	var planet_lod := PlanetLodManager.LOD_REAL_CHUNKS
	if _planet_lod_manager != null:
		planet_lod = _planet_lod_manager.get_current_lod_level()
	_current_chunk_lod = planet_lod

	_last_horizontal_lod0_chunks = 0
	_last_horizontal_lod1_chunks = 0
	_last_horizontal_hidden_chunks = 0

	if _planet_lod_manager != null and planet_lod >= PlanetLodManager.LOD_PROXY_SPHERE:
		for chunk: Vector3i in _visible_chunks.keys():
			var entry: Dictionary = _visible_chunks[chunk]
			var root: Node3D = entry.get("root")
			if root:
				root.visible = false
			_set_entry_collision_enabled(chunk, entry, false)
			_last_horizontal_hidden_chunks += 1
		return

	var player := get_node_or_null(player_node_path) as Node3D
	var planet := _get_active_streaming_planet()
	if player == null or planet == null:
		_enable_all_visible_chunk_collisions()
		super._update_chunk_visibility_for_lod()
		return

	var full_radius_blocks := float(maxi(0, horizontal_lod0_radius) * CHUNK_SIZE)
	var mesh_radius_chunks := horizontal_lod1_radius if horizontal_lod1_radius > 0 else view_radius
	var mesh_radius_blocks := float(maxi(0, mesh_radius_chunks) * CHUNK_SIZE)

	for chunk: Vector3i in _visible_chunks.keys():
		var entry: Dictionary = _visible_chunks[chunk]
		var root: Node3D = entry.get("root")
		var full: Node3D = entry.get("full")
		var simplified: Node3D = entry.get("simplified")
		var tangent_distance := _chunk_tangent_distance(
				chunk, player.global_position, planet)

		var within_mesh_radius := tangent_distance <= mesh_radius_blocks + float(CHUNK_SIZE)
		var show_full := planet_lod == PlanetLodManager.LOD_REAL_CHUNKS \
				and tangent_distance <= full_radius_blocks + float(CHUNK_SIZE)
		var show_simplified := within_mesh_radius and not show_full

		if root:
			root.visible = within_mesh_radius
		if full:
			full.visible = show_full
		if simplified:
			simplified.visible = show_simplified

		_set_entry_collision_enabled(chunk, entry, show_full)

		if show_full:
			_last_horizontal_lod0_chunks += 1
		elif show_simplified:
			_last_horizontal_lod1_chunks += 1
		else:
			_last_horizontal_hidden_chunks += 1


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


func _collect_keepalive_chunks(
		player_chunk: Vector3i,
		player_pos: Vector3,
		planet: PlanetDescriptor) -> Array[Vector3i]:
	var result: Array[Vector3i] = []
	if not deep_chunk_keepalive_enabled:
		return result

	var seen: Dictionary = {}
	for chunk: Vector3i in _forced_shell_chunks.keys():
		if not _is_chunk_near_player_chunk(chunk, player_chunk, deep_chunk_keepalive_radius):
			continue
		seen[chunk] = true
		result.append(chunk)
	_last_forced_keepalive_chunks = result.size()

	# When the player is already outside the active surface shell, keep a small
	# cube around them loaded. This is the runtime path for tunnels below H_below
	# or vertical towers above H_above.
	var altitude := planet.local_surface_altitude_at(player_pos)
	var outside_active_shell := altitude < -planet.active_shell_below \
			or altitude > planet.active_shell_above
	if outside_active_shell:
		var radius := maxi(0, deep_player_chunk_radius)
		for dx in range(-radius, radius + 1):
			for dy in range(-radius, radius + 1):
				for dz in range(-radius, radius + 1):
					var chunk := player_chunk + Vector3i(dx, dy, dz)
					if seen.has(chunk):
						continue
					seen[chunk] = true
					result.append(chunk)
					_last_deep_player_chunks += 1
	return result


func _mark_forced_shell_chunk(chunk: Vector3i, reason: StringName) -> void:
	_forced_shell_chunks[chunk] = {
		"reason": reason,
		"time_msec": Time.get_ticks_msec(),
	}
	if deep_chunk_max_forced_entries <= 0:
		return
	if _forced_shell_chunks.size() <= deep_chunk_max_forced_entries:
		return
	_prune_forced_shell_chunks(deep_chunk_max_forced_entries)


func _prune_forced_shell_chunks(max_entries: int) -> void:
	while _forced_shell_chunks.size() > max_entries:
		var oldest_chunk: Variant = null
		var oldest_time := INF
		for chunk in _forced_shell_chunks.keys():
			var entry: Dictionary = _forced_shell_chunks[chunk]
			var time_msec := float(entry.get("time_msec", 0.0))
			if time_msec < oldest_time:
				oldest_time = time_msec
				oldest_chunk = chunk
		if oldest_chunk == null:
			break
		_forced_shell_chunks.erase(oldest_chunk)


func _is_chunk_near_player_chunk(chunk: Vector3i, player_chunk: Vector3i,
		radius: int) -> bool:
	var r := maxi(0, radius)
	return abs(chunk.x - player_chunk.x) <= r \
			and abs(chunk.y - player_chunk.y) <= r \
			and abs(chunk.z - player_chunk.z) <= r


func _get_shell_chunk_order_cached(
		player_chunk: Vector3i,
		player_pos: Vector3,
		planet: PlanetDescriptor,
		radius_chunks: int) -> Array[Vector3i]:
	if not shell_order_cache_enabled:
		_last_shell_order_cache_misses += 1
		return _compute_shell_chunk_order(player_pos, planet, radius_chunks)

	var key := _make_shell_order_cache_key(player_chunk, planet, radius_chunks)
	if _shell_order_cache.has(key):
		_last_shell_order_cache_hits += 1
		return _shell_order_cache[key]

	_last_shell_order_cache_misses += 1
	var order := _compute_shell_chunk_order(player_pos, planet, radius_chunks)
	if shell_order_cache_max_entries > 0 \
			and _shell_order_cache.size() >= shell_order_cache_max_entries:
		_shell_order_cache.clear()
	_shell_order_cache[key] = order
	return order


func _make_shell_order_cache_key(
		player_chunk: Vector3i,
		planet: PlanetDescriptor,
		radius_chunks: int) -> String:
	return "%s|pc=%d,%d,%d|r=%d|pr=%.1f|lc=%.1f,%.1f,%.1f|shell=%.1f,%.1f" % [
		String(active_dimension),
		player_chunk.x, player_chunk.y, player_chunk.z,
		radius_chunks,
		planet.planet_radius,
		planet.local_center.x, planet.local_center.y, planet.local_center.z,
		planet.active_shell_above, planet.active_shell_below,
	]


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


func _chunk_tangent_distance(
		chunk: Vector3i,
		player_pos: Vector3,
		planet: PlanetDescriptor) -> float:
	var center := planet.local_center
	var up := player_pos - center
	if up.length_squared() < 0.0001:
		up = Vector3.UP
	else:
		up = up.normalized()
	var surface_point := center + up * planet.planet_radius
	var relative := _chunk_center(chunk) - surface_point
	var radial_height := relative.dot(up)
	var tangent := relative - up * radial_height
	return tangent.length()


func _enable_all_visible_chunk_collisions() -> void:
	for chunk: Vector3i in _visible_chunks.keys():
		var entry: Dictionary = _visible_chunks[chunk]
		_set_entry_collision_enabled(chunk, entry, true)


func _set_entry_collision_enabled(chunk: Vector3i, entry: Dictionary,
		enabled: bool) -> void:
	var current: Variant = entry.get("collision_enabled", null)
	if current != null and bool(current) == enabled:
		return
	var full: Node3D = entry.get("full")
	_set_collision_shapes_disabled(full, not enabled)
	entry["collision_enabled"] = enabled
	_visible_chunks[chunk] = entry


func _set_collision_shapes_disabled(root: Node, disabled: bool) -> void:
	if root == null:
		return
	if root is CollisionShape3D:
		(root as CollisionShape3D).disabled = disabled
	for child in root.get_children():
		_set_collision_shapes_disabled(child, disabled)


func _chunk_center(chunk: Vector3i) -> Vector3:
	return Vector3(
		(float(chunk.x) + 0.5) * float(CHUNK_SIZE),
		(float(chunk.y) + 0.5) * float(CHUNK_SIZE),
		(float(chunk.z) + 0.5) * float(CHUNK_SIZE))
