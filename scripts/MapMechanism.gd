class_name MapMechanism
extends Resource

const SURFACE: StringName = &"surface"

enum ActivationMode {
	INTERACT,
	AUTO_ON_ENTER,
}

@export var mechanism_id: StringName = &""
@export var layer_id: StringName = SURFACE
@export var cell_position: Vector2i = Vector2i.ZERO
@export var display_name := "Mechanism"
@export var action_label := "Use Mechanism"
@export var flag_name: StringName = &""
@export var activation_mode := ActivationMode.INTERACT
@export var one_shot := true
@export var required_flag: StringName = &""
@export var effects: Array[Dictionary] = []


func is_at(target_layer_id: StringName, target_cell_position: Vector2i) -> bool:
	return layer_id == target_layer_id and cell_position == target_cell_position


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
