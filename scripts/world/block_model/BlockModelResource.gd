# BlockModelResource — data-driven 3D model definition for non-terrain world
# objects (machines, magic structures, etc.).
#
# A model is composed of:
#   - boxes: simple axis-aligned box parts (BoxMesh), each with a position,
#     size, and color. Used for furnace bodies, machine casings, etc.
#   - custom_meshes: arbitrary Mesh resources (e.g. imported .glb/.obj) placed
#     at a transform with a tint color. Used for complex shapes (pipes, cables,
#     magic structure geometry).
#   - collision_boxes: axis-aligned boxes used to build the physics shape.
#
# Models are registered by `model_key` (a StringName) in a registry and
# instantiated by WorldObjectRenderer when an object is placed at a voxel cell.
# The model's local origin maps to the cell's world position (block centre).
class_name BlockModelResource
extends Resource

@export var model_key: StringName = &""

# Box parts. Each Dictionary:
#   { "position": Vector3, "size": Vector3, "color": Color }
# position/size are in voxel block units (1.0 = one block).
@export var boxes: Array[Dictionary] = []

# Custom mesh parts. Each Dictionary:
#   { "mesh_path": String, "position": Vector3, "rotation_degrees": Vector3,
#     "scale": Vector3, "color": Color }
# mesh_path is a res:// path to a Mesh resource (ArrayMesh or imported scene
# mesh). scale defaults to Vector3.ONE.
@export var custom_meshes: Array[Dictionary] = []

# Collision boxes (axis-aligned). Each Dictionary:
#   { "position": Vector3, "size": Vector3 }
# When non-empty, WorldObjectRenderer builds a StaticBody3D with
# BoxShape3D children matching these boxes.
@export var collision_boxes: Array[Dictionary] = []


# Convenience constructor for building a model in code.
static func create(key: StringName,
		p_boxes: Array[Dictionary] = [],
		p_custom_meshes: Array[Dictionary] = [],
		p_collision_boxes: Array[Dictionary] = []) -> BlockModelResource:
	var model := BlockModelResource.new()
	model.model_key = key
	model.boxes = p_boxes
	model.custom_meshes = p_custom_meshes
	model.collision_boxes = p_collision_boxes
	return model


# Returns true if the model has any renderable content.
func is_empty() -> bool:
	return boxes.is_empty() and custom_meshes.is_empty()
