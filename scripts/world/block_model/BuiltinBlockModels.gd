# BuiltinBlockModels — registers the built-in BlockModelResource definitions
# used by WorldObjectRenderer. Models are keyed by a StringName that matches
# the object_type passed to WorldObjectRenderer._create_object().
#
# To add a new model, append it to the registry returned by build_registry().
class_name BuiltinBlockModels
extends RefCounted


# Build the registry of all built-in block models.
# Returns: Dictionary { StringName model_key -> BlockModelResource }
static func build_registry() -> Dictionary:
	var registry: Dictionary = {}
	registry[&"furnace"] = _make_furnace_model()
	registry[&"magic_structure"] = _make_magic_structure_model()
	return registry


# Furnace model: stone body with a dark mouth opening and a hot bar above it.
# Ports the previous hardcoded BoxMesh combination in WorldObjectRenderer.
static func _make_furnace_model() -> BlockModelResource:
	var boxes: Array[Dictionary] = [
		{ "position": Vector3(0.0, 0.32, 0.0), "size": Vector3(0.96, 0.72, 0.96),
		  "color": Color(0.42, 0.40, 0.36) },
		{ "position": Vector3(0.0, 0.32, -0.49), "size": Vector3(0.46, 0.32, 0.04),
		  "color": Color(0.08, 0.075, 0.07) },
		{ "position": Vector3(0.0, 0.58, -0.515), "size": Vector3(0.30, 0.08, 0.03),
		  "color": Color(1.0, 0.35, 0.08) },
	]
	var collision: Array[Dictionary] = [
		{ "position": Vector3(0.0, 0.32, 0.0), "size": Vector3(0.96, 0.72, 0.96) },
	]
	return BlockModelResource.create(&"furnace", boxes, [], collision)


# Magic structure model: a placeholder runic pedestal with a floating crystal.
# Rendered as boxes until dedicated magic-structure mesh assets exist; the
# custom_meshes slot is ready to receive an imported mesh path later.
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
	var collision: Array[Dictionary] = [
		{ "position": Vector3(0.0, -0.30, 0.0), "size": Vector3(0.80, 0.20, 0.80) },
		{ "position": Vector3(0.0, 0.05, 0.0), "size": Vector3(0.40, 0.50, 0.40) },
	]
	return BlockModelResource.create(&"magic_structure", boxes, [], collision)
