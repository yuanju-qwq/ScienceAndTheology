class_name WorldObjectRenderer
extends Node3D

# Renders world objects (furnaces, magic structures, etc.) using data-driven
# BlockModelResource definitions registered in BuiltinBlockModels.
# Workbenches and ladders are terrain blocks rendered by ChunkRendererBridge.
#
# 多星球支持：只渲染当前活跃星球的物体。当玩家旅行到其他星球时，
# 旧星球的物体会被隐藏，新星球的物体会被显示。

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

var _objects: Dictionary = {}
# model_key (StringName) -> BlockModelResource.
var _model_registry: Dictionary = {}
# 当前活跃维度（星球）。只有此维度的物体才会被渲染。
var _active_dimension: StringName = OVERWORLD


func _ready() -> void:
	_model_registry = BuiltinBlockModelsScript.build_registry()
	_connect_manager_signals()
	# 初始化活跃维度（从 ChunkRendererBridge 读取，若可获取）。
	if _world != null:
		_active_dimension = _world.active_dimension
	_sync_existing_objects()


func _connect_manager_signals() -> void:
	if _furnace_manager != null:
		_furnace_manager.furnace_placed.connect(_on_furnace_placed)
		_furnace_manager.furnace_removed.connect(_on_furnace_removed)
	# 监听活跃星球变化，自动切换渲染的物体集。
	if _universe_manager != null:
		_universe_manager.active_planet_changed.connect(_on_active_planet_changed)


# 当活跃星球变化时，清除旧维度的物体，加载新维度的物体。
func _on_active_planet_changed(planet: PlanetDescriptor) -> void:
	var new_dim: StringName = planet.dimension_id if planet != null else OVERWORLD
	if new_dim == _active_dimension:
		return
	print("[WorldObjectRenderer] active dimension changed: %s -> %s" % [
		String(_active_dimension), String(new_dim)])
	_clear_all_objects()
	_active_dimension = new_dim
	_sync_existing_objects()


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


func _create_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	# 只渲染当前活跃维度的物体。
	if dimension != _active_dimension:
		return
	var key := _make_key(object_type, dimension, cell)
	if _objects.has(key) or _world == null:
		return

	var model: BlockModelResource = _model_registry.get(object_type, null)
	if model == null:
		push_warning(
			"WorldObjectRenderer: no block model registered for '%s'" %
			String(object_type))
		return

	var root := Node3D.new()
	root.name = "%s_%d_%d_%d" % [object_type, cell.x, cell.y, cell.z]
	root.global_position = _world.cell_to_world_position(cell)
	add_child(root)
	_build_model(root, model)
	_objects[key] = root


func _remove_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	var key := _make_key(object_type, dimension, cell)
	var node := _objects.get(key) as Node
	if node == null:
		return
	_objects.erase(key)
	node.queue_free()


# 清除所有已渲染物体（用于维度切换时）。
func _clear_all_objects() -> void:
	for key in _objects.keys():
		var node := _objects[key] as Node
		if node != null:
			node.queue_free()
	_objects.clear()


# Instantiate a BlockModelResource under `root`: box parts, custom meshes,
# and collision boxes.
func _build_model(root: Node3D, model: BlockModelResource) -> void:
	for box: Dictionary in model.boxes:
		_add_box(root,
			box.get("position", Vector3.ZERO),
			box.get("size", Vector3.ONE),
			box.get("color", Color.WHITE))
	for mesh_def: Dictionary in model.custom_meshes:
		_add_custom_mesh(root, mesh_def)
	if model.collision_boxes.size() > 0:
		_add_collision(root, model.collision_boxes)


func _add_box(root: Node3D, position: Vector3, size: Vector3, color: Color) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = _make_material(color)
	instance.position = position
	root.add_child(instance)
	return instance


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
		instance.material_override = _make_material(color)
	root.add_child(instance)


func _apply_tint(node: Node3D, color: Color) -> void:
	if node is MeshInstance3D:
		(node as MeshInstance3D).material_override = _make_material(color)
	for child in node.get_children():
		if child is Node3D:
			_apply_tint(child, color)


func _add_collision(root: Node3D, collision_boxes: Array[Dictionary]) -> void:
	var static_body := StaticBody3D.new()
	static_body.name = "Collision"
	for box: Dictionary in collision_boxes:
		var shape := BoxShape3D.new()
		shape.size = box.get("size", Vector3.ONE)
		var col_shape := CollisionShape3D.new()
		col_shape.shape = shape
		col_shape.position = box.get("position", Vector3.ZERO)
		static_body.add_child(col_shape)
	root.add_child(static_body)


func _make_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.9
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	return material


func _make_key(object_type: StringName, dimension: StringName, cell: Vector3i) -> String:
	return "%s,%s,%d,%d,%d" % [object_type, dimension, cell.x, cell.y, cell.z]
