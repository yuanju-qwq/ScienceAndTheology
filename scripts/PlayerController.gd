class_name PlayerController
extends CharacterBody2D

signal connector_used(connector_id: StringName, from_layer: StringName, to_layer: StringName)
signal mechanism_activated(mechanism_id: StringName, layer_id: StringName)

@export var move_speed := 96.0
@export var connector_cooldown := 0.25
@export var layer_controller_path: NodePath = ^".."
@export var connector_manager_path: NodePath = ^"../ConnectorManager"
@export var mechanism_manager_path: NodePath = ^"../MechanismManager"
@export var connector_prompt_path: NodePath = ^"../UI/ConnectorPrompt"
@export var connector_prompt_label_path: NodePath = ^"../UI/ConnectorPrompt/Label"
@export var transition_overlay_path: NodePath = ^"../UI/TransitionOverlay"
@export var transition_label_path: NodePath = ^"../UI/TransitionOverlay/Label"
@export var exploration_tracker_path: NodePath = ^"../ExplorationTracker"
@export var transition_fade_in_duration := 0.18
@export var transition_hold_duration := 0.08
@export var transition_fade_out_duration := 0.22

@onready var layer_controller = get_node_or_null(layer_controller_path)
@onready var connector_manager = get_node_or_null(connector_manager_path)
@onready var mechanism_manager = get_node_or_null(mechanism_manager_path)
@onready var connector_prompt: CanvasItem = get_node_or_null(connector_prompt_path) as CanvasItem
@onready var connector_prompt_label: Label = get_node_or_null(connector_prompt_label_path) as Label
@onready var transition_overlay: CanvasItem = get_node_or_null(transition_overlay_path) as CanvasItem
@onready var transition_label: Label = get_node_or_null(transition_label_path) as Label
@onready var exploration_tracker = get_node_or_null(exploration_tracker_path)

var _cooldown_remaining := 0.0
var _last_layer: StringName = &""
var _last_cell := Vector2i.ZERO
var _input_locked := false


func _ready() -> void:
	_prepare_transition_overlay()
	_last_layer = _get_current_layer()
	_last_cell = _get_current_cell()
	_mark_current_cell_visited()
	_update_connector_prompt()


func _physics_process(delta: float) -> void:
	if _cooldown_remaining > 0.0:
		_cooldown_remaining = maxf(_cooldown_remaining - delta, 0.0)

	if _input_locked:
		velocity = Vector2.ZERO
		move_and_slide()
		_update_connector_prompt()
		return

	_handle_movement()
	_try_auto_connector()
	_update_connector_prompt()


func _unhandled_input(event: InputEvent) -> void:
	if _input_locked:
		return

	if _is_interact_event(event) and (_try_use_connector(false) or _try_activate_mechanism(false)):
		get_viewport().set_input_as_handled()


func _handle_movement() -> void:
	var input_vector := Vector2.ZERO

	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		input_vector.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		input_vector.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		input_vector.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		input_vector.y += 1.0

	velocity = input_vector.normalized() * move_speed
	move_and_slide()


func _try_auto_connector() -> void:
	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var entered_new_cell := layer_id != _last_layer or cell_position != _last_cell

	if not entered_new_cell:
		return

	_last_layer = layer_id
	_last_cell = cell_position
	_mark_current_cell_visited()

	if _cooldown_remaining <= 0.0:
		if _try_use_connector(true):
			return

		_try_activate_mechanism(true)


func _try_use_connector(auto_only: bool) -> bool:
	if _input_locked or _cooldown_remaining > 0.0 or connector_manager == null:
		return false

	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var connector = connector_manager.get_connector_at(layer_id, cell_position)
	if connector == null:
		return false

	if auto_only and not connector.activates_on_enter():
		return false

	if not auto_only and not connector.requires_interaction():
		return false

	_start_connector_transition(connector, layer_id, cell_position)
	return true


func _try_activate_mechanism(auto_only: bool) -> bool:
	if _input_locked or mechanism_manager == null:
		return false

	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var mechanism = mechanism_manager.get_mechanism_at(layer_id, cell_position)
	if mechanism == null:
		return false

	if auto_only and not mechanism.activates_on_enter():
		return false

	if not auto_only and not mechanism.requires_interaction():
		return false

	if not mechanism_manager.activate_mechanism(mechanism.mechanism_id):
		return false

	_cooldown_remaining = connector_cooldown
	mechanism_activated.emit(mechanism.mechanism_id, layer_id)
	_update_connector_prompt()
	return true


func _start_connector_transition(connector, layer_id: StringName, cell_position: Vector2i) -> void:
	_set_input_locked(true)
	_run_connector_transition(connector, layer_id, cell_position)


func _run_connector_transition(connector, layer_id: StringName, cell_position: Vector2i) -> void:
	var target_layer: StringName = connector.get_target_layer_for(layer_id, cell_position)
	if target_layer == &"":
		_set_input_locked(false)
		return

	var target_cell: Vector2i = connector.get_target_cell_for(layer_id, cell_position)
	await _play_transition_fade_in(connector)

	if layer_controller != null:
		layer_controller.change_layer(target_layer)

	var target_tile_layer := _get_current_tile_layer()
	if target_tile_layer != null:
		global_position = target_tile_layer.to_global(target_tile_layer.map_to_local(target_cell))

	_last_layer = target_layer
	_last_cell = target_cell
	_mark_current_cell_visited()
	_cooldown_remaining = connector_cooldown
	connector_used.emit(connector.connector_id, layer_id, target_layer)

	if transition_hold_duration > 0.0:
		await get_tree().create_timer(transition_hold_duration).timeout

	await _play_transition_fade_out()
	_set_input_locked(false)


func _get_current_layer() -> StringName:
	if layer_controller == null:
		return &"surface"

	return layer_controller.current_layer


func get_current_layer() -> StringName:
	return _get_current_layer()


func _get_current_cell() -> Vector2i:
	var tile_layer := _get_current_tile_layer()
	if tile_layer == null:
		return Vector2i.ZERO

	return tile_layer.local_to_map(tile_layer.to_local(global_position))


func get_current_cell() -> Vector2i:
	return _get_current_cell()


func _get_current_tile_layer() -> TileMapLayer:
	if layer_controller == null or not layer_controller.has_method("get_current_tile_layer"):
		return null

	return layer_controller.get_current_tile_layer()


func _update_connector_prompt() -> void:
	if connector_prompt == null or connector_prompt_label == null:
		return

	var connector = null
	if connector_manager != null:
		connector = connector_manager.get_connector_at(_get_current_layer(), _get_current_cell())

	var can_interact: bool = connector != null \
			and connector.requires_interaction() \
			and _cooldown_remaining <= 0.0 \
			and not _input_locked

	if can_interact:
		connector_prompt.visible = true
		connector_prompt_label.text = "E  %s" % _format_connector_type(connector.connector_type)
		return

	var mechanism = null
	if mechanism_manager != null:
		mechanism = mechanism_manager.get_mechanism_at(_get_current_layer(), _get_current_cell())

	var can_activate_mechanism: bool = mechanism != null \
			and mechanism.requires_interaction() \
			and not _input_locked
	connector_prompt.visible = can_activate_mechanism

	if can_activate_mechanism:
		connector_prompt_label.text = "E  %s" % _format_mechanism_action(mechanism)


func _format_connector_type(connector_type: StringName) -> String:
	match connector_type:
		&"cave_entrance":
			return "Cave Entrance"
		&"rift":
			return "Rift"
		&"ruin_gate":
			return "Ruin Gate"
		_:
			return String(connector_type).replace("_", " ").capitalize()


func _format_mechanism_action(mechanism) -> String:
	if mechanism.action_label != "":
		return mechanism.action_label

	return mechanism.display_name


func _prepare_transition_overlay() -> void:
	if transition_overlay == null:
		return

	transition_overlay.visible = false
	transition_overlay.modulate.a = 0.0


func _play_transition_fade_in(connector) -> void:
	if transition_overlay == null:
		return

	transition_overlay.visible = true
	transition_overlay.modulate.a = 0.0

	if transition_label != null:
		transition_label.text = _get_transition_text(connector)

	var tween := create_tween()
	tween.tween_property(transition_overlay, "modulate:a", 1.0, transition_fade_in_duration)
	await tween.finished


func _play_transition_fade_out() -> void:
	if transition_overlay == null:
		return

	var tween := create_tween()
	tween.tween_property(transition_overlay, "modulate:a", 0.0, transition_fade_out_duration)
	await tween.finished
	transition_overlay.visible = false


func _get_transition_text(connector) -> String:
	match connector.connector_type:
		&"cave_entrance":
			return "Entering"
		&"rift":
			return "Falling"
		&"ruin_gate":
			return "Passing"
		_:
			return "Moving"


func _mark_current_cell_visited() -> void:
	if exploration_tracker == null or not exploration_tracker.has_method("mark_visited"):
		return

	exploration_tracker.mark_visited(_get_current_layer(), _get_current_cell())


func _set_input_locked(is_locked: bool) -> void:
	_input_locked = is_locked
	if layer_controller != null and layer_controller.has_method("set_input_locked"):
		layer_controller.set_input_locked(is_locked)

	if _input_locked:
		velocity = Vector2.ZERO
	_update_connector_prompt()


func _is_interact_event(event: InputEvent) -> bool:
	return (
		event is InputEventKey
		and event.pressed
		and not event.echo
		and (event.physical_keycode == KEY_E or event.physical_keycode == KEY_SPACE)
	)
