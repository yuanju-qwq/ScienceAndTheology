class_name WorldLayers
extends RefCounted

const SURFACE: StringName = &"surface"
const UNDERGROUND: StringName = &"underground"
const ALL: Array[StringName] = [SURFACE, UNDERGROUND]


static func is_valid_layer(layer_id: StringName) -> bool:
	return layer_id == SURFACE or layer_id == UNDERGROUND


static func other_layer(layer_id: StringName) -> StringName:
	if layer_id == SURFACE:
		return UNDERGROUND
	if layer_id == UNDERGROUND:
		return SURFACE
	return SURFACE


static func display_name(layer_id: StringName) -> String:
	match layer_id:
		SURFACE:
			return "Surface"
		UNDERGROUND:
			return "Underground"
		_:
			return String(layer_id).capitalize()
