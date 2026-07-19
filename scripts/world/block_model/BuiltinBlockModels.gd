# BuiltinBlockModels — registers the built-in BlockModelResource definitions
# used by WorldObjectRenderer. Models are keyed by a StringName that matches
# the object_type passed to WorldObjectRenderer._create_object().
#
# Each model's `boxes` are baked into a single ArrayMesh (`baked_mesh`) at
# registration time so WorldObjectRenderer can batch instances via MultiMesh
# (one MultiMeshInstance3D per model_key). See design doc:
# docs/项目架构与运行时.md (render submission boundary).
#
# To add a new model, append it to the registry returned by build_registry().
class_name BuiltinBlockModels
extends RefCounted


# Six faces of an axis-aligned unit cube.
# Each entry: [normal: Vector3, v0, v1, v2, v3: Vector3]
# Vertices are CCW when viewed from outside the cube, matching the winding
# used by GDChunkHelper::build_collision_faces so culling stays consistent.
const _BOX_FACES: Array = [
	# +Y (top)
	[Vector3(0, 1, 0),
	 Vector3(0, 1, 0), Vector3(1, 1, 0), Vector3(1, 1, 1), Vector3(0, 1, 1)],
	# -Y (bottom)
	[Vector3(0, -1, 0),
	 Vector3(0, 0, 1), Vector3(1, 0, 1), Vector3(1, 0, 0), Vector3(0, 0, 0)],
	# +X (right)
	[Vector3(1, 0, 0),
	 Vector3(1, 0, 1), Vector3(1, 1, 1), Vector3(1, 1, 0), Vector3(1, 0, 0)],
	# -X (left)
	[Vector3(-1, 0, 0),
	 Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3(0, 1, 1), Vector3(0, 0, 1)],
	# +Z (front)
	[Vector3(0, 0, 1),
	 Vector3(0, 0, 1), Vector3(0, 1, 1), Vector3(1, 1, 1), Vector3(1, 0, 1)],
	# -Z (back)
	[Vector3(0, 0, -1),
	 Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(0, 1, 0), Vector3(0, 0, 0)],
]


# Build the registry of all built-in block models.
# Each model with non-empty `boxes` gets its `baked_mesh` populated for
# MultiMesh batching by WorldObjectRenderer.
# Returns: Dictionary { StringName model_key -> BlockModelResource }
static func build_registry() -> Dictionary:
	var registry: Dictionary = {}
	registry[&"campfire"] = _make_campfire_model()
	registry[&"magic_structure"] = _make_magic_structure_model()
	for key: StringName in registry.keys():
		var model: BlockModelResource = registry[key]
		if model.boxes.is_empty():
			continue
		model.baked_mesh = bake_boxes_to_mesh(model.boxes)
	return registry


# Bake a list of box definitions into a single ArrayMesh with per-vertex color.
# Each box: { "position": Vector3, "size": Vector3, "color": Color }.
# Vertices are emitted as unindexed triangles (6 vertices per face, 36 per box)
# so each face owns its own normal/color — no vertex sharing across faces.
# Returns null when `boxes` is empty.
static func bake_boxes_to_mesh(boxes: Array[Dictionary]) -> ArrayMesh:
	if boxes.is_empty():
		return null
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for box: Dictionary in boxes:
		var size: Vector3 = box.get("size", Vector3.ONE)
		var pos: Vector3 = box.get("position", Vector3.ZERO)
		var color: Color = box.get("color", Color.WHITE)
		_append_box(st, pos, size, color)
	st.index()
	var mesh := st.commit()
	return mesh


# Append one box (6 faces, 36 vertices total) to the SurfaceTool.
# `pos` is the box CENTER in the model's local space (matches BoxMesh.size
# semantics: BoxMesh is centered at origin and the MeshInstance3D is placed at
# `pos`, so the box extends ±size/2 around `pos`).
static func _append_box(st: SurfaceTool, pos: Vector3, size: Vector3, color: Color) -> void:
	# Unit-cube corners (0..1) → centered offsets (-size/2..+size/2).
	var half_size: Vector3 = size * 0.5
	for face: Array in _BOX_FACES:
		var normal: Vector3 = face[0]
		var v0: Vector3 = face[1] * size - half_size + pos
		var v1: Vector3 = face[2] * size - half_size + pos
		var v2: Vector3 = face[3] * size - half_size + pos
		var v3: Vector3 = face[4] * size - half_size + pos
		# Triangle 1: v0, v1, v2
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v0)
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v1)
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v2)
		# Triangle 2: v0, v2, v3
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v0)
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v2)
		st.set_normal(normal)
		st.set_color(color)
		st.add_vertex(v3)


# Campfire model: a small log pile with a flame on top.
# Collision is handled by MachineCollisionOverlay (campfire cell marked in
# WorldData, merged into chunk collision by GDChunkHelper::build_collision_faces).
static func _make_campfire_model() -> BlockModelResource:
	var boxes: Array[Dictionary] = [
		# Log base (two crossed logs).
		{ "position": Vector3(-0.20, 0.05, 0.0), "size": Vector3(0.40, 0.12, 0.12),
		  "color": Color(0.45, 0.25, 0.10) },
		{ "position": Vector3(0.20, 0.05, 0.0), "size": Vector3(0.40, 0.12, 0.12),
		  "color": Color(0.50, 0.28, 0.12) },
		{ "position": Vector3(0.0, 0.05, -0.20), "size": Vector3(0.12, 0.12, 0.40),
		  "color": Color(0.48, 0.26, 0.11) },
		{ "position": Vector3(0.0, 0.05, 0.20), "size": Vector3(0.12, 0.12, 0.40),
		  "color": Color(0.52, 0.30, 0.13) },
		# Fire (emissive orange).
		{ "position": Vector3(0.0, 0.20, 0.0), "size": Vector3(0.30, 0.24, 0.30),
		  "color": Color(1.0, 0.45, 0.10) },
		# Inner fire (brighter).
		{ "position": Vector3(0.0, 0.22, 0.0), "size": Vector3(0.18, 0.16, 0.18),
		  "color": Color(1.0, 0.70, 0.20) },
	]
	return BlockModelResource.create(&"campfire", boxes)


# Magic structure model: a placeholder runic pedestal with a floating crystal.
# Rendered as boxes until dedicated magic-structure mesh assets exist; the
# custom_meshes slot is ready to receive an imported mesh path later.
# Collision: when a manager places magic structures, it should mark the cell
# in MachineCollisionOverlay (see MachineCollisionBridge).
static func _make_magic_structure_model() -> BlockModelResource:
	var boxes: Array[Dictionary] = [
		# Pedestal base.
		{ "position": Vector3(0.0, -0.30, 0.0), "size": Vector3(0.80, 0.20, 0.80),
		  "color": Color(0.30, 0.28, 0.40) },
		# Pedestal column.
		{ "position": Vector3(0.0, 0.05, 0.0), "size": Vector3(0.40, 0.50, 0.40),
		  "color": Color(0.35, 0.33, 0.45) },
		# Floating crystal (emissive-looking cyan).
		{ "position": Vector3(0.0, 0.55, 0.0), "size": Vector3(0.30, 0.40, 0.30),
		  "color": Color(0.20, 0.85, 0.90) },
	]
	return BlockModelResource.create(&"magic_structure", boxes)
