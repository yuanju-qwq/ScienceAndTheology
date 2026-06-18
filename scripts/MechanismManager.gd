# MechanismManager — manages MapMechanism resources using 3D cell coordinates.
# All lookups use dimension + Vector3i instead of the old layer_id + Vector2i.
class_name MechanismManager
extends Node

const MapMechanismResource := preload("res://scripts/MapMechanism.gd")

signal mechanism_activated(mechanism_id: StringName, dimension: StringName, cell_position: Vector3i)
signal mechanisms_changed
signal world_flags_changed

@export var mechanisms: Array[MapMechanismResource] = []
@export var connector_manager_path: NodePath = ^"../ConnectorManager"

@onready var connector_manager: ConnectorManager = get_node_or_null(connector_manager_path) as ConnectorManager

var _world_flags := {}


func _ready() -> void:
	_apply_world_state()


func add_mechanism(mechanism: MapMechanismResource) -> void:
	if mechanism == null:
		return

	mechanisms.append(mechanism)
	mechanisms_changed.emit()


func add_generated_mechanisms(mechanism_data: Array) -> int:
	var added := 0

	for entry in mechanism_data:
		if not (entry is Dictionary):
			continue

		var mechanism := _make_mechanism_from_dict(entry)
		if mechanism == null or _has_mechanism_id(mechanism.mechanism_id):
			continue

		mechanisms.append(mechanism)
		added += 1

	if added > 0:
		_apply_world_state()
		mechanisms_changed.emit()

	return added


func get_mechanism_at(dimension: StringName, cell_position: Vector3i) -> MapMechanismResource:
	for mechanism in mechanisms:
		if mechanism == null:
			continue

		if mechanism.is_at(dimension, cell_position) and mechanism.is_available(_world_flags):
			return mechanism

	return null


func has_mechanism_at(dimension: StringName, cell_position: Vector3i) -> bool:
	return get_mechanism_at(dimension, cell_position) != null


func get_mechanisms_for_dimension(dimension: StringName) -> Array[MapMechanismResource]:
	var dim_mechanisms: Array[MapMechanismResource] = []

	for mechanism in mechanisms:
		if mechanism != null and mechanism.dimension == dimension:
			dim_mechanisms.append(mechanism)

	return dim_mechanisms


func activate_mechanism_at(dimension: StringName, cell_position: Vector3i) -> bool:
	var mechanism := get_mechanism_at(dimension, cell_position)
	if mechanism == null:
		return false

	return activate_mechanism(mechanism.mechanism_id)


func activate_mechanism(mechanism_id: StringName) -> bool:
	var mechanism := get_mechanism(mechanism_id)
	if mechanism == null or not mechanism.is_available(_world_flags):
		return false

	if mechanism.flag_name != &"":
		set_world_flag(mechanism.flag_name, true)
	else:
		_apply_effects_for_mechanism(mechanism, true)
		world_flags_changed.emit()

	mechanism_activated.emit(mechanism.mechanism_id, mechanism.dimension, mechanism.cell_position)
	mechanisms_changed.emit()
	return true


func get_mechanism(mechanism_id: StringName) -> MapMechanismResource:
	for mechanism in mechanisms:
		if mechanism != null and mechanism.mechanism_id == mechanism_id:
			return mechanism

	return null


func set_world_flag(flag_name: StringName, is_active: bool) -> void:
	if flag_name == &"":
		return

	var flag_key := String(flag_name)
	var was_active := bool(_world_flags.get(flag_key, false))
	_world_flags[flag_key] = is_active

	_apply_world_state()

	if was_active != is_active:
		world_flags_changed.emit()


func is_world_flag_active(flag_name: StringName) -> bool:
	if flag_name == &"":
		return false

	return bool(_world_flags.get(String(flag_name), false))


func get_save_data() -> Dictionary:
	return _world_flags.duplicate(true)


func load_save_data(save_data: Dictionary) -> void:
	_world_flags.clear()

	for flag_key in save_data.keys():
		_world_flags[String(flag_key)] = bool(save_data[flag_key])

	_apply_world_state()
	world_flags_changed.emit()
	mechanisms_changed.emit()


func _apply_world_state() -> void:
	for mechanism in mechanisms:
		if mechanism == null:
			continue

		_apply_effects_for_mechanism(mechanism, is_world_flag_active(mechanism.flag_name))


func _apply_effects_for_mechanism(mechanism: MapMechanismResource, is_active: bool) -> void:
	for effect in mechanism.effects:
		_apply_effect(effect, _get_effect_value(effect, is_active))


func _get_effect_value(effect: Dictionary, is_active: bool) -> Variant:
	if is_active:
		return effect.get("when_active", true)

	return effect.get("when_inactive", false)


func _apply_effect(effect: Dictionary, value: Variant) -> void:
	var effect_type := StringName(str(effect.get("type", "")))

	match effect_type:
		&"node_visible":
			_set_node_visible(_get_effect_node_path(effect), bool(value))
		&"connector_locked":
			_set_connector_locked(int(effect.get("connector_id", 0)), bool(value))
		_:
			push_warning("Unknown mechanism effect: %s" % String(effect_type))


func _get_effect_node_path(effect: Dictionary) -> NodePath:
	var raw_path: Variant = effect.get("path", NodePath(""))
	if raw_path is NodePath:
		return raw_path

	return NodePath(str(raw_path))


func _set_node_visible(node_path: NodePath, is_visible: bool) -> void:
	if node_path.is_empty():
		return

	var target := get_node_or_null(node_path)
	if target == null:
		push_warning("Mechanism effect target not found: %s" % String(node_path))
		return

	if target is CanvasItem:
		(target as CanvasItem).visible = is_visible
	else:
		_set_bool_property_if_present(target, &"visible", is_visible)


func _set_connector_locked(connector_id: int, is_locked: bool) -> void:
	if connector_id == 0 or connector_manager == null:
		return
	connector_manager.set_connector_locked(connector_id, is_locked)


func _set_bool_property_if_present(object: Object, property_name: StringName, value: bool) -> void:
	if _has_property(object, property_name):
		object.set(property_name, value)


func _has_property(object: Object, property_name: StringName) -> bool:
	if object == null:
		return false

	for property in object.get_property_list():
		if property.name == property_name:
			return true

	return false


func _has_mechanism_id(mechanism_id: StringName) -> bool:
	if mechanism_id == &"":
		return false

	for mechanism in mechanisms:
		if mechanism != null and mechanism.mechanism_id == mechanism_id:
			return true

	return false


func _make_mechanism_from_dict(data: Dictionary) -> MapMechanismResource:
	var mechanism := MapMechanismResource.new()
	mechanism.mechanism_id = StringName(str(data.get("mechanism_id", "")))
	mechanism.dimension = StringName(str(data.get("dimension", "")))
	mechanism.cell_position = Vector3i(
			int(data.get("cell_x", 0)),
			int(data.get("cell_y", 0)),
			int(data.get("cell_z", 0)))
	mechanism.title_key = str(data.get("title_key", "ui.mechanism"))
	mechanism.action_label = str(data.get("action_label", "Use Mechanism"))
	mechanism.flag_name = StringName(str(data.get("flag_name", "")))
	mechanism.activation_mode = int(data.get("activation_mode", MapMechanismResource.ActivationMode.INTERACT))
	mechanism.one_shot = bool(data.get("one_shot", true))
	mechanism.required_flag = StringName(str(data.get("required_flag", "")))

	var typed_effects: Array[Dictionary] = []
	var effects: Array = data.get("effects", [])
	for effect in effects:
		if effect is Dictionary:
			typed_effects.append(effect)
	mechanism.effects = typed_effects

	if mechanism.mechanism_id == &"" or mechanism.dimension == &"":
		return null

	return mechanism
