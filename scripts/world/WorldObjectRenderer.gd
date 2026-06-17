class_name WorldObjectRenderer
extends Node3D

const OVERWORLD: StringName = &"overworld"

@export var world_path: NodePath = ^"../ChunkRendererBridge"
@export var workbench_manager_path: NodePath = ^"../WorkbenchManager"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var ladder_manager_path: NodePath = ^"../LadderManager"

@onready var _world: ChunkRendererBridge = get_node_or_null(world_path) as ChunkRendererBridge
@onready var _workbench_manager: WorkbenchManager = get_node_or_null(workbench_manager_path) as WorkbenchManager
@onready var _furnace_manager: FurnaceManager = get_node_or_null(furnace_manager_path) as FurnaceManager
@onready var _ladder_manager: LadderManager = get_node_or_null(ladder_manager_path) as LadderManager

var _objects: Dictionary = {}
var _materials: Dictionary = {}


func _ready() -> void:
	_build_materials()
	_connect_manager_signals()
	_sync_existing_objects()


func _connect_manager_signals() -> void:
	if _workbench_manager != null:
		_workbench_manager.workbench_placed.connect(_on_workbench_placed)
		_workbench_manager.workbench_removed.connect(_on_workbench_removed)
	if _furnace_manager != null:
		_furnace_manager.furnace_placed.connect(_on_furnace_placed)
		_furnace_manager.furnace_removed.connect(_on_furnace_removed)
	if _ladder_manager != null:
		_ladder_manager.ladder_placed.connect(_on_ladder_placed)
		_ladder_manager.ladder_removed.connect(_on_ladder_removed)


func _sync_existing_objects() -> void:
	if _workbench_manager != null:
		for entry in _workbench_manager.get_all_workbenches():
			_on_workbench_placed(StringName(entry.get("dimension", "")), entry.get("cell", Vector3i.ZERO))
	if _furnace_manager != null:
		for entry in _furnace_manager.get_all_furnaces():
			_on_furnace_placed(StringName(entry.get("dimension", "")), entry.get("cell", Vector3i.ZERO))
	if _ladder_manager != null:
		for entry in _ladder_manager.get_all_ladders():
			_on_ladder_placed(StringName(entry.get("dimension", "")), entry.get("cell", Vector3i.ZERO))


func _on_workbench_placed(dimension: StringName, cell: Vector3i) -> void:
	_create_object(&"workbench", dimension, cell)


func _on_workbench_removed(dimension: StringName, cell: Vector3i) -> void:
	_remove_object(&"workbench", dimension, cell)


func _on_furnace_placed(dimension: StringName, cell: Vector3i) -> void:
	_create_object(&"furnace", dimension, cell)


func _on_furnace_removed(dimension: StringName, cell: Vector3i) -> void:
	_remove_object(&"furnace", dimension, cell)


func _on_ladder_placed(dimension: StringName, cell: Vector3i) -> void:
	_create_object(&"ladder", dimension, cell)


func _on_ladder_removed(dimension: StringName, cell: Vector3i) -> void:
	_remove_object(&"ladder", dimension, cell)


func _create_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	if dimension != OVERWORLD:
		return
	var key := _make_key(object_type, dimension, cell)
	if _objects.has(key) or _world == null:
		return

	var root := Node3D.new()
	root.name = "%s_%d_%d_%d" % [object_type, cell.x, cell.y, cell.z]
	root.global_position = _world.cell_to_world_position(cell)
	add_child(root)
	_objects[key] = root

	match object_type:
		&"workbench":
			_build_workbench(root)
		&"furnace":
			_build_furnace(root)
		&"ladder":
			_build_ladder(root)


func _remove_object(object_type: StringName, dimension: StringName, cell: Vector3i) -> void:
	var key := _make_key(object_type, dimension, cell)
	var node := _objects.get(key) as Node
	if node == null:
		return
	_objects.erase(key)
	node.queue_free()


func _build_workbench(root: Node3D) -> void:
	_add_box(root, Vector3(0.0, 0.25, 0.0), Vector3(0.92, 0.50, 0.92), _materials.wood)
	_add_box(root, Vector3(0.0, 0.55, 0.0), Vector3(1.0, 0.10, 1.0), _materials.table_top)
	for x in [-0.32, 0.32]:
		for z in [-0.32, 0.32]:
			_add_box(root, Vector3(x, -0.02, z), Vector3(0.16, 0.42, 0.16), _materials.dark_wood)


func _build_furnace(root: Node3D) -> void:
	_add_box(root, Vector3(0.0, 0.32, 0.0), Vector3(0.96, 0.72, 0.96), _materials.stone)
	_add_box(root, Vector3(0.0, 0.32, -0.49), Vector3(0.46, 0.32, 0.04), _materials.furnace_mouth)
	_add_box(root, Vector3(0.0, 0.58, -0.515), Vector3(0.30, 0.08, 0.03), _materials.hot)


func _build_ladder(root: Node3D) -> void:
	for x in [-0.26, 0.26]:
		_add_box(root, Vector3(x, 0.42, -0.40), Vector3(0.08, 0.92, 0.08), _materials.dark_wood)
	for y in [0.06, 0.28, 0.50, 0.72]:
		_add_box(root, Vector3(0.0, y, -0.40), Vector3(0.62, 0.06, 0.08), _materials.table_top)


func _add_box(root: Node3D, position: Vector3, size: Vector3, material: Material) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = position
	root.add_child(instance)
	return instance


func _build_materials() -> void:
	_materials = {
		"wood": _make_material(Color(0.50, 0.32, 0.16)),
		"dark_wood": _make_material(Color(0.28, 0.17, 0.08)),
		"table_top": _make_material(Color(0.66, 0.45, 0.22)),
		"stone": _make_material(Color(0.42, 0.40, 0.36)),
		"furnace_mouth": _make_material(Color(0.08, 0.075, 0.07)),
		"hot": _make_material(Color(1.0, 0.35, 0.08)),
	}


func _make_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.9
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	return material


func _make_key(object_type: StringName, dimension: StringName, cell: Vector3i) -> String:
	return "%s,%s,%d,%d,%d" % [object_type, dimension, cell.x, cell.y, cell.z]
