# ChunkRendererBridge — 3D chunk rendering bridge for the voxel world.
# Delegates pure-computation helpers to GDChunkHelper (C++) and keeps
# Godot scene-tree operations (ArrayMesh, collision) in GDScript.
# Planet LOD is managed by PlanetLodManager (sibling node).
class_name ChunkRendererBridge
extends Node3D

signal chunk_bridge_ready

const BuiltinTerrainContentScript := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")
const CHUNK_SIZE := 32
const BLOCK_SIZE := 1.0

# Air material ID 0 is a protocol convention: zero-initialized terrain cells
# are air by definition. This is safe to hardcode.
const AIR_MATERIAL := 0

const OVERWORLD: StringName = &"overworld"

# Runtime material IDs resolved from worldgen_config at initialization.
# Do NOT hardcode these — they depend on material registration order.
var ladder_material_id: int = AIR_MATERIAL
var workbench_material_id: int = AIR_MATERIAL

@export var world_data: GDWorldData = null
@export var worldgen_config: Resource = null
@export var seed: int = 0:
	set(value):
		seed = value
		if world_data:
			world_data.seed = value

@export var loaded_radius := 5
@export var view_radius := 4
@export var use_spherical_loading := true
@export var max_async_results_per_frame := 4:
	set(value):
		max_async_results_per_frame = max(0, value)
		if world_data:
			world_data.set_max_async_results_per_frame(max_async_results_per_frame)
@export var max_chunk_load_requests_per_frame := 10
@export var max_chunk_views_per_frame := 2
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
var _tracked_chunks: Dictionary = {}
var _chunk_request_count_this_frame := 0
var _debug_elapsed := 0.0
var _materials: Dictionary = {}
var _material_cache: Dictionary = {}
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

	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = seed if seed != 0 else randi()
	if world_data:
		world_data.set_max_async_results_per_frame(max_async_results_per_frame)

	if worldgen_config == null:
		worldgen_config = BuiltinTerrainContentScript.create_default_config()
	world_data.worldgen_config = worldgen_config

	_resolve_runtime_material_ids()

	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)

	_build_materials()
	_connect_planet_lod_manager()
	if auto_generate_start_chunks:
		_generate_initial_chunks()

	is_initialized = true
	chunk_bridge_ready.emit()


# --- Public API ---

func get_world_data() -> GDWorldData:
	return world_data


# Initialize with a multi-planet universe config.
# Called by UniverseManager instead of the default single-planet initialize().
func initialize_for_universe(config: Resource, initial_dimension: StringName) -> void:
	if is_initialized:
		_clear_all_chunk_views()

	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = seed if seed != 0 else randi()
	if world_data:
		world_data.set_max_async_results_per_frame(max_async_results_per_frame)

	worldgen_config = config
	world_data.worldgen_config = worldgen_config
	active_dimension = initial_dimension

	_resolve_runtime_material_ids()

	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)

	_build_materials()
	_connect_planet_lod_manager()
	_connect_command_server()
	if auto_generate_start_chunks:
		_generate_initial_chunks()

	is_initialized = true
	chunk_bridge_ready.emit()


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


func refresh_cell(dimension: StringName, chunk: Vector3i, _local: Vector3i) -> void:
	if dimension != active_dimension:
		return
	if not _visible_chunks.has(chunk):
		return
	_remove_chunk_view(chunk)
	_enqueue_chunk_view(chunk)


func on_terrain_cell_synced(dimension: StringName, chunk: Vector3i, local: Vector3i,
		_old_material: int, _new_material: int) -> void:
	refresh_cell(dimension, chunk, local)


# Resolve runtime material IDs from the frozen worldgen config snapshot.
# Must be called after worldgen_config is assigned to world_data.
func _resolve_runtime_material_ids() -> void:
	if worldgen_config == null:
		push_warning("ChunkRendererBridge: cannot resolve runtime material IDs — no worldgen_config.")
		return
	var runtime_ids: Dictionary = worldgen_config.get_runtime_material_ids()
	ladder_material_id = int(runtime_ids.get("ladder", AIR_MATERIAL))
	workbench_material_id = int(runtime_ids.get("workbench", AIR_MATERIAL))


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

	# Planet: radius-based loading (original behavior).
	var result := GDChunkHelper.compute_visible_chunks(
		player_chunk, loaded_radius, view_radius, use_spherical_loading)
	var wanted_visible: Dictionary = result.get("wanted_visible", {})
	var visible_order: Array = result.get("visible_order", [])

	# Ensure all wanted chunks are loaded (C++ only computes positions; loading is async).
	for pos in wanted_visible.keys():
		_ensure_chunk_loaded(pos)

	for key in _visible_chunks.keys():
		if not wanted_visible.has(key):
			_remove_chunk_view(key)

	for pos in visible_order:
		if _visible_chunks.has(pos):
			continue
		if not _pending_view_queue.has(pos):
			_enqueue_chunk_view(pos)

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
	for key in _visible_chunks.keys():
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
	if _pending_view_queue.has(chunk):
		return
	_enqueue_chunk_view(chunk)


func _enqueue_chunk_view(chunk: Vector3i) -> void:
	if _pending_view_queue.has(chunk):
		return
	_pending_view_queue.append(chunk)


func _process_visible_queue() -> void:
	var built := 0
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


# --- Chunk rendering (scene-tree operations, stays in GDScript) ---

func _create_chunk_view(chunk: Vector3i) -> void:
	var terrain := world_data.get_chunk_terrain(active_dimension, chunk.x, chunk.y, chunk.z)
	if terrain.is_empty():
		return

	var materials: PackedByteArray = terrain.get("materials", PackedByteArray())
	if materials.is_empty():
		return

	var size_x := int(terrain.get("size_x", CHUNK_SIZE))
	var size_y := int(terrain.get("size_y", CHUNK_SIZE))
	var size_z := int(terrain.get("size_z", CHUNK_SIZE))

	# Build greedy mesh (C++ hot path).
	var greedy_mesh: Dictionary = GDChunkHelper.build_greedy_mesh(
		materials, size_x, size_y, size_z, AIR_MATERIAL, ladder_material_id)

	# Build collision faces (C++ hot path).
	var collision_data: Dictionary = GDChunkHelper.build_collision_faces(
		materials, size_x, size_y, size_z, AIR_MATERIAL, ladder_material_id)

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

	for material_id_key in greedy_mesh.keys():
		var mid: int = int(material_id_key)
		var mesh_data: Dictionary = greedy_mesh[material_id_key]
		_create_greedy_mesh_instance(full_node, mid, mesh_data, chunk_offset)

	# Chunk-level collision: one ConcavePolygonShape3D.
	if not collision_data.is_empty():
		var col_verts: PackedVector3Array = collision_data.get("vertices", PackedVector3Array())
		var col_indices: PackedInt32Array = collision_data.get("indices", PackedInt32Array())
		if col_verts.size() > 0 and col_indices.size() > 0:
			_create_chunk_collision(full_node, col_verts, col_indices, chunk_offset)

	# LOD 1: simplified (same greedy mesh, no collision, slightly different visibility).
	var simplified_node := Node3D.new()
	simplified_node.name = "LOD1_Simplified"
	simplified_node.visible = false
	root.add_child(simplified_node)

	for material_id_key in greedy_mesh.keys():
		var mid: int = int(material_id_key)
		var mesh_data: Dictionary = greedy_mesh[material_id_key]
		_create_greedy_mesh_instance(simplified_node, mid, mesh_data, chunk_offset)

	_visible_chunks[chunk] = {
		"root": root,
		"full": full_node,
		"simplified": simplified_node,
	}


# Create a MeshInstance3D from greedy mesh data for a single material.
func _create_greedy_mesh_instance(root: Node3D, material_id: int,
		mesh_data: Dictionary, chunk_offset: Vector3) -> void:
	var verts: PackedVector3Array = mesh_data.get("vertices", PackedVector3Array())
	var norms: PackedVector3Array = mesh_data.get("normals", PackedVector3Array())
	var uvs: PackedVector2Array = mesh_data.get("uvs", PackedVector2Array())
	var uvs2: PackedVector2Array = mesh_data.get("uvs2", PackedVector2Array())
	var indices: PackedInt32Array = mesh_data.get("indices", PackedInt32Array())

	if verts.size() == 0 or indices.size() == 0:
		return

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

	var instance := MeshInstance3D.new()
	instance.name = "Material_%d" % material_id
	instance.mesh = array_mesh
	instance.material_override = _get_material(material_id)
	root.add_child(instance)


# Create a single chunk-level collision body from exposed face triangles.
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


func _remove_chunk_view(chunk: Vector3i) -> void:
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	var node: Node = entry.get("root") as Node
	if node == null:
		return
	_visible_chunks.erase(chunk)
	node.queue_free()


# Clear all chunk views, pending queue, and tracked chunks.
# Used when switching dimensions or re-initializing the universe.
func _clear_all_chunk_views() -> void:
	for chunk in _visible_chunks.keys():
		_remove_chunk_view(chunk)
	_visible_chunks.clear()
	_pending_view_queue.clear()
	_tracked_chunks.clear()


# --- Material cache ---

func _build_materials() -> void:
	_materials.clear()
	_material_cache.clear()
	if worldgen_config == null:
		return
	var visuals: Array = worldgen_config.get_material_visuals()
	for visual: Dictionary in visuals:
		var mid: int = visual.get("material_id", -1)
		if mid < 0:
			continue
		_materials[mid] = visual


const TERRAIN_BLOCK_SHADER := preload("res://resource/shaders/terrain_block.gdshader")


func _get_material(material_id: int) -> Material:
	if _material_cache.has(material_id):
		return _material_cache[material_id]

	var visual: Dictionary = _materials.get(material_id, {})
	var albedo: Color = visual.get("albedo_color", Color(0.85, 0.20, 0.85)) if not visual.is_empty() else Color(0.85, 0.20, 0.85)
	var roughness: float = visual.get("roughness", 0.92) if not visual.is_empty() else 0.92
	var emissive: Color = visual.get("emissive_color", Color(0, 0, 0)) if not visual.is_empty() else Color(0, 0, 0)
	var is_transparent: bool = visual.get("transparent", false) if not visual.is_empty() else false
	var is_cull_disabled: bool = visual.get("cull_disabled", false) if not visual.is_empty() else false

	# Check if any per-face texture is defined.
	var top_tex_path: String = visual.get("top", {}).get("texture_path", "") if not visual.is_empty() else ""
	var bottom_tex_path: String = visual.get("bottom", {}).get("texture_path", "") if not visual.is_empty() else ""
	var sides_tex_path: String = visual.get("sides", {}).get("texture_path", "") if not visual.is_empty() else ""
	var has_per_face_tex: bool = top_tex_path != "" or bottom_tex_path != "" or sides_tex_path != ""

	# Overlay layers (first overlay only for now).
	var overlays: Array = visual.get("overlays", []) if not visual.is_empty() else []
	var overlay_path: String = overlays[0].get("texture_path", "") if overlays.size() > 0 else ""

	var material: Material

	if has_per_face_tex:
		# Use ShaderMaterial with per-face texture selection via UV2.
		var shader_mat := ShaderMaterial.new()
		shader_mat.shader = TERRAIN_BLOCK_SHADER
		shader_mat.set_shader_parameter("albedo_color", albedo)
		shader_mat.set_shader_parameter("roughness", roughness)
		shader_mat.set_shader_parameter("emissive_color", emissive)

		# Per-face textures and variant counts.
		var top_variant: int = visual.get("top", {}).get("variant_count", 1) if not visual.is_empty() else 1
		var bottom_variant: int = visual.get("bottom", {}).get("variant_count", 1) if not visual.is_empty() else 1
		var sides_variant: int = visual.get("sides", {}).get("variant_count", 1) if not visual.is_empty() else 1
		shader_mat.set_shader_parameter("top_variant_count", top_variant)
		shader_mat.set_shader_parameter("bottom_variant_count", bottom_variant)
		shader_mat.set_shader_parameter("sides_variant_count", sides_variant)

		if top_tex_path != "":
			var tex := load(top_tex_path) as Texture2D
			if tex != null:
				shader_mat.set_shader_parameter("top_texture", tex)
				shader_mat.set_shader_parameter("has_top_texture", true)
			else:
				push_warning("ChunkRendererBridge: failed to load top texture '%s' for material %d" % [top_tex_path, material_id])

		if bottom_tex_path != "":
			var tex := load(bottom_tex_path) as Texture2D
			if tex != null:
				shader_mat.set_shader_parameter("bottom_texture", tex)
				shader_mat.set_shader_parameter("has_bottom_texture", true)
			else:
				push_warning("ChunkRendererBridge: failed to load bottom texture '%s' for material %d" % [bottom_tex_path, material_id])

		if sides_tex_path != "":
			var tex := load(sides_tex_path) as Texture2D
			if tex != null:
				shader_mat.set_shader_parameter("sides_texture", tex)
				shader_mat.set_shader_parameter("has_sides_texture", true)
			else:
				push_warning("ChunkRendererBridge: failed to load sides texture '%s' for material %d" % [sides_tex_path, material_id])

		# Overlay layer.
		if overlay_path != "":
			var overlay_tex := load(overlay_path) as Texture2D
			if overlay_tex != null:
				shader_mat.set_shader_parameter("overlay_texture", overlay_tex)
				var overlay_blend_val: float = overlays[0].get("blend", 0.5) if overlays.size() > 0 else 0.5
				shader_mat.set_shader_parameter("overlay_blend", overlay_blend_val)
				shader_mat.set_shader_parameter("has_overlay", true)
			else:
				push_warning("ChunkRendererBridge: failed to load overlay texture '%s' for material %d" % [overlay_path, material_id])

		material = shader_mat
	else:
		# Fallback to StandardMaterial3D when no per-face textures.
		var std_mat := StandardMaterial3D.new()
		std_mat.albedo_color = albedo
		std_mat.roughness = roughness
		std_mat.specular_mode = BaseMaterial3D.SPECULAR_DISABLED

		if emissive != Color(0, 0, 0):
			std_mat.emission_enabled = true
			std_mat.emission = emissive

		if is_transparent or albedo.a < 1.0:
			std_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		if is_cull_disabled or albedo.a < 1.0:
			std_mat.cull_mode = BaseMaterial3D.CULL_DISABLED

		# Single texture fallback: pick the best available.
		var chosen_path := sides_tex_path if sides_tex_path != "" else top_tex_path
		if chosen_path == "":
			chosen_path = bottom_tex_path
		if chosen_path != "":
			var tex := load(chosen_path) as Texture2D
			if tex != null:
				std_mat.albedo_texture = tex
			else:
				push_warning("ChunkRendererBridge: failed to load texture '%s' for material %d" % [chosen_path, material_id])

		# Overlay as detail map.
		if overlay_path != "":
			var overlay_tex := load(overlay_path) as Texture2D
			if overlay_tex != null:
				std_mat.detail_enabled = true
				std_mat.detail_albedo = overlay_tex
				std_mat.detail_blend_mode = BaseMaterial3D.BLEND_MODE_MUL

		material = std_mat

	_material_cache[material_id] = material
	return material


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

	for chunk in _visible_chunks.keys():
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
	print("ChunkRendererBridge3D: visible=%d queued=%d loaded=%d async=%d lod=%d" % [
		_visible_chunks.size(),
		_pending_view_queue.size(),
		_tracked_chunks.size(),
		world_data.get_async_pending_count() if world_data else 0,
		_current_chunk_lod,
	])
