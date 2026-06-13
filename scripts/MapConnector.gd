class_name MapConnector
extends Resource

enum ActivationMode {
	INTERACT,
	AUTO_ON_ENTER,
}

@export var connector_id: int = 0
@export var from_layer: StringName = WorldLayers.SURFACE
@export var from_cell: Vector2i = Vector2i.ZERO
@export var to_layer: StringName = WorldLayers.UNDERGROUND
@export var to_cell: Vector2i = Vector2i.ZERO
@export var one_way := false
@export var locked := false
@export var connector_type: StringName = &"cave_entrance"
@export var activation_mode := ActivationMode.INTERACT


func is_enterable_from(layer_id: StringName, cell_position: Vector2i) -> bool:
	if locked:
		return false

	if from_layer == layer_id and from_cell == cell_position:
		return true

	return not one_way and to_layer == layer_id and to_cell == cell_position


func get_target_layer_for(layer_id: StringName, cell_position: Vector2i) -> StringName:
	if from_layer == layer_id and from_cell == cell_position:
		return to_layer

	if not one_way and to_layer == layer_id and to_cell == cell_position:
		return from_layer

	return &""


func get_target_cell_for(layer_id: StringName, cell_position: Vector2i) -> Vector2i:
	if from_layer == layer_id and from_cell == cell_position:
		return to_cell

	if not one_way and to_layer == layer_id and to_cell == cell_position:
		return from_cell

	return cell_position


func has_valid_route() -> bool:
	if from_layer == &"" or to_layer == &"":
		return false

	return from_layer != to_layer or from_cell != to_cell


func requires_interaction() -> bool:
	return activation_mode == ActivationMode.INTERACT


func activates_on_enter() -> bool:
	return activation_mode == ActivationMode.AUTO_ON_ENTER
