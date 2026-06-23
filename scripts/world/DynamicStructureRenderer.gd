# DynamicStructureRenderer — prototype renderer for Create-style moving block structures.
#
# Rendering policy:
# - Structure block payloads are grouped by material id.
# - Each material group uses one MultiMeshInstance3D.
# - Transform sync only moves the parent Node3D; it does not rebuild block meshes.
# - Mesh rebuild happens only when a structure is assembled or its version changes.
class_name DynamicStructureRenderer
extends Node3D

@export var ship_command_bridge_path: NodePath = ^"../ShipCommandBridge"
@export var block_size := 1.0
@export var debug_material_colors := true

var _ship_bridge: Node = null
var _structures: Dictionary = {} # structure_id -> Node3D
var _versions: Dictionary = {} # structure_id -> structure_version
var _cube_mesh: BoxMesh = null
var _material_cache: Dictionary = {} # material_id -> StandardMaterial3D


func _ready() -> void:
	_cube_mesh = BoxMesh.new()
	_cube_mesh.size = Vector3.ONE * block_size
	call_deferred(&"_connect_signals")


func _connect_signals() -> void:
	_ship_bridge = get_node_or_null(ship_command_bridge_path)
	if _ship_bridge == null:
		return
	if _ship_bridge.has_signal(&"dynamic_structure_assembled") \
			and not _ship_bridge.dynamic_structure_assembled.is_connected(_on_dynamic_structure_assembled):
		_ship_bridge.dynamic_structure_assembled.connect(_on_dynamic_structure_assembled)
	if _ship_bridge.has_signal(&"dynamic_structure_transform_synced") \
			and not _ship_bridge.dynamic_structure_transform_synced.is_connected(_on_dynamic_structure_transform_synced):
		_ship_bridge.dynamic_structure_transform_synced.connect(_on_dynamic_structure_transform_synced)
	if _ship_bridge.has_signal(&"dynamic_structure_removed") \
			and not _ship_bridge.dynamic_structure_removed.is_connected(_on_dynamic_structure_removed):
		_ship_bridge.dynamic_structure_removed.connect(_on_dynamic_structure_removed)


func _on_dynamic_structure_assembled(structure: Dictionary) -> void:
	var structure_id := int(structure.get("structure_id", 0))
	if structure_id <= 0:
		return
	var version := int(structure.get("structure_version", 0))
	var node := _get_or_create_structure_node(structure_id)
	if int(_versions.get(structure_id, -1)) != version:
		_rebuild_structure_mesh(node, structure)
		_versions[structure_id] = version
	_apply_transform(node, structure.get("transform", {}))


func _on_dynamic_structure_transform_synced(snapshot: Dictionary) -> void:
	var structure_id := int(snapshot.get("structure_id", 0))
	if structure_id <= 0:
		return
	var node := _structures.get(structure_id) as Node3D
	if node == null:
		return
	_apply_transform(node, snapshot.get("transform", {}))


func _on_dynamic_structure_removed(structure_id: int) -> void:
	var node := _structures.get(structure_id) as Node3D
	if node != null:
		node.queue_free()
	_structures.erase(structure_id)
	_versions.erase(structure_id)


func _get_or_create_structure_node(structure_id: int) -> Node3D:
	var existing := _structures.get(structure_id) as Node3D
	if existing != null:
		return existing
	var node := Node3D.new()
	node.name = "DynamicStructure_%d" % structure_id
	add_child(node)
	_structures[structure_id] = node
	return node


func _rebuild_structure_mesh(node: Node3D, structure: Dictionary) -> void:
	for child in node.get_children():
		child.queue_free()

	var groups: Dictionary = {} # material_id -> Array[Vector3i]
	var blocks: Array = structure.get("blocks", [])
	for block_data in blocks:
		if not (block_data is Dictionary):
			continue
		var material_id := int(block_data.get("material", 0))
		if material_id <= 0:
			continue
		var local: Vector3i = block_data.get("local", Vector3i.ZERO)
		if not groups.has(material_id):
			groups[material_id] = []
		groups[material_id].append(local)

	for material_id in groups.keys():
		var locals: Array = groups[material_id]
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = _cube_mesh
		mm.instance_count = locals.size()
		for i in locals.size():
			var local: Vector3i = locals[i]
			var origin := Vector3(local.x, local.y, local.z) * block_size
			mm.set_instance_transform(i, Transform3D(Basis.IDENTITY, origin))

		var inst := MultiMeshInstance3D.new()
		inst.name = "Material_%d" % int(material_id)
		inst.multimesh = mm
		inst.material_override = _get_material(int(material_id))
		node.add_child(inst)


func _apply_transform(node: Node3D, transform_dict: Dictionary) -> void:
	if transform_dict.is_empty():
		return
	var position: Vector3 = transform_dict.get("position", node.position)
	node.position = position

	# C++ sends quaternion xyz + w. Use it when available; otherwise keep existing rotation.
	if transform_dict.has("rotation") and transform_dict.has("rotation_w"):
		var r: Vector3 = transform_dict.get("rotation", Vector3.ZERO)
		var w := float(transform_dict.get("rotation_w", 1.0))
		var q := Quaternion(r.x, r.y, r.z, w)
		if q.length() > 0.0001:
			node.basis = Basis(q.normalized())


func _get_material(material_id: int) -> StandardMaterial3D:
	var mat := _material_cache.get(material_id) as StandardMaterial3D
	if mat != null:
		return mat
	mat = StandardMaterial3D.new()
	mat.albedo_color = _debug_color_for_material(material_id)
	mat.roughness = 0.88
	_material_cache[material_id] = mat
	return mat


func _debug_color_for_material(material_id: int) -> Color:
	if not debug_material_colors:
		return Color(0.7, 0.7, 0.7, 1.0)
	var r := float((material_id * 53) % 255) / 255.0
	var g := float((material_id * 97) % 255) / 255.0
	var b := float((material_id * 193) % 255) / 255.0
	return Color(maxf(r, 0.18), maxf(g, 0.18), maxf(b, 0.18), 1.0)
