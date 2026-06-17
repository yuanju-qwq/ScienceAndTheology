class_name ChunkRendererBridge
extends Node3D

signal chunk_bridge_ready

const BuiltinTerrainContentScript := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")
const CHUNK_SIZE := 32
const BLOCK_SIZE := 1.0
const AIR_MATERIAL := 0
const LADDER_MATERIAL := 11
const OVERWORLD: StringName = &"overworld"

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

	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)

	_build_materials()
	_create_planet_lod()
	if auto_generate_start_chunks:
		_generate_initial_chunks()

	is_initialized = true
	chunk_bridge_ready.emit()


func get_world_data() -> GDWorldData:
	return world_data


func world_position_to_cell(world_position: Vector3) -> Vector3i:
	return Vector3i(
		floori(world_position.x / BLOCK_SIZE),
		floori(world_position.y / BLOCK_SIZE),
		floori(world_position.z / BLOCK_SIZE))


func cell_to_world_position(cell: Vector3i) -> Vector3:
	return Vector3(
		(float(cell.x) + 0.5) * BLOCK_SIZE,
		(float(cell.y) + 0.5) * BLOCK_SIZE,
		(float(cell.z) + 0.5) * BLOCK_SIZE)


func world_position_to_chunk(world_position: Vector3) -> Vector3i:
	var cell := world_position_to_cell(world_position)
	return cell_to_chunk(cell)


func cell_to_chunk(cell: Vector3i) -> Vector3i:
	return Vector3i(
		floori(float(cell.x) / float(CHUNK_SIZE)),
		floori(float(cell.y) / float(CHUNK_SIZE)),
		floori(float(cell.z) / float(CHUNK_SIZE)))


func cell_to_local(cell: Vector3i) -> Vector3i:
	var chunk := cell_to_chunk(cell)
	return Vector3i(
		cell.x - chunk.x * CHUNK_SIZE,
		cell.y - chunk.y * CHUNK_SIZE,
		cell.z - chunk.z * CHUNK_SIZE)


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


func _generate_initial_chunks() -> void:
	for cz in range(-start_chunk_radius, start_chunk_radius + 1):
		for cx in range(-start_chunk_radius, start_chunk_radius + 1):
			var pos := Vector3i(cx, 0, cz)
			_ensure_chunk_loaded(pos)
			_enqueue_chunk_view(pos)


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_chunk_request_count_this_frame = 0

	var wanted_loaded: Dictionary = {}
	var wanted_visible: Dictionary = {}
	var visible_order: Array[Vector3i] = []

	if use_spherical_loading:
		# Spherical loading: iterate in 3D radius around the player chunk.
		var lr_sq := loaded_radius * loaded_radius
		var vr_sq := view_radius * view_radius
		for cy in range(player_chunk.y - loaded_radius, player_chunk.y + loaded_radius + 1):
			var dy_sq := (cy - player_chunk.y) * (cy - player_chunk.y)
			if dy_sq > lr_sq:
				continue
			for cz in range(player_chunk.z - loaded_radius, player_chunk.z + loaded_radius + 1):
				var dz_sq := (cz - player_chunk.z) * (cz - player_chunk.z)
				if dy_sq + dz_sq > lr_sq:
					continue
				for cx in range(player_chunk.x - loaded_radius, player_chunk.x + loaded_radius + 1):
					var dx_sq := (cx - player_chunk.x) * (cx - player_chunk.x)
					if dx_sq + dy_sq + dz_sq > lr_sq:
						continue
					var pos := Vector3i(cx, cy, cz)
					wanted_loaded[pos] = true
					_ensure_chunk_loaded(pos)
					if dx_sq + dy_sq + dz_sq <= vr_sq:
						wanted_visible[pos] = true
						visible_order.append(pos)
	else:
		# Flat loading: original XZ-plane-only loading.
		for cz in range(player_chunk.z - loaded_radius, player_chunk.z + loaded_radius + 1):
			for cx in range(player_chunk.x - loaded_radius, player_chunk.x + loaded_radius + 1):
				var dx := cx - player_chunk.x
				var dz := cz - player_chunk.z
				if dx * dx + dz * dz > loaded_radius * loaded_radius:
					continue
				var pos := Vector3i(cx, player_chunk.y, cz)
				wanted_loaded[pos] = true
				_ensure_chunk_loaded(pos)

		for cz in range(player_chunk.z - view_radius, player_chunk.z + view_radius + 1):
			for cx in range(player_chunk.x - view_radius, player_chunk.x + view_radius + 1):
				var dx := cx - player_chunk.x
				var dz := cz - player_chunk.z
				if dx * dx + dz * dz > view_radius * view_radius:
					continue
				var pos := Vector3i(cx, player_chunk.y, cz)
				wanted_visible[pos] = true
				visible_order.append(pos)

	visible_order.sort_custom(func(a: Vector3i, b: Vector3i) -> bool:
		var da := (a.x - player_chunk.x) * (a.x - player_chunk.x) + (a.y - player_chunk.y) * (a.y - player_chunk.y) + (a.z - player_chunk.z) * (a.z - player_chunk.z)
		var db := (b.x - player_chunk.x) * (b.x - player_chunk.x) + (b.y - player_chunk.y) * (b.y - player_chunk.y) + (b.z - player_chunk.z) * (b.z - player_chunk.z)
		if da != db:
			return da < db
		if a.x != b.x:
			return a.x < b.x
		if a.y != b.y:
			return a.y < b.y
		return a.z < b.z
	)

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
				var index := _terrain_index(local_x, local_y, local_z, size_x, size_z)
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

	# Ladder blocks use a thin panel mesh and no collision.
	var is_ladder := material_id == LADDER_MATERIAL
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
			var facing := _ladder_facing(local, materials, size_x, size_y, size_z)
			var basis := Basis().rotated(Vector3.UP, facing)
			multimesh.set_instance_transform(i, Transform3D(basis, pos))
		else:
			multimesh.set_instance_transform(i, Transform3D(Basis(), pos))

	var instance := MultiMeshInstance3D.new()
	instance.name = "Material_%d" % material_id
	instance.multimesh = multimesh
	instance.material_override = _get_material(material_id)
	root.add_child(instance)

	# Climbable blocks (ladders) do not generate solid collision.
	if is_ladder:
		return

	var static_body := StaticBody3D.new()
	static_body.name = "Collision_%d" % material_id
	root.add_child(static_body)

	for local in cells:
		if not _is_surface_voxel(local, materials, size_x, size_y, size_z):
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


func _terrain_index(local_x: int, local_y: int, local_z: int, size_x: int, size_z: int) -> int:
	return (local_y * size_z + local_z) * size_x + local_x


func _is_surface_voxel(local: Vector3i, materials: PackedByteArray, size_x: int, size_y: int, size_z: int) -> bool:
	var offsets: Array[Vector3i] = [
		Vector3i.RIGHT, Vector3i.LEFT,
		Vector3i.UP, Vector3i.DOWN,
		Vector3i.FORWARD, Vector3i.BACK
	]
	for offset: Vector3i in offsets:
		var n := local + offset
		if n.x < 0 or n.x >= size_x or n.y < 0 or n.y >= size_y or n.z < 0 or n.z >= size_z:
			return true
		var index := _terrain_index(n.x, n.y, n.z, size_x, size_z)
		if index >= materials.size():
			return true
		var neighbor_material := int(materials[index])
		# Air and climbable (ladder) blocks are non-solid; adjacent
		# solid blocks should generate collision on that face.
		if neighbor_material == AIR_MATERIAL or neighbor_material == LADDER_MATERIAL:
			return true
	return false


# Returns the Y-axis rotation angle (radians) for a ladder block
# based on which adjacent cell is solid (the wall it attaches to).
func _ladder_facing(local: Vector3i, materials: PackedByteArray, size_x: int, size_y: int, size_z: int) -> float:
	# Check horizontal neighbors in priority order: +Z, -Z, +X, -X.
	# The ladder panel faces toward the solid wall.
	var checks: Array[Dictionary] = [
		{ "offset": Vector3i.FORWARD, "angle": PI },
		{ "offset": Vector3i.BACK, "angle": 0.0 },
		{ "offset": Vector3i.RIGHT, "angle": PI * 0.5 },
		{ "offset": Vector3i.LEFT, "angle": PI * 1.5 },
	]
	for check in checks:
		var n: Vector3i = local + check.get("offset", Vector3i.ZERO)
		if n.x < 0 or n.x >= size_x or n.y < 0 or n.y >= size_y or n.z < 0 or n.z >= size_z:
			continue
		var index := _terrain_index(n.x, n.y, n.z, size_x, size_z)
		if index < materials.size() and int(materials[index]) != AIR_MATERIAL:
			return check.get("angle", 0.0)
	# Default facing: toward -Z (no wall found).
	return 0.0


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

	# Show LOD sphere when player is far from the surface.
	# Hide when close enough that real chunks are visible.
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
