# ChunkRendererBridge — 3D chunk rendering bridge for the voxel world.
# Delegates pure-computation helpers to GDChunkHelper (C++) and keeps
# Godot scene-tree operations (MultiMesh, collision, Planet LOD) in GDScript.
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
@export var show_planet_lod := true
@export var planet_lod_radius := 512.0
@export var planet_lod_center := Vector3(0.0, -512.0, 0.0)

var is_initialized := false
var _visible_chunks: Dictionary = {}
var _pending_view_queue: Array[Vector3i] = []
var _tracked_chunks: Dictionary = {}
var _chunk_request_count_this_frame := 0
var _debug_elapsed := 0.0
var _materials: Dictionary = {}
var _material_cache: Dictionary = {}
var _planet_lod_mesh_instance: MeshInstance3D = null


func _ready() -> void:
	initialize()


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
	_update_planet_lod_visibility(player)
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
	_create_planet_lod()
	if auto_generate_start_chunks:
		_generate_initial_chunks()

	is_initialized = true
	chunk_bridge_ready.emit()


# --- Public API ---

func get_world_data() -> GDWorldData:
	return world_data


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
	if dimension != OVERWORLD:
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


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0

	# Delegate visibility computation to C++.
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


func _ensure_chunk_loaded(chunk: Vector3i) -> void:
	if world_data == null:
		return
	if _tracked_chunks.has(chunk):
		return

	if world_data.has_chunk(OVERWORLD, chunk.x, chunk.y, chunk.z):
		_tracked_chunks[chunk] = true
		return

	if world_data.is_chunk_async_pending(OVERWORLD, chunk.x, chunk.y, chunk.z):
		return

	if max_chunk_load_requests_per_frame > 0 \
			and _chunk_request_count_this_frame >= max_chunk_load_requests_per_frame:
		return

	world_data.request_chunk_async(OVERWORLD, chunk.x, chunk.y, chunk.z)
	_chunk_request_count_this_frame += 1


func _on_chunk_ready(dimension: String, chunk_x: int, chunk_y: int, chunk_z: int) -> void:
	if StringName(dimension) != OVERWORLD:
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

		if world_data == null or not world_data.has_chunk(OVERWORLD, chunk.x, chunk.y, chunk.z):
			index += 1
			continue

		_pending_view_queue.remove_at(index)
		_create_chunk_view(chunk)
		built += 1


# --- Chunk rendering (scene-tree operations, stays in GDScript) ---

func _create_chunk_view(chunk: Vector3i) -> void:
	var terrain := world_data.get_chunk_terrain(OVERWORLD, chunk.x, chunk.y, chunk.z)
	if terrain.is_empty():
		return

	var materials: PackedByteArray = terrain.get("materials", PackedByteArray())
	if materials.is_empty():
		return

	var root := Node3D.new()
	root.name = "Chunk_%d_%d_%d" % [chunk.x, chunk.y, chunk.z]
	add_child(root)
	_visible_chunks[chunk] = root

	var size_x := int(terrain.get("size_x", CHUNK_SIZE))
	var size_y := int(terrain.get("size_y", CHUNK_SIZE))
	var size_z := int(terrain.get("size_z", CHUNK_SIZE))
	var by_material: Dictionary = {}
	for local_y in range(size_y):
		for local_z in range(size_z):
			for local_x in range(size_x):
				var index := GDChunkHelper.terrain_index(local_x, local_y, local_z, size_x, size_z)
				if index >= materials.size():
					continue
				var material_id := int(materials[index])
				if material_id == AIR_MATERIAL:
					continue
				if not by_material.has(material_id):
					by_material[material_id] = []
				by_material[material_id].append(Vector3i(local_x, local_y, local_z))

	for material_id in by_material.keys():
		_create_material_multimesh(root, chunk, int(material_id), by_material[material_id], materials, size_x, size_y, size_z)


func _create_material_multimesh(root: Node3D, chunk: Vector3i, material_id: int, cells: Array,
		materials: PackedByteArray, size_x: int, size_y: int, size_z: int) -> void:
	if cells.is_empty():
		return

	var is_ladder := material_id == ladder_material_id
	var mesh := BoxMesh.new()
	if is_ladder:
		mesh.size = Vector3(BLOCK_SIZE * 0.9, BLOCK_SIZE * 0.9, BLOCK_SIZE * 0.15)
	else:
		mesh.size = Vector3(BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)

	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = mesh
	multimesh.instance_count = cells.size()

	for i in range(cells.size()):
		var local: Vector3i = cells[i]
		var cell := Vector3i(
			chunk.x * CHUNK_SIZE + local.x,
			chunk.y * CHUNK_SIZE + local.y,
			chunk.z * CHUNK_SIZE + local.z)
		var pos := cell_to_world_position(cell)
		if is_ladder:
			var facing := GDChunkHelper.ladder_facing(local, materials, size_x, size_y, size_z, AIR_MATERIAL)
			var basis := Basis().rotated(Vector3.UP, facing)
			multimesh.set_instance_transform(i, Transform3D(basis, pos))
		else:
			multimesh.set_instance_transform(i, Transform3D(Basis(), pos))

	var instance := MultiMeshInstance3D.new()
	instance.name = "Material_%d" % material_id
	instance.multimesh = multimesh
	instance.material_override = _get_material(material_id)
	root.add_child(instance)

	if is_ladder:
		return

	var static_body := StaticBody3D.new()
	static_body.name = "Collision_%d" % material_id
	root.add_child(static_body)

	for local in cells:
		if not GDChunkHelper.is_surface_voxel(local, materials, size_x, size_y, size_z, AIR_MATERIAL, ladder_material_id):
			continue
		var cell := Vector3i(
			chunk.x * CHUNK_SIZE + local.x,
			chunk.y * CHUNK_SIZE + local.y,
			chunk.z * CHUNK_SIZE + local.z)
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE)
		shape.shape = box
		shape.position = cell_to_world_position(cell)
		static_body.add_child(shape)


func _remove_chunk_view(chunk: Vector3i) -> void:
	var node := _visible_chunks.get(chunk) as Node
	if node == null:
		return
	_visible_chunks.erase(chunk)
	node.queue_free()


# --- Material cache ---

func _build_materials() -> void:
	_materials = {
		0: Color(0, 0, 0, 0),
		1: Color(0.46, 0.47, 0.45),
		2: Color(0.33, 0.25, 0.14),
		3: Color(0.73, 0.64, 0.40),
		4: Color(0.18, 0.39, 0.74, 0.78),
		5: Color(0.95, 0.28, 0.08),
		6: Color(0.65, 0.58, 0.50),
		7: Color(0.72, 0.37, 0.18),
		8: Color(0.13, 0.13, 0.13),
		9: Color(0.45, 0.27, 0.12),
		10: Color(0.21, 0.42, 0.20),
		11: Color(0.55, 0.30, 0.15),
		12: Color(0.60, 0.40, 0.20),
	}


func _get_material(material_id: int) -> StandardMaterial3D:
	if _material_cache.has(material_id):
		return _material_cache[material_id]

	var material := StandardMaterial3D.new()
	material.albedo_color = _materials.get(material_id, Color(0.85, 0.20, 0.85))
	material.roughness = 0.92
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	if material.albedo_color.a < 1.0:
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
	_material_cache[material_id] = material
	return material


# --- Planet LOD ---

func _create_planet_lod() -> void:
	if not show_planet_lod:
		return

	var sphere := SphereMesh.new()
	sphere.radius = planet_lod_radius
	sphere.height = planet_lod_radius * 2.0
	sphere.radial_segments = 64
	sphere.rings = 32

	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.35, 0.55, 0.30)
	material.roughness = 0.95
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	sphere.surface_set_material(0, material)

	_planet_lod_mesh_instance = MeshInstance3D.new()
	_planet_lod_mesh_instance.name = "PlanetLOD"
	_planet_lod_mesh_instance.mesh = sphere
	_planet_lod_mesh_instance.global_position = planet_lod_center
	add_child(_planet_lod_mesh_instance)


func _update_planet_lod_visibility(player: Node3D) -> void:
	if _planet_lod_mesh_instance == null:
		return

	var dist_to_center := player.global_position.distance_to(planet_lod_center)
	var surface_dist := dist_to_center - planet_lod_radius

	var lod_show_distance := float(loaded_radius * CHUNK_SIZE) * 1.5
	_planet_lod_mesh_instance.visible = surface_dist > lod_show_distance


func _maybe_log_chunk_streaming(delta: float) -> void:
	if not debug_chunk_streaming:
		return
	_debug_elapsed += delta
	if _debug_elapsed < debug_chunk_streaming_interval:
		return
	_debug_elapsed = 0.0
	print("ChunkRendererBridge3D: visible=%d queued=%d loaded=%d async=%d" % [
		_visible_chunks.size(),
		_pending_view_queue.size(),
		_tracked_chunks.size(),
		world_data.get_async_pending_count() if world_data else 0,
	])
