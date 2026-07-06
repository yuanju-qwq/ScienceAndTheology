# ChunkRendererBridge — 3D chunk rendering bridge for the voxel world.
# Delegates pure-computation helpers to GDChunkHelper (C++) and keeps
# Godot scene-tree operations (ArrayMesh, collision) in GDScript.
# Planet LOD is managed by PlanetLodManager (sibling node).
class_name ChunkRendererBridge
extends Node3D

signal chunk_bridge_ready

const BlockAtlasBuilderScript := preload("res://scripts/world/BlockAtlasBuilder.gd")
const CHUNK_SIZE := 32
const BLOCK_SIZE := 1.0

# Air material ID 0 is a protocol convention: zero-initialized terrain cells
# are air by definition. This is safe to hardcode.
const AIR_MATERIAL := 0
const MATERIAL_FLAG_WALKABLE := 1
const MATERIAL_FLAG_SOLID := 2
const MATERIAL_ID_CAPACITY := 256
const FACE_NEIGHBOR_OFFSETS: Array[Vector3i] = [
	Vector3i(0, 1, 0),
	Vector3i(0, -1, 0),
	Vector3i(1, 0, 0),
	Vector3i(-1, 0, 0),
	Vector3i(0, 0, 1),
	Vector3i(0, 0, -1),
]

const OVERWORLD: StringName = &"overworld"

# Runtime material IDs resolved from worldgen_config at initialization.
# Do NOT hardcode these — they depend on material registration order.
var ladder_material_id: int = AIR_MATERIAL
var workbench_material_id: int = AIR_MATERIAL

@export var world_data: GDWorldData = null
@export var worldgen_config: Resource = null
@export var world_seed: int = 0:
	set(value):
		world_seed = value
		if world_data:
			world_data.seed = value

@export var loaded_radius := 5
@export var view_radius := 4
@export var max_async_results_per_frame := 4:
	set(value):
		max_async_results_per_frame = max(0, value)
		if world_data:
			world_data.set_max_async_results_per_frame(max_async_results_per_frame)
@export var max_chunk_load_requests_per_frame := 10
@export var max_chunk_views_per_frame := 1
@export var mesh_section_size := 8
@export var max_section_rebuilds_per_frame := 8

# --- Unified per-frame budgets (driven by FrameBudgetController) -------------
# These fields previously lived on the shell / async subclasses. They are
# promoted to the base class so FrameBudgetController can set them through a
# single typed reference without unsafe property access. Subclasses retain
# their original default values; non-shell bridges simply ignore them.

# Max persistence (single-chunk save) restores processed per frame.
# Owned by PlanetShellAsyncChunkRendererBridge at runtime.
@export var max_persistence_restores_per_frame := 4

# Max C++ ChunkData state transitions (ACTIVE<->SLEEPING) per frame.
# Owned by PlanetShellChunkRendererBridge at runtime.
@export var max_chunk_state_updates_per_frame := 64

# Max sleeping chunks evicted from GDWorldData memory per frame.
# Owned by PlanetShellChunkRendererBridge at runtime.
@export var max_chunk_memory_unloads_per_frame := 8

# Radius (chunk units) on the local tangent plane that keeps full collision.
# Chunks outside switch to simplified mesh. Owned by PlanetShellChunkRendererBridge.
@export var horizontal_lod0_radius := 4
@export var player_node_path: NodePath = ^"../Player"
@export var auto_update := true
@export var auto_generate_start_chunks := true
@export var start_chunk_radius := 1
@export var debug_chunk_streaming := false
@export var debug_chunk_streaming_interval := 2.0
@export var planet_lod_manager_path: NodePath = ^"../PlanetLodManager"
@export var universe_manager_path: NodePath = ^"../UniverseManager"
@export var command_server_path: NodePath = ^"../GameCommandServer"

var is_initialized := false

# Each entry: { "root": Node3D, "full": Node3D, "simplified": Node3D }
# "root" is the parent node added to the scene tree.
# "full" is the LOD 0 child (all voxels + collision).
# "simplified" is the LOD 1 child (surface voxels only, no collision).
var _visible_chunks: Dictionary = {}
var _pending_view_queue: Array[Vector3i] = []
var _pending_rebuild_queue: Array[Vector3i] = []
var _pending_section_rebuild_queue: Array = []
var _pending_section_rebuild_keys: Dictionary = {}
var _tracked_chunks: Dictionary = {}
var _chunk_request_count_this_frame := 0
var _debug_elapsed := 0.0
var _mesh_build_count := 0
var _mesh_build_total_usec := 0
var _mesh_build_max_usec := 0
var _mesh_build_last_usec := 0
var _materials: Dictionary = {}
var _material_cache: Dictionary = {}
var _collidable_material_mask := PackedByteArray()
var _transparent_material_mask := PackedByteArray()
# Shared block texture atlas for the active dimension.
# Built by BlockAtlasBuilder from the worldgen material visuals.
# Keys: "texture" (ImageTexture), "grid" (Vector2i), "tiles" (Dictionary).
var _block_atlas: Dictionary = {}
var _planet_lod_manager: PlanetLodManager = null
var _current_chunk_lod := 0
var _universe_manager: UniverseManager = null

# Active dimension — the planet currently being rendered and streamed.
# Switched by UniverseManager when the player moves between planets.
var active_dimension: StringName = OVERWORLD

# If true, the active dimension is a space station using build-aware loading.
# Only occupied chunks (those with player-placed content) are loaded.
var _is_station_dimension: bool = false

# Reference to the active StationDescriptor (null if on a planet).
var _active_station: StationDescriptor = null


func _ready() -> void:
	# If UniverseManager is present, it will call initialize_for_universe()
	# after setting up the multi-planet config. Skip auto-initialization.
	var um := get_node_or_null(universe_manager_path) as UniverseManager
	if um != null:
		return
	initialize()
	_connect_command_server()


func _print_perf(label: String, started_usec: int) -> void:
	print("[Perf] %s elapsed_ms=%.2f" % [
		label,
		float(Time.get_ticks_usec() - started_usec) / 1000.0,
	])


func _process(delta: float) -> void:
	if not auto_update or not is_initialized:
		return

	if world_data:
		world_data.process_async_results()

	var player := get_node_or_null(player_node_path) as Node3D
	if player == null:
		return

	var player_chunk := world_position_to_chunk(player.global_position)
	_refresh_chunks(player_chunk)
	_update_chunk_visibility_for_lod()
	_maybe_log_chunk_streaming(delta)


# --- Initialization ---

func initialize() -> void:
	if is_initialized:
		return
	var total_started_usec := Time.get_ticks_usec()
	var stage_started_usec := total_started_usec

	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = world_seed if world_seed != 0 else randi()
	if world_data:
		world_data.set_max_async_results_per_frame(max_async_results_per_frame)
	_print_perf("ChunkRendererBridge.initialize.world_data_setup", stage_started_usec)

	if worldgen_config == null:
		push_error("ChunkRendererBridge: worldgen_config must be provided by UniverseManager.")
		return
	stage_started_usec = Time.get_ticks_usec()
	world_data.worldgen_config = worldgen_config

	_resolve_runtime_material_ids()
	_build_collidable_material_mask()
	_print_perf("ChunkRendererBridge.initialize.resolve_materials", stage_started_usec)

	stage_started_usec = Time.get_ticks_usec()
	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)
	_print_perf("ChunkRendererBridge.initialize.connect_signals", stage_started_usec)

	stage_started_usec = Time.get_ticks_usec()
	_build_materials()
	var atlas_tile_count := _get_block_atlas_tile_count()
	_print_perf("ChunkRendererBridge.initialize.build_materials count=%d atlas_tiles=%d" % [
		_materials.size(),
		atlas_tile_count,
	], stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_connect_planet_lod_manager()
	if auto_generate_start_chunks:
		_generate_initial_chunks()
	_print_perf("ChunkRendererBridge.initialize.initial_chunks queued=%d" %
			_pending_view_queue.size(), stage_started_usec)

	is_initialized = true
	chunk_bridge_ready.emit()
	_print_perf("ChunkRendererBridge.initialize total dimension=%s" %
			String(active_dimension), total_started_usec)


# --- Public API ---

func get_world_data() -> GDWorldData:
	return world_data


# Cumulative, read-only counters used by the opt-in U0 baseline monitor.
# Keeping the counters here avoids adding timing logs to the chunk hot path.
func get_streaming_metrics() -> Dictionary:
	var average_mesh_ms := 0.0
	if _mesh_build_count > 0:
		average_mesh_ms = (
				float(_mesh_build_total_usec) / float(_mesh_build_count) / 1000.0)
	return {
		"active_dimension": String(active_dimension),
		"visible_chunks": _visible_chunks.size(),
		"queued_chunk_views": _pending_view_queue.size(),
		"queued_chunk_rebuilds": _pending_rebuild_queue.size(),
		"tracked_chunks": _tracked_chunks.size(),
		"async_generation_pending": (
				world_data.get_async_pending_count() if world_data else 0),
		"chunk_requests_this_frame": _chunk_request_count_this_frame,
		"lod_level": _current_chunk_lod,
		"mesh_build_count": _mesh_build_count,
		"mesh_build_average_ms": average_mesh_ms,
		"mesh_build_last_ms": float(_mesh_build_last_usec) / 1000.0,
		"mesh_build_max_ms": float(_mesh_build_max_usec) / 1000.0,
	}


# Returns true when the chunk's view (mesh + collision) has been created.
# Distinct from world_data.has_chunk() which only reports chunk *data* availability.
func is_chunk_visible(chunk: Vector3i) -> bool:
	return _visible_chunks.has(chunk)


# Initialize with a multi-planet universe config.
# Called by UniverseManager instead of the default single-planet initialize().
func initialize_for_universe(config: Resource, initial_dimension: StringName) -> void:
	var total_started_usec := Time.get_ticks_usec()
	var stage_started_usec := total_started_usec
	if is_initialized:
		_clear_all_chunk_views()
		_print_perf("ChunkRendererBridge.initialize_for_universe.clear_old_views", stage_started_usec)
		stage_started_usec = Time.get_ticks_usec()

	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = world_seed if world_seed != 0 else randi()
	if world_data:
		world_data.set_max_async_results_per_frame(max_async_results_per_frame)
	_print_perf("ChunkRendererBridge.initialize_for_universe.world_data_setup", stage_started_usec)

	stage_started_usec = Time.get_ticks_usec()
	worldgen_config = config
	world_data.worldgen_config = worldgen_config
	active_dimension = initial_dimension

	_resolve_runtime_material_ids()
	_build_collidable_material_mask()
	_print_perf("ChunkRendererBridge.initialize_for_universe.resolve_materials", stage_started_usec)

	stage_started_usec = Time.get_ticks_usec()
	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)
	_print_perf("ChunkRendererBridge.initialize_for_universe.connect_signals", stage_started_usec)

	stage_started_usec = Time.get_ticks_usec()
	_build_materials()
	var atlas_tile_count := _get_block_atlas_tile_count()
	_print_perf("ChunkRendererBridge.initialize_for_universe.build_materials count=%d atlas_tiles=%d" % [
		_materials.size(),
		atlas_tile_count,
	], stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_connect_planet_lod_manager()
	_connect_command_server()
	if auto_generate_start_chunks:
		_generate_initial_chunks()
	_print_perf("ChunkRendererBridge.initialize_for_universe.initial_chunks queued=%d" %
			_pending_view_queue.size(), stage_started_usec)

	is_initialized = true
	chunk_bridge_ready.emit()
	_print_perf("ChunkRendererBridge.initialize_for_universe total dimension=%s" %
			String(active_dimension), total_started_usec)


func _get_block_atlas_tile_count() -> int:
	var tiles: Variant = _block_atlas.get("tiles", {})
	if tiles is Dictionary:
		return (tiles as Dictionary).size()
	return 0


# Switch the active dimension (planet) being rendered.
# Clears current chunk views and starts streaming the new planet.
# If the dimension belongs to a space station, enables build-aware loading.
func set_active_dimension(dimension_id: StringName) -> void:
	if dimension_id == active_dimension:
		return

	_clear_all_chunk_views()

	active_dimension = dimension_id

	# Check if this dimension is a space station.
	_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	if _universe_manager != null:
		var station := _universe_manager.get_station_by_dimension(dimension_id)
		if station != null:
			_is_station_dimension = true
			_active_station = station
		else:
			_is_station_dimension = false
			_active_station = null
	else:
		_is_station_dimension = false
		_active_station = null

	if auto_generate_start_chunks:
		if _is_station_dimension and _active_station != null:
			_generate_station_chunks()
		else:
			_generate_initial_chunks()


# --- Runtime material ID accessors ---

# Returns the runtime material ID for ladder blocks.
func get_ladder_material_id() -> int:
	return ladder_material_id


# Returns the runtime material ID for workbench blocks.
func get_workbench_material_id() -> int:
	return workbench_material_id


# --- Coordinate transforms (delegated to C++ GDChunkHelper) ---

func world_position_to_cell(world_position: Vector3) -> Vector3i:
	return GDChunkHelper.world_position_to_cell(world_position)


func cell_to_world_position(cell: Vector3i) -> Vector3:
	return GDChunkHelper.cell_to_world_position(cell)


func world_position_to_chunk(world_position: Vector3) -> Vector3i:
	return GDChunkHelper.world_position_to_chunk(world_position, CHUNK_SIZE)


func cell_to_chunk(cell: Vector3i) -> Vector3i:
	return GDChunkHelper.cell_to_chunk(cell, CHUNK_SIZE)


func cell_to_local(cell: Vector3i) -> Vector3i:
	return GDChunkHelper.cell_to_local(cell, CHUNK_SIZE)


# Notify the bridge that a block was placed at the given cell in the given dimension.
# If the dimension is a space station, marks the chunk as occupied so it
# will be loaded and rendered on future visits.
func notify_block_placed(dimension: StringName, cell: Vector3i) -> void:
	if not _is_station_dimension or _active_station == null:
		return
	if dimension != active_dimension:
		return
	var chunk := cell_to_chunk(cell)
	if _active_station.mark_chunk_occupied(chunk):
		_ensure_chunk_loaded(chunk)
		_enqueue_chunk_view(chunk)


# Connect to the GameCommandServer's world_object_synced signal
# to detect block placements in station dimensions.
func _connect_command_server() -> void:
	var cmd_server := get_node_or_null(command_server_path) as GameCommandServer
	if cmd_server == null:
		return
	if not cmd_server.world_object_synced.is_connected(_on_world_object_placed):
		cmd_server.world_object_synced.connect(_on_world_object_placed)


# Callback for when a world object is placed via the command server.
# Triggers build-aware chunk tracking for station dimensions.
func _on_world_object_placed(_object_type: StringName, action: StringName,
		dimension: StringName, cell: Vector3i) -> void:
	if action != &"placed":
		return
	notify_block_placed(dimension, cell)


func get_cell_info(cell: Vector3i, dimension: StringName = OVERWORLD) -> Dictionary:
	var chunk := cell_to_chunk(cell)
	var local := cell_to_local(cell)
	var data := world_data.get_terrain_cell(
		dimension, chunk.x, chunk.y, chunk.z, local.x, local.y, local.z) if world_data else {}
	return {
		"dimension": dimension,
		"cell": cell,
		"chunk": chunk,
		"local": local,
		"data": data,
	}


func refresh_cell(dimension: StringName, chunk: Vector3i, local: Vector3i) -> void:
	if dimension != active_dimension:
		return
	if not _visible_chunks.has(chunk):
		return
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	if _entry_uses_sections(entry):
		_enqueue_cell_section_rebuilds(chunk, local)
	else:
		_enqueue_chunk_rebuild(chunk)


func on_terrain_cell_synced(dimension: StringName, chunk: Vector3i, local: Vector3i,
		_old_material: int, _new_material: int) -> void:
	refresh_cell(dimension, chunk, local)


# Resolve runtime material IDs from the frozen worldgen config snapshot.
# Must be called after worldgen_config is assigned to world_data.
@warning_ignore("unsafe_method_access", "unsafe_call_argument")
func _resolve_runtime_material_ids() -> void:
	if worldgen_config == null:
		push_warning("ChunkRendererBridge: cannot resolve runtime material IDs — no worldgen_config.")
		return
	var runtime_ids: Dictionary = worldgen_config.get_runtime_material_ids()
	ladder_material_id = int(runtime_ids.get("ladder", AIR_MATERIAL))
	workbench_material_id = int(runtime_ids.get("workbench", AIR_MATERIAL))


@warning_ignore("unsafe_method_access")
func _build_collidable_material_mask() -> void:
	_collidable_material_mask.resize(MATERIAL_ID_CAPACITY)
	_collidable_material_mask.fill(0)
	if worldgen_config == null:
		return
	for definition: Dictionary in worldgen_config.get_material_defs():
		var material_id := int(definition.get("id", -1))
		var flags := int(definition.get("flags", 0))
		if material_id < 0 or material_id >= MATERIAL_ID_CAPACITY:
			continue
		if (flags & (MATERIAL_FLAG_WALKABLE | MATERIAL_FLAG_SOLID)) != 0:
			_collidable_material_mask[material_id] = 1


# --- Chunk lifecycle ---

func _generate_initial_chunks() -> void:
	for cz in range(-start_chunk_radius, start_chunk_radius + 1):
		for cx in range(-start_chunk_radius, start_chunk_radius + 1):
			var pos := Vector3i(cx, 0, cz)
			_ensure_chunk_loaded(pos)
			_enqueue_chunk_view(pos)


# Generate initial chunks for a space station dimension.
# Only loads chunks that are in the station's occupied set.
func _generate_station_chunks() -> void:
	if _active_station == null:
		return
	for chunk in _active_station.get_occupied_chunks():
		_ensure_chunk_loaded(chunk)
		_enqueue_chunk_view(chunk)


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0

	# Space station: build-aware loading — only load occupied chunks.
	if _is_station_dimension and _active_station != null:
		_refresh_station_chunks()
		return

	# Planet streaming is intentionally implemented by PlanetShellChunkRendererBridge.
	# The old radius/sphere loader was removed because it scales poorly for large
	# planets and hid bugs by silently bypassing shell streaming.
	_process_visible_queue()


# Refresh chunks for a space station dimension.
# Loads all occupied chunks, unloads chunks that are no longer occupied.
func _refresh_station_chunks() -> void:
	if _active_station == null:
		return

	# Ensure all occupied chunks are loaded and enqueued for view.
	for chunk in _active_station.get_occupied_chunks():
		_ensure_chunk_loaded(chunk)
		if not _visible_chunks.has(chunk) and not _pending_view_queue.has(chunk):
			_enqueue_chunk_view(chunk)

	# Remove views for chunks that are no longer occupied.
	for key: Vector3i in _visible_chunks.keys():
		var chunk_key: Vector3i = key
		if not _active_station.is_chunk_occupied(chunk_key):
			_remove_chunk_view(chunk_key)

	_process_visible_queue()


func _ensure_chunk_loaded(chunk: Vector3i) -> void:
	if world_data == null:
		return
	if _tracked_chunks.has(chunk):
		return

	if world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
		_tracked_chunks[chunk] = true
		return

	if world_data.is_chunk_async_pending(active_dimension, chunk.x, chunk.y, chunk.z):
		return

	if max_chunk_load_requests_per_frame > 0 \
			and _chunk_request_count_this_frame >= max_chunk_load_requests_per_frame:
		return

	world_data.request_chunk_async(active_dimension, chunk.x, chunk.y, chunk.z)
	_chunk_request_count_this_frame += 1


func _on_chunk_ready(dimension: String, chunk_x: int, chunk_y: int, chunk_z: int) -> void:
	if StringName(dimension) != active_dimension:
		return
	var chunk := Vector3i(chunk_x, chunk_y, chunk_z)
	_tracked_chunks[chunk] = true
	if not _pending_view_queue.has(chunk):
		_enqueue_chunk_view(chunk)

	# Existing neighbors may have been meshed while this chunk was absent.
	# Rebuild them once so transparent boundary faces are culled correctly.
	for offset in FACE_NEIGHBOR_OFFSETS:
		var neighbor := chunk + offset
		if not _visible_chunks.has(neighbor):
			continue
		_enqueue_chunk_rebuild(neighbor)


func _enqueue_chunk_view(chunk: Vector3i) -> void:
	if _pending_view_queue.has(chunk):
		return
	_pending_view_queue.append(chunk)


func _enqueue_chunk_rebuild(chunk: Vector3i) -> void:
	if _pending_rebuild_queue.has(chunk):
		return
	_pending_rebuild_queue.append(chunk)


func _enqueue_cell_section_rebuilds(chunk: Vector3i, local: Vector3i) -> void:
	var offsets: Array[Vector3i] = [Vector3i.ZERO]
	offsets.append_array(FACE_NEIGHBOR_OFFSETS)
	for offset in offsets:
		var target_chunk := chunk
		var target_local := local + offset
		if target_local.x < 0:
			target_chunk.x -= 1
			target_local.x = CHUNK_SIZE - 1
		elif target_local.x >= CHUNK_SIZE:
			target_chunk.x += 1
			target_local.x = 0
		if target_local.y < 0:
			target_chunk.y -= 1
			target_local.y = CHUNK_SIZE - 1
		elif target_local.y >= CHUNK_SIZE:
			target_chunk.y += 1
			target_local.y = 0
		if target_local.z < 0:
			target_chunk.z -= 1
			target_local.z = CHUNK_SIZE - 1
		elif target_local.z >= CHUNK_SIZE:
			target_chunk.z += 1
			target_local.z = 0
		_enqueue_section_rebuild_for_local(target_chunk, target_local)


func _enqueue_section_rebuild_for_local(chunk: Vector3i, local: Vector3i) -> void:
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	if not _entry_uses_sections(entry):
		if _visible_chunks.has(chunk):
			_enqueue_chunk_rebuild(chunk)
		return
	var section_size := int(entry.get("section_size", 0))
	if section_size <= 0:
		_enqueue_chunk_rebuild(chunk)
		return
	var max_section_index := int(CHUNK_SIZE / section_size) - 1
	var section := Vector3i(
		clampi(int(local.x / section_size), 0, max_section_index),
		clampi(int(local.y / section_size), 0, max_section_index),
		clampi(int(local.z / section_size), 0, max_section_index))
	_enqueue_chunk_section_rebuild(chunk, section)


func _enqueue_chunk_section_rebuild(chunk: Vector3i, section: Vector3i) -> void:
	if _pending_rebuild_queue.has(chunk):
		return
	var key := _chunk_section_key(chunk, section)
	if _pending_section_rebuild_keys.has(key):
		return
	_pending_section_rebuild_queue.append({
		"chunk": chunk,
		"section": section,
	})
	_pending_section_rebuild_keys[key] = true


func _chunk_section_key(chunk: Vector3i, section: Vector3i) -> String:
	return "%d,%d,%d:%d,%d,%d" % [
		chunk.x, chunk.y, chunk.z,
		section.x, section.y, section.z,
	]


func _section_key(section: Vector3i) -> String:
	return "%d,%d,%d" % [section.x, section.y, section.z]


func _process_visible_queue() -> void:
	var built := 0

	var sections_rebuilt := 0
	while max_section_rebuilds_per_frame <= 0 \
			or sections_rebuilt < max_section_rebuilds_per_frame:
		if _pending_section_rebuild_queue.is_empty():
			break
		var request: Dictionary = _pending_section_rebuild_queue.pop_front()
		var section_chunk: Vector3i = request.get("chunk", Vector3i.ZERO)
		var section: Vector3i = request.get("section", Vector3i.ZERO)
		_pending_section_rebuild_keys.erase(
				_chunk_section_key(section_chunk, section))
		if _rebuild_chunk_section(section_chunk, section):
			sections_rebuilt += 1

	# Gameplay edits must win over background streaming. The old mesh remains
	# visible until _rebuild_chunk_view has built the replacement, so mining and
	# placing update promptly without exposing a blank chunk.
	while built < max_chunk_views_per_frame or max_chunk_views_per_frame <= 0:
		if _pending_rebuild_queue.is_empty():
			break
		var rebuild_chunk: Vector3i = _pending_rebuild_queue.pop_front()
		if not _visible_chunks.has(rebuild_chunk) or world_data == null \
				or not world_data.has_chunk(
						active_dimension, rebuild_chunk.x, rebuild_chunk.y,
						rebuild_chunk.z):
			continue
		_rebuild_chunk_view(rebuild_chunk)
		built += 1

	var index := 0
	while index < _pending_view_queue.size():
		if max_chunk_views_per_frame > 0 and built >= max_chunk_views_per_frame:
			break

		var chunk: Vector3i = _pending_view_queue[index]
		if _visible_chunks.has(chunk):
			_pending_view_queue.remove_at(index)
			continue

		if world_data == null or not world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
			index += 1
			continue

		_pending_view_queue.remove_at(index)
		_create_chunk_view(chunk)
		built += 1


func _rebuild_chunk_view(chunk: Vector3i) -> void:
	var old_entry: Dictionary = _visible_chunks.get(chunk, {})
	var old_root := old_entry.get("root") as Node3D
	_visible_chunks.erase(chunk)
	_create_chunk_view(chunk)
	if not _visible_chunks.has(chunk):
		_visible_chunks[chunk] = old_entry
		return
	if old_root != null:
		old_root.visible = false
		old_root.queue_free()


func _entry_uses_sections(entry: Dictionary) -> bool:
	var sections: Dictionary = entry.get("sections", {})
	return int(entry.get("section_size", 0)) > 0 \
			and not sections.is_empty()


func _get_mesh_section_size(size_x: int, size_y: int, size_z: int) -> int:
	var section_size := int(mesh_section_size)
	if section_size <= 0:
		return 0
	if section_size >= mini(mini(size_x, size_y), size_z):
		return 0
	if size_x % section_size != 0 or size_y % section_size != 0 \
			or size_z % section_size != 0:
		return 0
	return section_size


func _create_chunk_section_views(
		full_parent: Node3D,
		simplified_parent: Node3D,
		chunk: Vector3i,
		materials: PackedByteArray,
		size_x: int,
		size_y: int,
		size_z: int,
		chunk_offset: Vector3,
		section_size: int) -> Dictionary:
	var sections: Dictionary = {}
	var sections_x := int(size_x / section_size)
	var sections_y := int(size_y / section_size)
	var sections_z := int(size_z / section_size)
	for sy in range(sections_y):
		for sz in range(sections_z):
			for sx in range(sections_x):
				var section := Vector3i(sx, sy, sz)
				var section_entry := _build_chunk_section_nodes(
						chunk, section, materials, size_x, size_y, size_z,
						chunk_offset, section_size)
				full_parent.add_child(section_entry["full"])
				simplified_parent.add_child(section_entry["simplified"])
				sections[_section_key(section)] = section_entry
	return sections


func _build_chunk_section_nodes(
		chunk: Vector3i,
		section: Vector3i,
		materials: PackedByteArray,
		size_x: int,
		size_y: int,
		size_z: int,
		chunk_offset: Vector3,
		section_size: int) -> Dictionary:
	var full_section := Node3D.new()
	full_section.name = "Section_%d_%d_%d" % [section.x, section.y, section.z]
	var simplified_section := Node3D.new()
	simplified_section.name = full_section.name

	var origin := Vector3i(
			section.x * section_size,
			section.y * section_size,
			section.z * section_size)
	var section_offset := chunk_offset + Vector3(origin.x, origin.y, origin.z)
	var section_materials := _extract_section_materials(
			materials, size_x, size_y, size_z, origin, section_size)
	var neighbor_materials := _get_section_neighbor_materials(
			chunk, section, materials, size_x, size_y, size_z, section_size)
	var greedy_mesh: Dictionary = GDChunkHelper.build_greedy_mesh(
			section_materials, section_size, section_size, section_size,
			AIR_MATERIAL, ladder_material_id,
			_transparent_material_mask, neighbor_materials)

	for material_id_key: Variant in greedy_mesh.keys():
		var mid := int(material_id_key)
		var mesh_data: Dictionary = greedy_mesh[material_id_key]
		var array_mesh := _create_greedy_mesh_resource(mesh_data, section_offset)
		if array_mesh == null:
			continue
		_create_greedy_mesh_instance(full_section, mid, array_mesh)
		_create_greedy_mesh_instance(simplified_section, mid, array_mesh)

	var collision_data: Dictionary = GDChunkHelper.build_collision_faces(
			section_materials, section_size, section_size, section_size,
			_collidable_material_mask)
	if not collision_data.is_empty():
		var col_verts: PackedVector3Array = collision_data.get(
				"vertices", PackedVector3Array())
		var col_indices: PackedInt32Array = collision_data.get(
				"indices", PackedInt32Array())
		if col_verts.size() > 0 and col_indices.size() > 0:
			_create_chunk_collision(
					full_section, col_verts, col_indices, section_offset)

	return {
		"full": full_section,
		"simplified": simplified_section,
		"section": section,
	}


func _rebuild_chunk_section(chunk: Vector3i, section: Vector3i) -> bool:
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	if not _entry_uses_sections(entry):
		return false
	var section_size := int(entry.get("section_size", 0))
	if section_size <= 0:
		return false
	var full_parent := entry.get("full") as Node3D
	var simplified_parent := entry.get("simplified") as Node3D
	if full_parent == null or simplified_parent == null:
		return false
	var terrain := world_data.get_chunk_terrain(
			active_dimension, chunk.x, chunk.y, chunk.z)
	if terrain.is_empty():
		return false
	var materials: PackedByteArray = terrain.get("materials", PackedByteArray())
	if materials.is_empty():
		return false
	var size_x := int(terrain.get("size_x", CHUNK_SIZE))
	var size_y := int(terrain.get("size_y", CHUNK_SIZE))
	var size_z := int(terrain.get("size_z", CHUNK_SIZE))
	var sections_x := int(size_x / section_size)
	var sections_y := int(size_y / section_size)
	var sections_z := int(size_z / section_size)
	if section.x < 0 or section.x >= sections_x \
			or section.y < 0 or section.y >= sections_y \
			or section.z < 0 or section.z >= sections_z:
		return false

	var chunk_offset := Vector3(
			chunk.x * CHUNK_SIZE,
			chunk.y * CHUNK_SIZE,
			chunk.z * CHUNK_SIZE)
	var new_section := _build_chunk_section_nodes(
			chunk, section, materials, size_x, size_y, size_z,
			chunk_offset, section_size)
	full_parent.add_child(new_section["full"])
	simplified_parent.add_child(new_section["simplified"])

	var sections: Dictionary = entry.get("sections", {})
	var key := _section_key(section)
	var old_section: Dictionary = sections.get(key, {})
	var old_full := old_section.get("full") as Node3D
	var old_simplified := old_section.get("simplified") as Node3D
	if old_full != null:
		old_full.visible = false
		old_full.queue_free()
	if old_simplified != null:
		old_simplified.visible = false
		old_simplified.queue_free()
	sections[key] = new_section
	entry["sections"] = sections
	_visible_chunks[chunk] = entry
	return true


func _extract_section_materials(
		materials: PackedByteArray,
		size_x: int,
		size_y: int,
		size_z: int,
		origin: Vector3i,
		section_size: int) -> PackedByteArray:
	var result := PackedByteArray()
	var total := section_size * section_size * section_size
	result.resize(total)
	for y in range(section_size):
		for z in range(section_size):
			for x in range(section_size):
				var source_x := origin.x + x
				var source_y := origin.y + y
				var source_z := origin.z + z
				var dst_idx := (y * section_size + z) * section_size + x
				if source_x < 0 or source_x >= size_x \
						or source_y < 0 or source_y >= size_y \
						or source_z < 0 or source_z >= size_z:
					result[dst_idx] = AIR_MATERIAL
					continue
				var src_idx := (source_y * size_z + source_z) * size_x + source_x
				result[dst_idx] = materials[src_idx] \
						if src_idx >= 0 and src_idx < materials.size() \
						else AIR_MATERIAL
	return result


func _get_section_neighbor_materials(
		chunk: Vector3i,
		section: Vector3i,
		materials: PackedByteArray,
		size_x: int,
		size_y: int,
		size_z: int,
		section_size: int) -> Dictionary:
	var result: Dictionary = {}
	var sections_x := int(size_x / section_size)
	var sections_y := int(size_y / section_size)
	var sections_z := int(size_z / section_size)
	for direction in range(FACE_NEIGHBOR_OFFSETS.size()):
		var source_chunk := chunk
		var source_section: Vector3i = section + FACE_NEIGHBOR_OFFSETS[direction]
		var source_materials := materials
		if source_section.x < 0:
			source_chunk.x -= 1
			source_section.x = sections_x - 1
		elif source_section.x >= sections_x:
			source_chunk.x += 1
			source_section.x = 0
		if source_section.y < 0:
			source_chunk.y -= 1
			source_section.y = sections_y - 1
		elif source_section.y >= sections_y:
			source_chunk.y += 1
			source_section.y = 0
		if source_section.z < 0:
			source_chunk.z -= 1
			source_section.z = sections_z - 1
		elif source_section.z >= sections_z:
			source_chunk.z += 1
			source_section.z = 0
		if source_chunk != chunk:
			source_materials = _get_chunk_materials(source_chunk)
			if source_materials.is_empty():
				continue
		var origin := Vector3i(
				source_section.x * section_size,
				source_section.y * section_size,
				source_section.z * section_size)
		result[direction] = _extract_section_materials(
				source_materials, size_x, size_y, size_z, origin, section_size)
	return result


func _get_chunk_materials(chunk: Vector3i) -> PackedByteArray:
	if world_data == null:
		return PackedByteArray()
	if not world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
		return PackedByteArray()
	var terrain := world_data.get_chunk_terrain(
			active_dimension, chunk.x, chunk.y, chunk.z)
	return terrain.get("materials", PackedByteArray())


# --- Chunk rendering (scene-tree operations, stays in GDScript) ---

@warning_ignore("unsafe_call_argument")
func _create_chunk_view(chunk: Vector3i) -> void:
	var build_started_usec := Time.get_ticks_usec()
	var terrain := world_data.get_chunk_terrain(active_dimension, chunk.x, chunk.y, chunk.z)
	if terrain.is_empty():
		print("[ChunkBridge] _create_chunk_view %s: terrain empty, skipping" % chunk)
		return

	var materials: PackedByteArray = terrain.get("materials", PackedByteArray())
	if materials.is_empty():
		print("[ChunkBridge] _create_chunk_view %s: materials empty, skipping" % chunk)
		return

	# Opt-in diagnostic for the spawn chunk.
	if debug_chunk_streaming and chunk.x == 0 and chunk.y == 0 and chunk.z == 0:
		var non_air := 0
		for i in range(materials.size()):
			if materials[i] != AIR_MATERIAL:
				non_air += 1
		print("[ChunkBridge] chunk(0,0,0) materials_size=%d non_air=%d air_id=%d" % [materials.size(), non_air, AIR_MATERIAL])

	var size_x := int(terrain.get("size_x", CHUNK_SIZE))
	var size_y := int(terrain.get("size_y", CHUNK_SIZE))
	var size_z := int(terrain.get("size_z", CHUNK_SIZE))

	# Root node holds both LOD 0 and LOD 1 children.
	var root := Node3D.new()
	root.name = "Chunk_%d_%d_%d" % [chunk.x, chunk.y, chunk.z]
	add_child(root)

	# Chunk world offset for positioning.
	var chunk_offset := Vector3(
		chunk.x * CHUNK_SIZE,
		chunk.y * CHUNK_SIZE,
		chunk.z * CHUNK_SIZE)

	# LOD 0: greedy meshed chunk (exposed faces only) + collision.
	var full_node := Node3D.new()
	full_node.name = "LOD0_Full"
	root.add_child(full_node)

	# LOD 1 currently uses the same geometry without collision. Both instances
	# share one ArrayMesh so vertices are transformed and uploaded only once.
	var simplified_node := Node3D.new()
	simplified_node.name = "LOD1_Simplified"
	simplified_node.visible = false
	root.add_child(simplified_node)

	var sections: Dictionary = {}
	var section_size := _get_mesh_section_size(size_x, size_y, size_z)
	if section_size > 0:
		sections = _create_chunk_section_views(
				full_node, simplified_node, chunk, materials,
				size_x, size_y, size_z, chunk_offset, section_size)
	else:
		# Build greedy mesh (C++ hot path).
		var greedy_mesh: Dictionary = GDChunkHelper.build_greedy_mesh(
			materials, size_x, size_y, size_z, AIR_MATERIAL, ladder_material_id,
			_transparent_material_mask, _get_neighbor_materials(chunk))

		# Build collision faces (C++ hot path).
		var collision_data: Dictionary = GDChunkHelper.build_collision_faces(
			materials, size_x, size_y, size_z, _collidable_material_mask)

		for material_id_key: Variant in greedy_mesh.keys():
			var mid: int = int(material_id_key)
			var mesh_data: Dictionary = greedy_mesh[material_id_key]
			var array_mesh := _create_greedy_mesh_resource(mesh_data, chunk_offset)
			if array_mesh == null:
				continue
			_create_greedy_mesh_instance(full_node, mid, array_mesh)
			_create_greedy_mesh_instance(simplified_node, mid, array_mesh)

		# Chunk-level collision: one ConcavePolygonShape3D.
		if not collision_data.is_empty():
			var col_verts: PackedVector3Array = collision_data.get("vertices", PackedVector3Array())
			var col_indices: PackedInt32Array = collision_data.get("indices", PackedInt32Array())
			if col_verts.size() > 0 and col_indices.size() > 0:
				_create_chunk_collision(full_node, col_verts, col_indices, chunk_offset)

	_visible_chunks[chunk] = {
		"root": root,
		"full": full_node,
		"simplified": simplified_node,
		"sections": sections,
		"section_size": section_size,
	}
	_mesh_build_last_usec = Time.get_ticks_usec() - build_started_usec
	_mesh_build_count += 1
	_mesh_build_total_usec += _mesh_build_last_usec
	_mesh_build_max_usec = maxi(_mesh_build_max_usec, _mesh_build_last_usec)
	if debug_chunk_streaming:
		print("[ChunkBridge] chunk view created: %s (visible=%d mesh_ms=%.2f)" % [
			chunk, _visible_chunks.size(), float(_mesh_build_last_usec) / 1000.0])


func _get_neighbor_materials(chunk: Vector3i) -> Dictionary:
	var result := {}
	if world_data == null:
		return result
	for direction in range(FACE_NEIGHBOR_OFFSETS.size()):
		var neighbor: Vector3i = chunk + FACE_NEIGHBOR_OFFSETS[direction]
		if not world_data.has_chunk(
				active_dimension, neighbor.x, neighbor.y, neighbor.z):
			continue
		var terrain := world_data.get_chunk_terrain(
				active_dimension, neighbor.x, neighbor.y, neighbor.z)
		var materials: PackedByteArray = terrain.get(
				"materials", PackedByteArray())
		if not materials.is_empty():
			result[direction] = materials
	return result


# Create one shareable ArrayMesh from greedy mesh data for a single material.
func _create_greedy_mesh_resource(mesh_data: Dictionary,
		chunk_offset: Vector3) -> ArrayMesh:
	var verts: PackedVector3Array = mesh_data.get("vertices", PackedVector3Array())
	var norms: PackedVector3Array = mesh_data.get("normals", PackedVector3Array())
	var uvs: PackedVector2Array = mesh_data.get("uvs", PackedVector2Array())
	var uvs2: PackedVector2Array = mesh_data.get("uvs2", PackedVector2Array())
	var indices: PackedInt32Array = mesh_data.get("indices", PackedInt32Array())

	if verts.size() == 0 or indices.size() == 0:
		return null

	# Apply chunk offset to vertices.
	var offset_verts := PackedVector3Array()
	offset_verts.resize(verts.size())
	for i in range(verts.size()):
		offset_verts[i] = verts[i] + chunk_offset

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = offset_verts
	arrays[Mesh.ARRAY_NORMAL] = norms
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_TEX_UV2] = uvs2
	arrays[Mesh.ARRAY_INDEX] = indices

	var array_mesh := ArrayMesh.new()
	array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return array_mesh


func _create_greedy_mesh_instance(root: Node3D, material_id: int,
		array_mesh: ArrayMesh) -> void:
	var instance := MeshInstance3D.new()
	instance.name = "Material_%d" % material_id
	instance.mesh = array_mesh
	instance.material_override = _get_material(material_id)
	root.add_child(instance)


# Create a single chunk-level collision body from exposed face triangles.
@warning_ignore("integer_division")
func _create_chunk_collision(root: Node3D, col_verts: PackedVector3Array,
		col_indices: PackedInt32Array, chunk_offset: Vector3) -> void:
	# Build triangle array for ConcavePolygonShape3D.
	# Each triangle = 3 Vector3 points, extracted from indexed data.
	var triangles: PackedVector3Array = PackedVector3Array()
	var tri_count := col_indices.size() / 3
	triangles.resize(tri_count * 3)
	for i in range(tri_count):
		var i0: int = col_indices[i * 3]
		var i1: int = col_indices[i * 3 + 1]
		var i2: int = col_indices[i * 3 + 2]
		triangles[i * 3] = col_verts[i0] + chunk_offset
		triangles[i * 3 + 1] = col_verts[i1] + chunk_offset
		triangles[i * 3 + 2] = col_verts[i2] + chunk_offset

	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(triangles)

	var collision_shape := CollisionShape3D.new()
	collision_shape.shape = shape

	var static_body := StaticBody3D.new()
	static_body.name = "Collision"
	static_body.add_child(collision_shape)
	root.add_child(static_body)


@warning_ignore("unsafe_cast")
func _remove_chunk_view(chunk: Vector3i) -> void:
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	var node: Node = entry.get("root") as Node
	if node == null:
		return
	_visible_chunks.erase(chunk)
	_pending_rebuild_queue.erase(chunk)
	_drop_pending_section_rebuilds_for_chunk(chunk)
	node.queue_free()


func _drop_pending_section_rebuilds_for_chunk(chunk: Vector3i) -> void:
	var index := _pending_section_rebuild_queue.size() - 1
	while index >= 0:
		var request: Dictionary = _pending_section_rebuild_queue[index]
		var request_chunk: Vector3i = request.get("chunk", Vector3i.ZERO)
		if request_chunk == chunk:
			var section: Vector3i = request.get("section", Vector3i.ZERO)
			_pending_section_rebuild_keys.erase(
					_chunk_section_key(request_chunk, section))
			_pending_section_rebuild_queue.remove_at(index)
		index -= 1


# Clear all chunk views, pending queue, and tracked chunks.
# Used when switching dimensions or re-initializing the universe.
func _clear_all_chunk_views() -> void:
	for chunk: Vector3i in _visible_chunks.keys():
		_remove_chunk_view(chunk)
	_visible_chunks.clear()
	_pending_view_queue.clear()
	_pending_rebuild_queue.clear()
	_pending_section_rebuild_queue.clear()
	_pending_section_rebuild_keys.clear()
	_tracked_chunks.clear()


# --- Material cache ---

@warning_ignore("unsafe_method_access")
func _build_materials() -> void:
	_materials.clear()
	_material_cache.clear()
	_transparent_material_mask.resize(MATERIAL_ID_CAPACITY)
	_transparent_material_mask.fill(0)
	_block_atlas.clear()
	if worldgen_config == null:
		return
	var visuals: Array = worldgen_config.get_material_visuals()
	for visual: Dictionary in visuals:
		var mid: int = visual.get("material_id", -1)
		if mid < 0:
			continue
		_materials[mid] = visual
		if mid < MATERIAL_ID_CAPACITY and bool(visual.get("transparent", false)):
			_transparent_material_mask[mid] = 1
	# Build the shared block texture atlas for this dimension's visuals.
	_block_atlas = BlockAtlasBuilderScript.build_atlas(visuals)


const TERRAIN_BLOCK_SHADER := preload("res://resource/shaders/terrain_block.gdshader")
const TERRAIN_TRANSPARENT_SHADER := preload(
		"res://resource/shaders/terrain_block_transparent.gdshader")


@warning_ignore("unsafe_method_access", "unsafe_call_argument")
func _get_material(material_id: int) -> Material:
	if _material_cache.has(material_id):
		return _material_cache[material_id]

	var visual: Dictionary = _materials.get(material_id, {})
	var has_visual: bool = not visual.is_empty()
	var albedo: Color = visual.get("albedo_color", Color(0.85, 0.20, 0.85)) if has_visual else Color(0.85, 0.20, 0.85)
	var roughness: float = visual.get("roughness", 0.92) if has_visual else 0.92
	var emissive: Color = visual.get("emissive_color", Color(0, 0, 0)) if has_visual else Color(0, 0, 0)
	var transparent: bool = bool(visual.get("transparent", false)) if has_visual else false

	# Overlay layers (first overlay only for now).
	var overlays: Array = visual.get("overlays", []) if has_visual else []
	var overlay_path: String = overlays[0].get("texture_path", "") if overlays.size() > 0 else ""

	# Per-material atlas tile lookup. Falls back to "no texture" when the
	# material has no tile in the atlas (the shader then uses albedo_color).
	var tiles: Dictionary = _block_atlas.get("tiles", {})
	var tile_entry: Dictionary = tiles.get(material_id, {})
	var top_tile: Dictionary = tile_entry.get("top", {})
	var bottom_tile: Dictionary = tile_entry.get("bottom", {})
	var sides_tile: Dictionary = tile_entry.get("sides", {})

	var shader_mat := ShaderMaterial.new()
	shader_mat.shader = TERRAIN_TRANSPARENT_SHADER if transparent else TERRAIN_BLOCK_SHADER
	if transparent:
		# Ice is drawn after water at shared boundaries.
		shader_mat.render_priority = 1 if String(visual.get("material_key", "")) == "snt:ice" else 0
	shader_mat.set_shader_parameter("albedo_color", albedo)
	shader_mat.set_shader_parameter("roughness", roughness)
	shader_mat.set_shader_parameter("emissive_color", emissive)

	# Shared atlas texture + grid (same for every material in this dimension).
	var atlas_tex: Texture2D = _block_atlas.get("texture", null)
	var atlas_grid: Vector2i = _block_atlas.get("grid", Vector2i(1, 1))
	if atlas_tex != null:
		shader_mat.set_shader_parameter("atlas_texture", atlas_tex)
	shader_mat.set_shader_parameter("atlas_grid", Vector2(float(atlas_grid.x), float(atlas_grid.y)))

	# Per-face tile base + variant count + has flag.
	_set_face_tile_uniforms(shader_mat, "top", top_tile)
	_set_face_tile_uniforms(shader_mat, "bottom", bottom_tile)
	_set_face_tile_uniforms(shader_mat, "sides", sides_tile)

	# Overlay layer (standalone repeating texture, not part of the atlas).
	if overlay_path != "":
		var overlay_tex := load(overlay_path) as Texture2D
		if overlay_tex != null:
			shader_mat.set_shader_parameter("overlay_texture", overlay_tex)
			var overlay_blend_val: float = overlays[0].get("blend", 0.5) if overlays.size() > 0 else 0.5
			shader_mat.set_shader_parameter("overlay_blend", overlay_blend_val)
			shader_mat.set_shader_parameter("has_overlay", true)
		else:
			push_warning("ChunkRendererBridge: failed to load overlay texture '%s' for material %d" % [overlay_path, material_id])

	var material: Material = shader_mat
	_material_cache[material_id] = material
	return material


# Helper: set the per-face atlas tile uniforms on a ShaderMaterial.
@warning_ignore("unsafe_method_access")
func _set_face_tile_uniforms(shader_mat: ShaderMaterial, face_key: String, tile: Dictionary) -> void:
	var base: Vector2i = tile.get("base", Vector2i(0, 0))
	var vcount: int = int(tile.get("variant_count", 1))
	var has_tex: bool = bool(tile.get("has", false))
	shader_mat.set_shader_parameter(face_key + "_tile_base", Vector2(float(base.x), float(base.y)))
	shader_mat.set_shader_parameter(face_key + "_variant_count", max(1, vcount))
	shader_mat.set_shader_parameter("has_" + face_key + "_texture", has_tex)


# --- Planet LOD integration ---

func _connect_planet_lod_manager() -> void:
	# Try UniverseManager first (multi-planet mode).
	_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	if _universe_manager != null:
		_refresh_lod_manager_from_universe()
		return

	# Fallback: direct PlanetLodManager reference (single-planet mode).
	_planet_lod_manager = get_node_or_null(planet_lod_manager_path) as PlanetLodManager
	if _planet_lod_manager == null:
		push_warning("ChunkRendererBridge: PlanetLodManager not found at %s" % planet_lod_manager_path)


# Refresh the LOD manager reference from UniverseManager's active planet.
func _refresh_lod_manager_from_universe() -> void:
	if _universe_manager == null or _universe_manager.active_planet == null:
		_planet_lod_manager = null
		return
	_planet_lod_manager = _universe_manager.get_lod_manager(
		_universe_manager.active_planet.dimension_id)


# Switch between LOD 0 (full) and LOD 1 (simplified) chunk views,
# or hide chunks entirely when the proxy sphere is active (LOD 2+).
@warning_ignore("unsafe_method_access")
func _update_chunk_visibility_for_lod() -> void:
	# In multi-planet mode, refresh LOD manager from UniverseManager.
	if _universe_manager != null:
		_refresh_lod_manager_from_universe()

	if _planet_lod_manager == null:
		return

	var lod_level := _planet_lod_manager.get_current_lod_level()

	# Skip per-chunk updates if LOD level hasn't changed.
	if lod_level == _current_chunk_lod:
		return
	_current_chunk_lod = lod_level

	var show_full := lod_level == PlanetLodManager.LOD_REAL_CHUNKS
	var show_simplified := lod_level == PlanetLodManager.LOD_SIMPLIFIED_MESH
	var show_root := show_full or show_simplified

	for chunk: Vector3i in _visible_chunks.keys():
		var entry: Dictionary = _visible_chunks[chunk]
		var root: Node3D = entry.get("root")
		var full: Node3D = entry.get("full")
		var simplified: Node3D = entry.get("simplified")
		if root:
			root.visible = show_root
		if full:
			full.visible = show_full
		if simplified:
			simplified.visible = show_simplified


func _maybe_log_chunk_streaming(delta: float) -> void:
	if not debug_chunk_streaming:
		return
	_debug_elapsed += delta
	if _debug_elapsed < debug_chunk_streaming_interval:
		return
	_debug_elapsed = 0.0
	var metrics := get_streaming_metrics()
	print("ChunkRendererBridge3D: visible=%d queued=%d loaded=%d async=%d "
			+ "lod=%d mesh_avg_ms=%.2f mesh_max_ms=%.2f" % [
		metrics["visible_chunks"],
		metrics["queued_chunk_views"],
		metrics["tracked_chunks"],
		metrics["async_generation_pending"],
		metrics["lod_level"],
		metrics["mesh_build_average_ms"],
		metrics["mesh_build_max_ms"],
	])
