class_name MiniMap
extends Control

const SURFACE_LAYER: StringName = &"surface"
const UNDERGROUND_LAYER: StringName = &"underground"

@export var layer_controller_path: NodePath = ^"../.."
@export var exploration_tracker_path: NodePath = ^"../../ExplorationTracker"
@export var connector_manager_path: NodePath = ^"../../ConnectorManager"
@export var mechanism_manager_path: NodePath = ^"../../MechanismManager"
@export var player_path: NodePath = ^"../../Player"
@export var title_label_path: NodePath = ^"TitleLabel"
@export var cell_size := 6.0
@export var max_draw_distance := 12

@onready var layer_controller = get_node_or_null(layer_controller_path)
@onready var exploration_tracker = get_node_or_null(exploration_tracker_path)
@onready var connector_manager = get_node_or_null(connector_manager_path)
@onready var mechanism_manager = get_node_or_null(mechanism_manager_path)
@onready var player = get_node_or_null(player_path)
@onready var title_label: Label = get_node_or_null(title_label_path) as Label

var _last_player_cell := Vector2i(1000000, 1000000)
var _last_layer: StringName = &""


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE

	if layer_controller != null and layer_controller.has_signal("layer_changed"):
		layer_controller.layer_changed.connect(_on_layer_changed)

	if exploration_tracker != null and exploration_tracker.has_signal("exploration_changed"):
		exploration_tracker.exploration_changed.connect(_on_exploration_changed)

	if connector_manager != null and connector_manager.has_signal("connectors_changed"):
		connector_manager.connectors_changed.connect(_on_connectors_changed)

	if mechanism_manager != null and mechanism_manager.has_signal("mechanisms_changed"):
		mechanism_manager.mechanisms_changed.connect(_on_mechanisms_changed)

	if mechanism_manager != null and mechanism_manager.has_signal("world_flags_changed"):
		mechanism_manager.world_flags_changed.connect(_on_world_flags_changed)

	_last_layer = _get_current_layer()
	_last_player_cell = _get_player_cell()
	_update_title()
	queue_redraw()


func _process(_delta: float) -> void:
	var current_layer := _get_current_layer()
	var player_cell := _get_player_cell()
	if current_layer == _last_layer and player_cell == _last_player_cell:
		return

	_last_layer = current_layer
	_last_player_cell = player_cell
	_update_title()
	queue_redraw()


func _draw() -> void:
	var panel_rect := Rect2(Vector2.ZERO, size)
	draw_rect(panel_rect, Color(0.025, 0.03, 0.035, 0.82), true)
	draw_rect(panel_rect, Color(0.5, 0.56, 0.58, 0.5), false, 1.0)

	var current_layer := _get_current_layer()
	var player_cell := _get_player_cell()
	_draw_visited_cells(current_layer, player_cell)
	_draw_connectors(current_layer, player_cell)
	_draw_mechanisms(current_layer, player_cell)
	_draw_player_marker()


func _draw_visited_cells(layer_id: StringName, player_cell: Vector2i) -> void:
	if exploration_tracker == null:
		return

	for cell_position in exploration_tracker.get_visited_cells(layer_id):
		if _is_too_far(cell_position, player_cell):
			continue

		var cell_rect := Rect2(_cell_to_minimap_position(cell_position, player_cell), Vector2(cell_size, cell_size))
		draw_rect(cell_rect, _get_layer_cell_color(layer_id), true)


func _draw_connectors(layer_id: StringName, player_cell: Vector2i) -> void:
	if connector_manager == null or exploration_tracker == null:
		return

	for connector in connector_manager.get_connectors_for_layer(layer_id):
		var connector_cell = _get_connector_cell_for_layer(connector, layer_id)
		if connector_cell == null:
			continue

		if not exploration_tracker.has_visited(layer_id, connector_cell):
			continue

		if _is_too_far(connector_cell, player_cell):
			continue

		draw_circle(
				_cell_to_minimap_position(connector_cell, player_cell) + Vector2(cell_size, cell_size) * 0.5,
				maxf(cell_size * 0.42, 2.0),
				_get_connector_color(connector))


func _draw_mechanisms(layer_id: StringName, player_cell: Vector2i) -> void:
	if mechanism_manager == null or exploration_tracker == null:
		return

	for mechanism in mechanism_manager.get_mechanisms_for_layer(layer_id):
		if not exploration_tracker.has_visited(layer_id, mechanism.cell_position):
			continue

		if _is_too_far(mechanism.cell_position, player_cell):
			continue

		var center := _cell_to_minimap_position(mechanism.cell_position, player_cell) \
				+ Vector2(cell_size, cell_size) * 0.5
		var radius := maxf(cell_size * 0.45, 2.0)
		var points := PackedVector2Array([
			center + Vector2(0.0, -radius),
			center + Vector2(radius, 0.0),
			center + Vector2(0.0, radius),
			center + Vector2(-radius, 0.0),
		])
		draw_colored_polygon(points, _get_mechanism_color(mechanism))


func _draw_player_marker() -> void:
	var center := size * 0.5
	var radius := maxf(cell_size * 0.55, 3.0)
	var points := PackedVector2Array([
		center + Vector2(0.0, -radius),
		center + Vector2(radius, 0.0),
		center + Vector2(0.0, radius),
		center + Vector2(-radius, 0.0),
	])
	draw_colored_polygon(points, Color(0.95, 0.96, 1.0, 1.0))


func _on_layer_changed(_old_layer: StringName, new_layer: StringName) -> void:
	_last_layer = new_layer
	_update_title()
	queue_redraw()


func _on_exploration_changed() -> void:
	_update_title()
	queue_redraw()


func _on_connectors_changed() -> void:
	queue_redraw()


func _on_mechanisms_changed() -> void:
	queue_redraw()


func _on_world_flags_changed() -> void:
	queue_redraw()


func _get_current_layer() -> StringName:
	if layer_controller == null:
		return SURFACE_LAYER

	return layer_controller.current_layer


func _get_player_cell() -> Vector2i:
	if player == null or not player.has_method("get_current_cell"):
		return Vector2i.ZERO

	return player.get_current_cell()


func _cell_to_minimap_position(cell_position: Vector2i, player_cell: Vector2i) -> Vector2:
	var offset := Vector2(cell_position - player_cell) * cell_size
	return size * 0.5 + offset - Vector2(cell_size, cell_size) * 0.5


func _is_too_far(cell_position: Vector2i, player_cell: Vector2i) -> bool:
	return abs(cell_position.x - player_cell.x) > max_draw_distance \
			or abs(cell_position.y - player_cell.y) > max_draw_distance


func _get_connector_cell_for_layer(connector, layer_id: StringName):
	if connector.from_layer == layer_id:
		return connector.from_cell

	if not connector.one_way and connector.to_layer == layer_id:
		return connector.to_cell

	return null


func _get_layer_cell_color(layer_id: StringName) -> Color:
	match layer_id:
		SURFACE_LAYER:
			return Color(0.2, 0.56, 0.34, 0.95)
		UNDERGROUND_LAYER:
			return Color(0.42, 0.34, 0.55, 0.95)
		_:
			return Color(0.45, 0.45, 0.45, 0.95)


func _get_connector_color(connector) -> Color:
	if connector.locked:
		return Color(0.35, 0.36, 0.38, 0.85)

	match connector.connector_type:
		&"cave_entrance":
			return Color(0.96, 0.78, 0.38, 1.0)
		&"rift":
			return Color(0.92, 0.22, 0.28, 1.0)
		&"ruin_gate":
			return Color(0.72, 0.9, 1.0, 1.0)
		_:
			return Color(0.9, 0.9, 0.9, 1.0)


func _get_mechanism_color(mechanism) -> Color:
	if mechanism_manager != null \
			and mechanism.flag_name != &"" \
			and mechanism_manager.is_world_flag_active(mechanism.flag_name):
		return Color(0.38, 0.78, 0.92, 1.0)

	return Color(1.0, 0.82, 0.22, 1.0)


func _update_title() -> void:
	if title_label == null:
		return

	var layer_id := _get_current_layer()
	var visited_count := 0
	if exploration_tracker != null:
		visited_count = exploration_tracker.get_visited_count(layer_id)

	title_label.text = "%s  %d" % [_get_layer_display_name(layer_id), visited_count]


func _get_layer_display_name(layer_id: StringName) -> String:
	match layer_id:
		SURFACE_LAYER:
			return "Surface"
		UNDERGROUND_LAYER:
			return "Underground"
		_:
			return String(layer_id).capitalize()
