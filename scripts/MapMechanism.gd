# MapMechanism — a world mechanism at a 3D cell position within a dimension.
# Replaces the old 2D layer_id + Vector2i model with dimension + Vector3i.
class_name MapMechanism
extends Resource

const OVERWORLD: StringName = &"overworld"

enum ActivationMode {
	INTERACT,
	AUTO_ON_ENTER,
}

@export var mechanism_id: StringName = &""
@export var dimension: StringName = OVERWORLD
@export var cell_position: Vector3i = Vector3i.ZERO
@export var title_key := "ui.mechanism"
@export var action_label := "Use Mechanism"
@export var flag_name: StringName = &""
@export var activation_mode := ActivationMode.INTERACT
@export var one_shot := true
@export var required_flag: StringName = &""
@export var effects: Array[Dictionary] = []


func is_at(target_dimension: StringName, target_cell_position: Vector3i) -> bool:
	return dimension == target_dimension and cell_position == target_cell_position


func is_available(world_flags: Dictionary) -> bool:
	if required_flag != &"" and not bool(world_flags.get(String(required_flag), false)):
		return false

	if one_shot and flag_name != &"" and bool(world_flags.get(String(flag_name), false)):
		return false

	return true


func requires_interaction() -> bool:
	return activation_mode == ActivationMode.INTERACT


func activates_on_enter() -> bool:
	return activation_mode == ActivationMode.AUTO_ON_ENTER
