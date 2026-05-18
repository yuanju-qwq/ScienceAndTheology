class_name MechanismManager
extends Node

const MapMechanismResource := preload("res://scripts/MapMechanism.gd")

signal mechanism_activated(mechanism_id: StringName, layer_id: StringName, cell_position: Vector2i)
signal mechanisms_changed
signal world_flags_changed

@export var mechanisms: Array[MapMechanismResource] = []
@export var create_prototype_mechanisms := true
@export var connector_manager_path: NodePath = ^"../ConnectorManager"

@onready var connector_manager = get_node_or_null(connector_manager_path)

var _world_flags := {}


func _ready() -> void:
	if create_prototype_mechanisms and mechanisms.is_empty():
		_create_prototype_mechanisms()

	_apply_world_state()


func add_mechanism(mechanism: MapMechanismResource) -> void:
	if mechanism == null:
		return

	mechanisms.append(mechanism)
	mechanisms_changed.emit()


func get_mechanism_at(layer_id: StringName, cell_position: Vector2i) -> MapMechanismResource:
	for mechanism in mechanisms:
		if mechanism == null:
			continue

		if mechanism.is_at(layer_id, cell_position) and mechanism.is_available(_world_flags):
			return mechanism

	return null


func has_mechanism_at(layer_id: StringName, cell_position: Vector2i) -> bool:
	return get_mechanism_at(layer_id, cell_position) != null


func get_mechanisms_for_layer(layer_id: StringName) -> Array[MapMechanismResource]:
	var layer_mechanisms: Array[MapMechanismResource] = []

	for mechanism in mechanisms:
		if mechanism != null and mechanism.layer_id == layer_id:
			layer_mechanisms.append(mechanism)

	return layer_mechanisms


func activate_mechanism_at(layer_id: StringName, cell_position: Vector2i) -> bool:
	var mechanism := get_mechanism_at(layer_id, cell_position)
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

	mechanism_activated.emit(mechanism.mechanism_id, mechanism.layer_id, mechanism.cell_position)
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
			_set_connector_locked(StringName(str(effect.get("connector_id", ""))), bool(value))
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


func _set_connector_locked(connector_id: StringName, is_locked: bool) -> void:
	if connector_id == &"" or connector_manager == null:
		return

	if connector_manager.has_method("set_connector_locked"):
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


func _create_prototype_mechanisms() -> void:
	add_mechanism(_make_mechanism(
			&"surface_sun_altar_001",
			&"surface",
			Vector2i(-1, -1),
			"Sun Altar",
			"Open Ruin Gate",
			&"underground_gate_open",
			[
				{
					"type": &"node_visible",
					"path": NodePath("../Layers/UndergroundLayer/GeneratedSealedGateArt"),
					"when_active": false,
					"when_inactive": true,
				},
				{
					"type": &"node_visible",
					"path": NodePath("../Layers/UndergroundLayer/GeneratedOpenGateArt"),
					"when_active": true,
					"when_inactive": false,
				},
				{
					"type": &"connector_locked",
					"connector_id": &"underground_ruin_gate_001",
					"when_active": false,
					"when_inactive": true,
				},
			]))

	add_mechanism(_make_mechanism(
			&"underground_pump_001",
			&"underground",
			Vector2i(0, 1),
			"Underground Pump",
			"Raise Surface Bridge",
			&"surface_bridge_raised",
			[
				{
					"type": &"node_visible",
					"path": NodePath("../Layers/SurfaceLayer/GeneratedBrokenBridgeArt"),
					"when_active": false,
					"when_inactive": true,
				},
				{
					"type": &"node_visible",
					"path": NodePath("../Layers/SurfaceLayer/GeneratedBridgeRaisedArt"),
					"when_active": true,
					"when_inactive": false,
				},
			]))


func _make_mechanism(
		mechanism_id: StringName,
		layer_id: StringName,
		cell_position: Vector2i,
		display_name: String,
		action_label: String,
		flag_name: StringName,
		effects: Array) -> MapMechanismResource:
	var mechanism := MapMechanismResource.new()
	mechanism.mechanism_id = mechanism_id
	mechanism.layer_id = layer_id
	mechanism.cell_position = cell_position
	mechanism.display_name = display_name
	mechanism.action_label = action_label
	mechanism.flag_name = flag_name

	var typed_effects: Array[Dictionary] = []
	for effect in effects:
		if effect is Dictionary:
			typed_effects.append(effect)

	mechanism.effects = typed_effects
	return mechanism
