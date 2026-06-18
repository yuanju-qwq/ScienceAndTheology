# MapConnector — a link between two 3D cell positions within or across dimensions.
# Uses dimension + Vector3i for both endpoints.
class_name MapConnector
extends Resource

const OVERWORLD: StringName = &"overworld"

enum ActivationMode {
	INTERACT,
	AUTO_ON_ENTER,
}

@export var connector_id: int = 0
@export var from_dimension: StringName = OVERWORLD
@export var from_cell: Vector3i = Vector3i.ZERO
@export var to_dimension: StringName = OVERWORLD
@export var to_cell: Vector3i = Vector3i.ZERO
@export var one_way := false
@export var locked := false
@export var connector_type: StringName = &"cave_entrance"
@export var activation_mode := ActivationMode.INTERACT


func is_enterable_from(dimension: StringName, cell_position: Vector3i) -> bool:
	if locked:
		return false

	if from_dimension == dimension and from_cell == cell_position:
		return true

	return not one_way and to_dimension == dimension and to_cell == cell_position


func get_target_dimension_for(dimension: StringName, cell_position: Vector3i) -> StringName:
	if from_dimension == dimension and from_cell == cell_position:
		return to_dimension

	if not one_way and to_dimension == dimension and to_cell == cell_position:
		return from_dimension

	return &""


func get_target_cell_for(dimension: StringName, cell_position: Vector3i) -> Vector3i:
	if from_dimension == dimension and from_cell == cell_position:
		return to_cell

	if not one_way and to_dimension == dimension and to_cell == cell_position:
		return from_cell

	return cell_position


func has_valid_route() -> bool:
	if from_dimension == &"" or to_dimension == &"":
		return false

	return from_dimension != to_dimension or from_cell != to_cell


func requires_interaction() -> bool:
	return activation_mode == ActivationMode.INTERACT


func activates_on_enter() -> bool:
	return activation_mode == ActivationMode.AUTO_ON_ENTER
