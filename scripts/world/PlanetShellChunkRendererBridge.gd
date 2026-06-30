# PlanetShellChunkRendererBridge — shell-aware chunk streaming for large planets.
#
# This subclass keeps ChunkRendererBridge's rendering/meshing path intact and
# only replaces the planet chunk-selection step. It streams a local tangent disk
# around the player, then clips it by the active planet's surface-relative shell
# band. Deep underground chunks outside that shell are intentionally left to
# later on-demand generation.
class_name PlanetShellChunkRendererBridge
extends ChunkRendererBridge

const CHUNK_STATE_ACTIVE := 3
const CHUNK_STATE_SLEEPING := 4

# If true, request chunk data out to loaded_radius but only mesh chunks inside
# view_radius. The overridden _on_chunk_ready() prevents prefetch chunks from
# creating views until they enter the visible shell.
@export var shell_prefetch_loaded_radius := true

# Cache shell candidate orders while the player remains in the same chunk.
# This avoids re-enumerating the tangent disk every frame.
@export var shell_order_cache_enabled := true
@export var shell_order_cache_max_entries := 128

# Mesh only the radial chunk layers near the player. The wider active shell is
# still available for loading/simulation, but far-above/far-below chunks should
# not all build scene nodes when the player enters a planet.
# Set either value below 0 to use the full planet active shell on that side.
@export var visible_shell_above_layers := 1
@export var visible_shell_below_layers := 1
@export var rebuild_visible_neighbors_on_chunk_ready := false

# Prefer the native GDExtension helper for shell candidate enumeration. The
# GDScript implementation below remains as a runtime fallback so an older DLL can
# still load the scene while the native extension is being rebuilt.
@export var use_cpp_shell_helper := true

# Prefer the native single-chunk persistence helper for save-before-unload and
# restore-before-regenerate. The normal async generation path remains as fallback.
@export var use_chunk_persistence_helper := true
@export var persist_chunks_before_memory_unload := true

# Keep a radial surface-column index for deep chunks touched by gameplay. Unlike
# the short-lived forced keepalive set, this index records which surface column a
# deep chunk belongs to, so returning to the same area can re-activate it.
@export var use_surface_column_index := true
@export var surface_column_keepalive_radius := 1

# Manage C++ ChunkData state for shell streaming. This does not remove chunk data;
# it only marks chunks outside the active shell as SLEEPING so simulation systems
# can skip them without losing generated/saved terrain.
@export var manage_deep_chunk_states := true
@export var chunk_state_keep_radius := 2
@export var max_chunk_state_updates_per_frame := 64

# Remove safe sleeping chunks from GDWorldData memory after single-chunk save.
# This does not delete region-file entries. Region GC is a separate explicit API.
@export var unload_sleeping_chunks_from_memory := true
@export var unload_sleeping_chunk_keep_radius := 8
@export var max_chunk_memory_unloads_per_frame := 8
@export var protect_indexed_chunks_from_memory_unload := false

# Prune stale tracking metadata outside the current shell. This does not delete
# saved chunk data; it only allows far-away chunks to be re-requested normally if
# the player later returns to them.
@export var prune_tracked_chunks_enabled := true
@export var tracked_chunk_extra_radius := 2

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

# Lazily-created GDPlanetShellHelper instance. Stored as Object so the script can
# still parse when the native class is not present in an older extension binary.
var _planet_shell_helper: Object = null

# Lazily-created GDChunkPersistenceHelper instance. Stored as Object so the script
# can still parse when the native class is not present in an older extension binary.
var _chunk_persistence_helper: Object = null

# Runtime surface-column index for deep chunks. It is intentionally not cleared
# on dimension switch because it stores data per dimension.
var _surface_column_index: SurfaceColumnIndex = SurfaceColumnIndex.new()

# Chunks explicitly touched by gameplay. These bypass surface-shell clipping while
# the player stays close, then age out through the max-entry cap or dimension switch.
var _forced_shell_chunks: Dictionary = {}

# Chunks removed from GDWorldData memory after being written to the single-chunk
# persistence path. They can be restored from disk before falling back to async
# generation when they re-enter load_order.
var _unloaded_shell_chunks: Dictionary = {}

# Per-frame shell/horizontal LOD counters exposed through get_streaming_metrics().
var _last_shell_visible_candidates := 0
var _last_shell_load_candidates := 0
var _last_shell_order_cache_hits := 0
var _last_shell_order_cache_misses := 0
var _last_tracked_chunks_pruned := 0
var _last_chunk_state_active_updates := 0
var _last_chunk_state_sleeping_updates := 0
var _last_memory_unloaded_chunks := 0
var _last_memory_saved_chunks := 0
var _last_memory_save_failures := 0
var _last_memory_restore_completions := 0
var _last_memory_load_hits := 0
var _last_memory_load_misses := 0
var _last_forced_keepalive_chunks := 0
var _last_indexed_keepalive_chunks := 0
var _last_deep_player_chunks := 0
var _last_horizontal_lod0_chunks := 0
var _last_horizontal_lod1_chunks := 0
var _last_horizontal_hidden_chunks := 0
var _missing_planet_context_warned := false


func set_active_dimension(dimension_id: StringName) -> void:
	_shell_wanted_visible.clear()
	_shell_order_cache.clear()
	_forced_shell_chunks.clear()
	_unloaded_shell_chunks.clear()
	_missing_planet_context_warned = false
	super.set_active_dimension(dimension_id)


func get_streaming_metrics() -> Dictionary:
	var metrics := super.get_streaming_metrics()
	metrics["shell_streaming_enabled"] = true
	metrics["shell_prefetch_loaded_radius"] = shell_prefetch_loaded_radius
	metrics["shell_order_cache_enabled"] = shell_order_cache_enabled
	metrics["shell_order_cache_entries"] = _shell_order_cache.size()
	metrics["shell_order_cache_hits"] = _last_shell_order_cache_hits
	metrics["shell_order_cache_misses"] = _last_shell_order_cache_misses
	metrics["visible_shell_above_layers"] = visible_shell_above_layers
	metrics["visible_shell_below_layers"] = visible_shell_below_layers
	metrics["rebuild_visible_neighbors_on_chunk_ready"] = rebuild_visible_neighbors_on_chunk_ready
	metrics["shell_visible_candidates"] = _last_shell_visible_candidates
	metrics["shell_load_candidates"] = _last_shell_load_candidates
	metrics["cpp_shell_helper_enabled"] = use_cpp_shell_helper
	metrics["cpp_shell_helper_available"] = _get_planet_shell_helper() != null
	metrics["chunk_persistence_helper_enabled"] = use_chunk_persistence_helper
	metrics["chunk_persistence_helper_available"] = _get_chunk_persistence_helper() != null
	metrics["surface_column_index_enabled"] = use_surface_column_index
	metrics["surface_column_count"] = _surface_column_index.get_column_count(active_dimension) if _surface_column_index else 0
	metrics["surface_column_indexed_chunks"] = _surface_column_index.get_indexed_chunk_count(active_dimension) if _surface_column_index else 0
	metrics["surface_column_keepalive_chunks"] = _last_indexed_keepalive_chunks
	metrics["manage_deep_chunk_states"] = manage_deep_chunk_states
	metrics["chunk_state_active_updates"] = _last_chunk_state_active_updates
	metrics["chunk_state_sleeping_updates"] = _last_chunk_state_sleeping_updates
	metrics["unload_sleeping_chunks_from_memory"] = unload_sleeping_chunks_from_memory
	metrics["memory_unloaded_chunks"] = _last_memory_unloaded_chunks
	metrics["memory_saved_chunks"] = _last_memory_saved_chunks
	metrics["memory_save_failures"] = _last_memory_save_failures
	metrics["memory_restore_completions"] = _last_memory_restore_completions
	metrics["memory_load_hits"] = _last_memory_load_hits
	metrics["memory_load_misses"] = _last_memory_load_misses
	metrics["tracked_memory_unloaded_chunks"] = _unloaded_shell_chunks.size()
	metrics["prune_tracked_chunks_enabled"] = prune_tracked_chunks_enabled
	metrics["tracked_chunks_pruned"] = _last_tracked_chunks_pruned
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


func get_surface_column_index() -> SurfaceColumnIndex:
	return _surface_column_index


func export_surface_column_index() -> Dictionary:
	if _surface_column_index == null:
		return {}
	return _surface_column_index.to_dict()


func import_surface_column_index(data: Dictionary) -> void:
	if _surface_column_index == null:
		_surface_column_index = SurfaceColumnIndex.new()
	_surface_column_index.from_dict(data)


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0
	_last_shell_order_cache_hits = 0
	_last_shell_order_cache_misses = 0
	_last_tracked_chunks_pruned = 0
	_last_chunk_state_active_updates = 0
	_last_chunk_state_sleeping_updates = 0
	_last_memory_unloaded_chunks = 0
	_last_memory_saved_chunks = 0
	_last_memory_save_failures = 0
	_last_memory_restore_completions = 0
	_last_memory_load_hits = 0
	_last_memory_load_misses = 0
	_last_forced_keepalive_chunks = 0
	_last_indexed_keepalive_chunks = 0
	_last_deep_player_chunks = 0

	# Space stations already use build-aware loading in the parent class.
	if _is_station_dimension and _active_station != null:
		_shell_wanted_visible.clear()
		_last_shell_visible_candidates = 0
		_last_shell_load_candidates = 0
		_refresh_station_chunks()
		return

	var player := get_node_or_null(player_node_path) as Node3D
	var planet := _get_active_streaming_planet()
	if player == null or planet == null:
		_shell_wanted_visible.clear()
		_last_shell_visible_candidates = 0
		_last_shell_load_candidates = 0
		_warn_missing_planet_context()
		return

	var visible_order := _get_shell_chunk_order_cached(
			player_chunk, player.global_position, planet, view_radius)
	visible_order = _filter_visible_shell_order(
			visible_order, player.global_position, planet)
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

	# Request data for the larger loading shell. The overridden _ensure_chunk_loaded()
	# restores from single-chunk persistence before falling back to async generation.
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

	_update_chunk_sleep_states(wanted_visible, load_order, player_chunk)
	_unload_stale_sleeping_chunks(wanted_visible, load_order, player_chunk)
	_prune_tracked_chunks(wanted_visible, load_order, player_chunk)
	_process_visible_queue()


func _warn_missing_planet_context() -> void:
	if _missing_planet_context_warned:
		return
	_missing_planet_context_warned = true
	push_warning("PlanetShellChunkRendererBridge: missing player or active planet; "
			+ "legacy radius chunk loading has been removed.")


func _ensure_chunk_loaded(chunk: Vector3i) -> void:
	if world_data == null:
		return
	if _tracked_chunks.has(chunk):
		return
	if world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
		_tracked_chunks[chunk] = true
		return
	if _try_restore_chunk_from_persistence(chunk):
		return
	if world_data.is_chunk_async_pending(active_dimension, chunk.x, chunk.y, chunk.z):
		return
	if max_chunk_load_requests_per_frame > 0 \
			and _chunk_request_count_this_frame >= max_chunk_load_requests_per_frame:
		return
	world_data.request_chunk_async(active_dimension, chunk.x, chunk.y, chunk.z)
	_chunk_request_count_this_frame += 1


# Keep prefetched chunks data-only until they enter the visible shell.
func _on_chunk_ready(dimension: String, chunk_x: int, chunk_y: int, chunk_z: int) -> void:
	if StringName(dimension) != active_dimension:
		return
	var chunk := Vector3i(chunk_x, chunk_y, chunk_z)
	_tracked_chunks[chunk] = true
	_set_chunk_state_if_loaded(chunk, CHUNK_STATE_ACTIVE)
	if _unloaded_shell_chunks.has(chunk):
		_unloaded_shell_chunks.erase(chunk)
		_last_memory_restore_completions += 1

	var chunk_is_visible_shell := _shell_wanted_visible.is_empty() \
			or _shell_wanted_visible.has(chunk)
	if chunk_is_visible_shell:
		if not _pending_view_queue.has(chunk):
			_enqueue_chunk_view(chunk)

	# Existing neighbors may have been meshed while this visible chunk was absent.
	# Data-only prefetch chunks should not force visible mesh rebuilds; they will
	# be considered when they enter the visible shell or when an adjacent visible
	# chunk is rebuilt for gameplay changes.
	if not chunk_is_visible_shell or not rebuild_visible_neighbors_on_chunk_ready:
		return
	for offset in FACE_NEIGHBOR_OFFSETS:
		var neighbor := chunk + offset
		if not _visible_chunks.has(neighbor):
			continue
		_enqueue_chunk_rebuild(neighbor)


func on_terrain_cell_synced(dimension: StringName, chunk: Vector3i, local: Vector3i,
		old_material: int, new_material: int) -> void:
	if dimension != active_dimension:
		super.on_terrain_cell_synced(dimension, chunk, local, old_material, new_material)
		return

	var affected_cells := _terrain_sync_affected_cells(chunk, local)
	if deep_chunk_keepalive_enabled:
		for affected in affected_cells:
			_mark_forced_shell_chunk(
					affected.get("chunk", chunk), &"terrain_cell_synced")

	for affected in affected_cells:
		var affected_chunk: Vector3i = affected.get("chunk", chunk)
		var affected_local: Vector3i = affected.get("local", local)
		_ensure_chunk_loaded(affected_chunk)
		refresh_cell(dimension, affected_chunk, affected_local)
		if not _visible_chunks.has(affected_chunk) \
				and not _pending_view_queue.has(affected_chunk):
			_enqueue_chunk_view(affected_chunk)


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


func _filter_visible_shell_order(
		order: Array[Vector3i],
		player_pos: Vector3,
		planet: PlanetDescriptor) -> Array[Vector3i]:
	if visible_shell_above_layers < 0 and visible_shell_below_layers < 0:
		return order

	var player_altitude := planet.local_surface_altitude_at(player_pos)
	var above := (
			planet.active_shell_above
			if visible_shell_above_layers < 0
			else float(maxi(0, visible_shell_above_layers) * CHUNK_SIZE))
	var below := (
			planet.active_shell_below
			if visible_shell_below_layers < 0
			else float(maxi(0, visible_shell_below_layers) * CHUNK_SIZE))
	var surface_min_altitude := -below
	var surface_max_altitude := above

	# Always keep a surface band visible. When flying above the surface, anchoring
	# the visible band to camera altitude makes terrain below disappear and look
	# like x-ray. Deep digging still gets an extra player-centered band below.
	var include_player_band := player_altitude < surface_min_altitude \
			or player_altitude > surface_max_altitude

	var result: Array[Vector3i] = []
	for chunk in order:
		if _chunk_altitude_intersects_band(
				chunk, planet, surface_min_altitude, surface_max_altitude):
			result.append(chunk)
			continue
		if include_player_band and _chunk_altitude_intersects_band(
				chunk, planet, player_altitude - below, player_altitude + above):
			result.append(chunk)
	return result


func _chunk_altitude_intersects_band(
		chunk: Vector3i,
		planet: PlanetDescriptor,
		min_altitude: float,
		max_altitude: float) -> bool:
	var altitude := planet.local_surface_altitude_at(_chunk_center(chunk))
	var half_diag := sqrt(3.0) * float(CHUNK_SIZE) * 0.5
	return altitude >= min_altitude - half_diag \
			and altitude <= max_altitude + half_diag


func _terrain_sync_affected_chunks(chunk: Vector3i, local: Vector3i) -> Array[Vector3i]:
	var result: Array[Vector3i] = [chunk]
	var seen: Dictionary = {chunk: true}
	for offset in _terrain_boundary_offsets(local):
		var neighbor := chunk + offset
		if seen.has(neighbor):
			continue
		seen[neighbor] = true
		result.append(neighbor)
	return result


func _terrain_sync_affected_cells(chunk: Vector3i, local: Vector3i) -> Array[Dictionary]:
	var result: Array[Dictionary] = [{
		"chunk": chunk,
		"local": local,
	}]
	for offset in _terrain_boundary_offsets(local):
		var neighbor_chunk := chunk + offset
		var neighbor_local := local
		if offset.x < 0:
			neighbor_local.x = CHUNK_SIZE - 1
		elif offset.x > 0:
			neighbor_local.x = 0
		if offset.y < 0:
			neighbor_local.y = CHUNK_SIZE - 1
		elif offset.y > 0:
			neighbor_local.y = 0
		if offset.z < 0:
			neighbor_local.z = CHUNK_SIZE - 1
		elif offset.z > 0:
			neighbor_local.z = 0
		result.append({
			"chunk": neighbor_chunk,
			"local": neighbor_local,
		})
	return result


func _terrain_boundary_offsets(local: Vector3i) -> Array[Vector3i]:
	var offsets: Array[Vector3i] = []
	if local.x <= 0:
		offsets.append(Vector3i(-1, 0, 0))
	elif local.x >= CHUNK_SIZE - 1:
		offsets.append(Vector3i(1, 0, 0))
	if local.y <= 0:
		offsets.append(Vector3i(0, -1, 0))
	elif local.y >= CHUNK_SIZE - 1:
		offsets.append(Vector3i(0, 1, 0))
	if local.z <= 0:
		offsets.append(Vector3i(0, 0, -1))
	elif local.z >= CHUNK_SIZE - 1:
		offsets.append(Vector3i(0, 0, 1))
	return offsets


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

	if use_surface_column_index and _surface_column_index != null:
		var indexed_chunks := _surface_column_index.get_nearby_indexed_chunks(
				active_dimension,
				player_pos,
				planet.local_center,
				planet.planet_radius,
				CHUNK_SIZE,
				surface_column_keepalive_radius)
		for chunk in indexed_chunks:
			if seen.has(chunk):
				continue
			seen[chunk] = true
			result.append(chunk)
			_last_indexed_keepalive_chunks += 1

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
	_record_surface_column_chunk(chunk, reason)
	_set_chunk_state_if_loaded(chunk, CHUNK_STATE_ACTIVE)
	if deep_chunk_max_forced_entries <= 0:
		return
	if _forced_shell_chunks.size() <= deep_chunk_max_forced_entries:
		return
	_prune_forced_shell_chunks(deep_chunk_max_forced_entries)


func _record_surface_column_chunk(chunk: Vector3i, reason: StringName) -> void:
	if not use_surface_column_index or _surface_column_index == null:
		return
	var planet := _get_active_streaming_planet()
	if planet == null:
		return
	_surface_column_index.record_chunk(
			active_dimension,
			chunk,
			planet.local_center,
			planet.planet_radius,
			CHUNK_SIZE,
			reason)


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


func _update_chunk_sleep_states(wanted_visible: Dictionary, load_order: Array,
		player_chunk: Vector3i) -> void:
	if not manage_deep_chunk_states or world_data == null:
		return
	var keep := _build_chunk_keep_set(wanted_visible, load_order)

	var state_updates := 0
	for chunk in keep.keys():
		if not _has_chunk_state_update_budget(state_updates):
			return
		if _set_chunk_state_if_loaded(chunk, CHUNK_STATE_ACTIVE):
			state_updates += 1
			_last_chunk_state_active_updates += 1

	for chunk: Vector3i in _tracked_chunks.keys():
		if not _has_chunk_state_update_budget(state_updates):
			return
		if keep.has(chunk):
			continue
		if _is_chunk_near_player_chunk(chunk, player_chunk, chunk_state_keep_radius):
			continue
		if _set_chunk_state_if_loaded(chunk, CHUNK_STATE_SLEEPING):
			state_updates += 1
			_last_chunk_state_sleeping_updates += 1


func _build_chunk_keep_set(wanted_visible: Dictionary, load_order: Array) -> Dictionary:
	var keep: Dictionary = {}
	for chunk: Vector3i in wanted_visible.keys():
		keep[chunk] = true
	for chunk in load_order:
		keep[chunk] = true
	for chunk: Vector3i in _visible_chunks.keys():
		keep[chunk] = true
	for chunk in _pending_view_queue:
		keep[chunk] = true
	for chunk in _pending_rebuild_queue:
		keep[chunk] = true
	return keep


func _has_chunk_state_update_budget(current_updates: int) -> bool:
	return max_chunk_state_updates_per_frame <= 0 \
			or current_updates < max_chunk_state_updates_per_frame


func _set_chunk_state_if_loaded(chunk: Vector3i, state: int) -> bool:
	if world_data == null:
		return false
	var dim := String(active_dimension)
	if not world_data.has_chunk(dim, chunk.x, chunk.y, chunk.z):
		return false
	if int(world_data.get_chunk_state(dim, chunk.x, chunk.y, chunk.z)) == state:
		return false
	world_data.set_chunk_state(dim, chunk.x, chunk.y, chunk.z, state)
	return true


func _unload_stale_sleeping_chunks(wanted_visible: Dictionary, load_order: Array,
		player_chunk: Vector3i) -> void:
	if not unload_sleeping_chunks_from_memory or world_data == null:
		return
	var keep := _build_chunk_keep_set(wanted_visible, load_order)
	var unloaded := 0
	for chunk: Vector3i in _tracked_chunks.keys():
		if max_chunk_memory_unloads_per_frame > 0 \
				and unloaded >= max_chunk_memory_unloads_per_frame:
			return
		if keep.has(chunk):
			continue
		if _is_chunk_near_player_chunk(chunk, player_chunk, unload_sleeping_chunk_keep_radius):
			continue
		if not _is_loaded_chunk_sleeping(chunk):
			continue
		if not _is_chunk_safe_for_memory_unload(chunk):
			continue
		if not _persist_chunk_before_memory_unload(chunk):
			continue
		_unload_chunk_from_memory(chunk, &"persisted_sleeping")
		unloaded += 1
		_last_memory_unloaded_chunks += 1


func _is_loaded_chunk_sleeping(chunk: Vector3i) -> bool:
	if world_data == null:
		return false
	var dim := String(active_dimension)
	if not world_data.has_chunk(dim, chunk.x, chunk.y, chunk.z):
		return false
	return int(world_data.get_chunk_state(dim, chunk.x, chunk.y, chunk.z)) == CHUNK_STATE_SLEEPING


func _is_chunk_safe_for_memory_unload(chunk: Vector3i) -> bool:
	if world_data == null:
		return false
	if _forced_shell_chunks.has(chunk):
		return false
	if protect_indexed_chunks_from_memory_unload \
			and _surface_column_index != null \
			and _surface_column_index.has_chunk(active_dimension, chunk):
		return false
	var dim := String(active_dimension)
	if not world_data.has_chunk(dim, chunk.x, chunk.y, chunk.z):
		return false
	if not world_data.get_chunk_entities(dim, chunk.x, chunk.y, chunk.z).is_empty():
		return false
	if not world_data.get_chunk_machines(dim, chunk.x, chunk.y, chunk.z).is_empty():
		return false
	if not world_data.get_chunk_connector_ids(dim, chunk.x, chunk.y, chunk.z).is_empty():
		return false
	if not world_data.get_chunk_connectors(dim, chunk.x, chunk.y, chunk.z).is_empty():
		return false
	if not world_data.get_chunk_mechanisms(dim, chunk.x, chunk.y, chunk.z).is_empty():
		return false
	return true


func _persist_chunk_before_memory_unload(chunk: Vector3i) -> bool:
	if not persist_chunks_before_memory_unload:
		return true
	var helper := _get_chunk_persistence_helper()
	var save_dir := _get_universe_save_dir()
	if helper == null or save_dir == "":
		_last_memory_save_failures += 1
		return false
	var ok := bool(helper.call(
			"save_chunk",
			save_dir,
			world_data,
			String(active_dimension),
			chunk.x,
			chunk.y,
			chunk.z))
	if ok:
		_last_memory_saved_chunks += 1
	else:
		_last_memory_save_failures += 1
	return ok


func _try_restore_chunk_from_persistence(chunk: Vector3i) -> bool:
	var helper := _get_chunk_persistence_helper()
	var save_dir := _get_universe_save_dir()
	if helper == null or save_dir == "":
		return false
	var ok := bool(helper.call(
			"load_chunk",
			save_dir,
			world_data,
			String(active_dimension),
			chunk.x,
			chunk.y,
			chunk.z,
			false))
	if not ok:
		_last_memory_load_misses += 1
		return false
	_last_memory_load_hits += 1
	_on_chunk_ready(String(active_dimension), chunk.x, chunk.y, chunk.z)
	return true


func _unload_chunk_from_memory(chunk: Vector3i, reason: StringName) -> void:
	if world_data == null:
		return
	if _visible_chunks.has(chunk):
		_remove_chunk_view(chunk)
	_pending_view_queue.erase(chunk)
	_pending_rebuild_queue.erase(chunk)
	_tracked_chunks.erase(chunk)
	world_data.remove_chunk(String(active_dimension), chunk.x, chunk.y, chunk.z)
	_unloaded_shell_chunks[chunk] = {
		"reason": reason,
		"time_msec": Time.get_ticks_msec(),
	}


func _prune_tracked_chunks(wanted_visible: Dictionary, load_order: Array,
		player_chunk: Vector3i) -> void:
	_last_tracked_chunks_pruned = 0
	if not prune_tracked_chunks_enabled:
		return
	var keep := _build_chunk_keep_set(wanted_visible, load_order)

	for chunk: Vector3i in _tracked_chunks.keys():
		if keep.has(chunk):
			continue
		if _is_chunk_near_player_chunk(chunk, player_chunk, tracked_chunk_extra_radius):
			continue
		_tracked_chunks.erase(chunk)
		_last_tracked_chunks_pruned += 1


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
	var native_order := _compute_shell_chunk_order_native(player_pos, planet, radius_chunks)
	if not native_order.is_empty():
		return native_order
	return _compute_shell_chunk_order_gdscript(player_pos, planet, radius_chunks)


func _compute_shell_chunk_order_native(
		player_pos: Vector3,
		planet: PlanetDescriptor,
		radius_chunks: int) -> Array[Vector3i]:
	var result: Array[Vector3i] = []
	var helper := _get_planet_shell_helper()
	if helper == null:
		return result
	var order_variant: Variant = helper.call(
		"compute_shell_chunk_order",
		player_pos,
		planet.local_center,
		planet.planet_radius,
		planet.active_shell_above,
		planet.active_shell_below,
		CHUNK_SIZE,
		radius_chunks)
	if not (order_variant is Array):
		return result
	var order: Array = order_variant
	for item in order:
		result.append(item)
	return result


func _get_planet_shell_helper() -> Object:
	if not use_cpp_shell_helper:
		return null
	if _planet_shell_helper != null:
		return _planet_shell_helper
	if not ClassDB.class_exists("GDPlanetShellHelper"):
		return null
	_planet_shell_helper = ClassDB.instantiate("GDPlanetShellHelper")
	return _planet_shell_helper


func _get_chunk_persistence_helper() -> Object:
	if not use_chunk_persistence_helper:
		return null
	if _chunk_persistence_helper != null:
		return _chunk_persistence_helper
	if not ClassDB.class_exists("GDChunkPersistenceHelper"):
		return null
	_chunk_persistence_helper = ClassDB.instantiate("GDChunkPersistenceHelper")
	return _chunk_persistence_helper


func _get_universe_save_dir() -> String:
	if _universe_manager == null:
		_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	if _universe_manager == null:
		return ""
	return String(_universe_manager.get("_save_dir"))


func _compute_shell_chunk_order_gdscript(
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
