# ChunkRendererBridge — 3D chunk rendering bridge for the voxel world.
# Delegates pure-computation helpers to GDChunkHelper (C++) and keeps
# Godot scene-tree operations (MultiMesh, collision) in GDScript.
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

	var size_x := int(terrain.get("size_x", CHUNK_SIZE))
	var size_y := int(terrain.get("size_y", CHUNK_SIZE))
	var size_z := int(terrain.get("size_z", CHUNK_SIZE))

	# Compute surface mask for LOD 1 simplified rendering.
	var surface_mask: PackedByteArray = GDChunkHelper.compute_surface_mask(
		materials, size_x, size_y, size_z, AIR_MATERIAL, ladder_material_id)

	# Classify voxels by material and surface status.
	var by_material: Dictionary = {}
	var by_material_surface_only: Dictionary = {}
	for local_y in range(size_y):
		for local_z in range(size_z):
			for local_x in range(size_x):
				var idx := GDChunkHelper.terrain_index(local_x, local_y, local_z, size_x, size_z)
				if idx >= materials.size():
					continue
				var material_id := int(materials[idx])
				if material_id == AIR_MATERIAL:
					continue
				var local := Vector3i(local_x, local_y, local_z)
				if not by_material.has(material_id):
					by_material[material_id] = []
				by_material[material_id].append(local)
				if idx < surface_mask.size() and surface_mask[idx] != 0:
					if not by_material_surface_only.has(material_id):
						by_material_surface_only[material_id] = []
					by_material_surface_only[material_id].append(local)

	# Root node holds both LOD 0 and LOD 1 children.
	var root := Node3D.new()
	root.name = "Chunk_%d_%d_%d" % [chunk.x, chunk.y, chunk.z]
	add_child(root)

	# LOD 0: full chunk (all voxels + collision).
	var full_node := Node3D.new()
	full_node.name = "LOD0_Full"
	root.add_child(full_node)
	for material_id in by_material.keys():
		_create_material_multimesh(full_node, chunk, int(material_id),
			by_material[material_id], materials, size_x, size_y, size_z, true)

	# LOD 1: simplified chunk (surface voxels only, no collision).
	var simplified_node := Node3D.new()
	simplified_node.name = "LOD1_Simplified"
	simplified_node.visible = false
	root.add_child(simplified_node)
	for material_id in by_material_surface_only.keys():
		_create_material_multimesh(simplified_node, chunk, int(material_id),
			by_material_surface_only[material_id], materials, size_x, size_y, size_z, false)

	_visible_chunks[chunk] = {
		"root": root,
		"full": full_node,
		"simplified": simplified_node,
	}


func _create_material_multimesh(root: Node3D, chunk: Vector3i, material_id: int, cells: Array,
		materials: PackedByteArray, size_x: int, size_y: int, size_z: int,
		create_collision: bool) -> void:
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

	if not create_collision or is_ladder:
		return

	# Collision only for LOD 0 full view.
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
	var entry: Dictionary = _visible_chunks.get(chunk, {})
	var node: Node = entry.get("root") as Node
	if node == null:
		return
	_visible_chunks.erase(chunk)
	node.queue_free()


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


func _get_material(material_id: int) -> StandardMaterial3D:
	if _material_cache.has(material_id):
		return _material_cache[material_id]

	var material := StandardMaterial3D.new()
	var visual: Dictionary = _materials.get(material_id, {})

	if visual.is_empty():
		material.albedo_color = Color(0.85, 0.20, 0.85)
		material.roughness = 0.92
		material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
		_material_cache[material_id] = material
		return material

	var albedo: Color = visual.get("albedo_color", Color(0.85, 0.20, 0.85))
	material.albedo_color = albedo
	material.roughness = visual.get("roughness", 0.92)
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED

	var emissive: Color = visual.get("emissive_color", Color(0, 0, 0))
	if emissive != Color(0, 0, 0):
		material.emission_enabled = true
		material.emission = emissive

	if visual.get("transparent", false) or albedo.a < 1.0:
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if visual.get("cull_disabled", false) or albedo.a < 1.0:
		material.cull_mode = BaseMaterial3D.CULL_DISABLED

	# Load per-face textures if available.
	# Currently BoxMesh uses a single material_override, so we pick the
	# best available texture: sides > top > bottom. Full per-face UV
	# support requires greedy meshing (future R2 milestone).
	var sides_tex_path: String = visual.get("sides", {}).get("texture_path", "")
	var top_tex_path: String = visual.get("top", {}).get("texture_path", "")
	var chosen_path := sides_tex_path if sides_tex_path != "" else top_tex_path
	if chosen_path == "":
		var bottom_tex_path: String = visual.get("bottom", {}).get("texture_path", "")
		chosen_path = bottom_tex_path

	if chosen_path != "":
		var tex := load(chosen_path) as Texture2D
		if tex != null:
			material.albedo_texture = tex
		else:
			push_warning("ChunkRendererBridge: failed to load texture '%s' for material %d" % [chosen_path, material_id])

	# Overlay layers: the first overlay texture is applied as a detail map
	# with blend mode. Full multi-overlay compositing requires a custom shader
	# or texture pre-bake (future milestone).
	var overlays: Array = visual.get("overlays", [])
	if overlays.size() > 0:
		var overlay_path: String = overlays[0].get("texture_path", "")
		if overlay_path != "":
			var overlay_tex := load(overlay_path) as Texture2D
			if overlay_tex != null:
				material.detail_enabled = true
				material.detail_albedo = overlay_tex
				material.detail_blend_mode = BaseMaterial3D.BLEND_MODE_MUL

	_material_cache[material_id] = material
	return material


# --- Planet LOD integration ---

func _connect_planet_lod_manager() -> void:
	_planet_lod_manager = get_node_or_null(planet_lod_manager_path) as PlanetLodManager
	if _planet_lod_manager == null:
		push_warning("ChunkRendererBridge: PlanetLodManager not found at %s" % planet_lod_manager_path)


# Switch between LOD 0 (full) and LOD 1 (simplified) chunk views,
# or hide chunks entirely when the proxy sphere is active (LOD 2+).
func _update_chunk_visibility_for_lod() -> void:
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
