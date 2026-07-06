class_name WorldObjectRenderer
extends Node3D

# Renders world objects (furnaces, magic structures, etc.) using data-driven
# BlockModelResource definitions registered in BuiltinBlockModels.
# Workbenches and ladders are terrain blocks rendered by ChunkRendererBridge.
#
# Rendering policy (design doc: docs/专用引擎性能优化方向.md, render
# submission layer):
#   - Each model_key with `baked_mesh` (boxes-only) is batched into a single
#     MultiMeshInstance3D. Placing/removing an object only updates one
#     instance transform — no per-object Node3D is created.
#   - Models that use `custom_meshes` (e.g. imported .glb) fall back to the
#     legacy per-object Node3D path. None of the built-in models currently
#     use custom_meshes, so the MultiMesh path is the hot one.
#   - Collision is NOT created here. Machines sit on terrain blocks whose
#     chunk-level collision (built by GDChunkHelper.build_collision_faces +
#     machine collision overlay) covers them. See MachineCollisionBridge.
#
# Multi-planet support: only the active planet's objects are rendered. When
# the player travels, all MultiMeshes and per-object nodes are cleared and
# re-populated from the new planet's manager state.

const OVERWORLD: StringName = &"overworld"
const BuiltinBlockModelsScript := preload("res://scripts/world/block_model/BuiltinBlockModels.gd")

@export var world_path: NodePath = ^"../ChunkRendererBridge"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var universe_manager_path: NodePath = ^"../UniverseManager"

@onready var _world: ChunkRendererBridge = get_node_or_null(world_path) as ChunkRendererBridge
@onready var _furnace_manager: FurnaceManager = \
	get_node_or_null(furnace_manager_path) as FurnaceManager
@onready var _universe_manager: UniverseManager = \
	get_node_or_null(universe_manager_path) as UniverseManager

# model_key (StringName) -> BlockModelResource.
var _model_registry: Dictionary = {}

# C++ RenderingServer-backed MultiMesh manager .
# Owns RIDs directly — no MultiMeshInstance3D Node3D children.
var _cpp_renderer: GDWorldObjectRenderer = null

# Per-object fallback path for models with custom_meshes.
# Key: "<object_type>,<dimension>,<x>,<y>,<z>" (String) -> Node3D.
var _per_object_nodes: Dictionary = {}

# Tracks every rendered object to avoid duplicate placement.
# Key: same string form as _per_object_nodes. Value: true.
var _rendered_keys: Dictionary = {}

# Active dimension (planet). Only this dimension's objects are rendered.
var _active_dimension: StringName = OVERWORLD

# Shared material: vertex color drives albedo so a single MultiMesh can host
# boxes with different colors (per-vertex color baked into the mesh).
var _vertex_color_material: StandardMaterial3D = null


func _ready() -> void:
	_model_registry = BuiltinBlockModelsScript.build_registry()
	_vertex_color_material = _make_vertex_color_material()
	_init_cpp_renderer()
	_connect_manager_signals()
	# Initialize active dimension from ChunkRendererBridge if available.
	if _world != null:
		_active_dimension = _world.active_dimension
	_sync_existing_objects()


# Initialize the C++ RenderingServer-backed renderer. Binds to the parent
# viewport's World3D scenario and registers every model with a baked_mesh.
func _init_cpp_renderer() -> void:
	_cpp_renderer = GDWorldObjectRenderer.new()
	var world_3d := get_world_3d()
	var scenario := world_3d.get_scenario() if world_3d != null else RID()
	_cpp_renderer.setup(scenario, _vertex_color_material)
	for model_key: StringName in _model_registry.keys():
		var model: BlockModelResource = _model_registry[model_key]
		if model.baked_mesh != null:
			_cpp_renderer.register_model(model_key, model.baked_mesh)


func _connect_manager_signals() -> void:
	if _furnace_manager != null:
		_furnace_manager.furnace_placed.connect(_on_furnace_placed)
		_furnace_manager.furnace_removed.connect(_on_furnace_removed)
	# Listen for active planet changes to swap rendered object sets.
	if _universe_manager != null:
		_universe_manager.active_planet_changed.connect(_on_active_planet_changed)


# Active planet changed: clear all rendered objects and re-sync from managers.
func _on_active_planet_changed(planet: PlanetDescriptor) -> void:
	var new_dim: StringName = planet.dimension_id if planet != null else OVERWORLD
	if new_dim == _active_dimension:
		return
	print("[WorldObjectRenderer] active dimension changed: %s -> %s" % [
		String(_active_dimension), String(new_dim)])
	_clear_all_objects()
	# Re-register models after clear_all freed the C++ RIDs.
	if _cpp_renderer != null:
		for model_key: StringName in _model_registry.keys():
			var model: BlockModelResource = _model_registry[model_key]
			if model.baked_mesh != null:
				_cpp_renderer.register_model(model_key, model.baked_mesh)
	_active_dimension = new_dim
	_sync_existing_objects()


# Re-populate rendered objects from the FurnaceManager's authoritative state.
# Called on ready and after dimension switches.
func _sync_existing_objects() -> void:
	if _furnace_manager != null:
		for entry in _furnace_manager.get_all_furnaces():
			_on_furnace_placed(
				StringName(entry.get("dimension", "")),
				entry.get("cell", Vector3i.ZERO))


func _on_furnace_placed(dimension: StringName, cell: Vector3i) -> void:
	_create_object(&"furnace", dimension, cell)


func _on_furnace_removed(dimension: StringName, cell: Vector3i) -> void:
	_remove_object(&"furnace", dimension, cell)


# Place an object of the given type at the cell. Routes to MultiMesh when the
# model has a baked_mesh; falls back to per-object Node3D otherwise.
func _create_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	# Only render objects in the active dimension.
	if dimension != _active_dimension:
		return
	var key := _make_key(object_type, dimension, cell)
	if _rendered_keys.has(key):
		return
	var model: BlockModelResource = _model_registry.get(object_type, null)
	if model == null:
		push_warning(
			"WorldObjectRenderer: no block model registered for '%s'" %
			String(object_type))
		return
	_rendered_keys[key] = true
	if model.baked_mesh != null:
		_append_multi_mesh_instance(object_type, model, cell)
	else:
		_create_per_object_node(object_type, model, cell)


# Remove an object. Inverse of _create_object: pops the C++ MultiMesh instance
# or frees the per-object Node3D.
func _remove_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	var key := _make_key(object_type, dimension, cell)
	if not _rendered_keys.has(key):
		return
	_rendered_keys.erase(key)
	var model: BlockModelResource = _model_registry.get(object_type, null)
	if model != null and model.baked_mesh != null:
		_remove_multi_mesh_instance(object_type, cell)
	else:
		var node := _per_object_nodes.get(key) as Node
		if node != null:
			node.queue_free()
		_per_object_nodes.erase(key)


# Clear everything (C++ MultiMesh RIDs + per-object nodes). Used on dimension switch.
func _clear_all_objects() -> void:
	if _cpp_renderer != null:
		_cpp_renderer.clear_all()
	for key in _per_object_nodes.keys():
		var node := _per_object_nodes[key] as Node
		if node != null:
			node.queue_free()
	_per_object_nodes.clear()
	_rendered_keys.clear()


# --- MultiMesh path (delegated to C++ GDWorldObjectRenderer) ---

# Append one instance to the C++ MultiMesh at the cell's world position.
func _append_multi_mesh_instance(model_key: StringName,
		model: BlockModelResource, cell: Vector3i) -> void:
	if _cpp_renderer == null:
		return
	var origin := _cell_world_origin(cell)
	_cpp_renderer.place_object(model_key, cell, origin)


# Swap-pop the instance at the given cell via the C++ renderer.
func _remove_multi_mesh_instance(model_key: StringName, cell: Vector3i) -> void:
	if _cpp_renderer == null:
		return
	_cpp_renderer.remove_object(model_key, cell)


# --- Per-object fallback path (for models with custom_meshes) ---

# Build a Node3D with MeshInstance3D children for each custom_mesh entry.
# Mirrors the legacy _build_model flow but skips boxes (those go via MultiMesh)
# and skips collision (handled by chunk collision + MachineCollisionBridge).
func _create_per_object_node(object_type: StringName,
		model: BlockModelResource, cell: Vector3i) -> void:
	if _world == null:
		return
	var root := Node3D.new()
	root.name = "%s_%d_%d_%d" % [object_type, cell.x, cell.y, cell.z]
	root.global_position = _cell_world_origin(cell)
	add_child(root)
	for mesh_def: Dictionary in model.custom_meshes:
		_add_custom_mesh(root, mesh_def)
	var key := _make_key(object_type, _active_dimension, cell)
	_per_object_nodes[key] = root


# Append a custom mesh (imported .glb/.obj) under the per-object root.
func _add_custom_mesh(root: Node3D, mesh_def: Dictionary) -> void:
	var mesh_path: String = mesh_def.get("mesh_path", "")
	if mesh_path == "":
		return
	var mesh_res: Resource = load(mesh_path)
	if mesh_res == null:
		push_warning("WorldObjectRenderer: failed to load custom mesh '%s'" % mesh_path)
		return
	var instance := MeshInstance3D.new()
	# Support both direct Mesh resources and PackedScene (imported .glb).
	if mesh_res is Mesh:
		instance.mesh = mesh_res
	elif mesh_res is PackedScene:
		var scene_root: Node3D = mesh_res.instantiate()
		if scene_root != null:
			root.add_child(scene_root)
			scene_root.position = mesh_def.get("position", Vector3.ZERO)
			scene_root.rotation_degrees = mesh_def.get("rotation_degrees", Vector3.ZERO)
			scene_root.scale = mesh_def.get("scale", Vector3.ONE)
			var color: Color = mesh_def.get("color", Color.WHITE)
			if color != Color.WHITE:
				_apply_tint(scene_root, color)
			return
		push_warning("WorldObjectRenderer: failed to instantiate scene '%s'" % mesh_path)
		return
	else:
		push_warning("WorldObjectRenderer: unsupported mesh resource type for '%s'" % mesh_path)
		return
	instance.position = mesh_def.get("position", Vector3.ZERO)
	instance.rotation_degrees = mesh_def.get("rotation_degrees", Vector3.ZERO)
	instance.scale = mesh_def.get("scale", Vector3.ONE)
	var color: Color = mesh_def.get("color", Color.WHITE)
	if color != Color.WHITE:
		instance.material_override = _make_tint_material(color)
	root.add_child(instance)


func _apply_tint(node: Node3D, color: Color) -> void:
	if node is MeshInstance3D:
		(node as MeshInstance3D).material_override = _make_tint_material(color)
	for child in node.get_children():
		if child is Node3D:
			_apply_tint(child, color)


# --- Helpers ---

# World-space origin for an instance at the given cell (cell center).
# Matches GDChunkHelper.cell_to_world_position semantics so visuals align
# with the existing chunk mesher coordinate convention.
func _cell_world_origin(cell: Vector3i) -> Vector3:
	if _world != null:
		return _world.cell_to_world_position(cell)
	return Vector3(
		float(cell.x) + 0.5,
		float(cell.y) + 0.5,
		float(cell.z) + 0.5)


# Vertex-color material shared across all MultiMeshes. Albedo comes from
# per-vertex color baked into each model's ArrayMesh.
func _make_vertex_color_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color.WHITE
	material.vertex_color_use_as_albedo = true
	material.roughness = 0.9
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	return material


# Tint material for the legacy per-object path (matches the previous look).
func _make_tint_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.9
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	return material


func _make_key(object_type: StringName, dimension: StringName, cell: Vector3i) -> String:
	return "%s,%s,%d,%d,%d" % [object_type, dimension, cell.x, cell.y, cell.z]
