extends Node2D

signal layer_changed(old_layer: StringName, new_layer: StringName)

const SURFACE_LAYER: StringName = &"surface"
const UNDERGROUND_LAYER: StringName = &"underground"

@export var current_layer: StringName = SURFACE_LAYER
@export var surface_layer_path: NodePath = ^"Layers/SurfaceLayer"
@export var underground_layer_path: NodePath = ^"Layers/UndergroundLayer"
@export var layer_status_label_path: NodePath = ^"UI/LayerStatusLabel"
@export var enable_debug_layer_toggle := true

@onready var surface_layer: Node = get_node_or_null(surface_layer_path)
@onready var underground_layer: Node = get_node_or_null(underground_layer_path)
@onready var layer_status_label: Label = get_node_or_null(layer_status_label_path) as Label


func _ready() -> void:
	_apply_layer_state()


func _unhandled_input(event: InputEvent) -> void:
	if not enable_debug_layer_toggle:
		return

	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_TAB:
		toggle_layer()
		get_viewport().set_input_as_handled()


func change_layer(target_layer: StringName) -> void:
	if target_layer == current_layer:
		return

	if target_layer != SURFACE_LAYER and target_layer != UNDERGROUND_LAYER:
		push_warning("Unknown map layer: %s" % target_layer)
		return

	var old_layer := current_layer
	current_layer = target_layer
	_apply_layer_state()
	layer_changed.emit(old_layer, current_layer)


func toggle_layer() -> void:
	if current_layer == SURFACE_LAYER:
		change_layer(UNDERGROUND_LAYER)
	else:
		change_layer(SURFACE_LAYER)


func is_current_layer(layer_id: StringName) -> bool:
	return current_layer == layer_id


func _apply_layer_state() -> void:
	_set_layer_active(surface_layer, current_layer == SURFACE_LAYER)
	_set_layer_active(underground_layer, current_layer == UNDERGROUND_LAYER)
	_update_layer_status_label()


func _set_layer_active(layer: Node, is_active: bool) -> void:
	if layer == null:
		return

	layer.visible = is_active

	_set_bool_property_if_present(layer, &"enabled", is_active)
	_set_bool_property_if_present(layer, &"collision_enabled", is_active)
	_set_bool_property_if_present(layer, &"navigation_enabled", is_active)

	layer.process_mode = Node.PROCESS_MODE_INHERIT if is_active else Node.PROCESS_MODE_DISABLED


func _update_layer_status_label() -> void:
	if layer_status_label == null:
		return

	layer_status_label.text = "Layer: %s" % _get_layer_display_name(current_layer)


func _get_layer_display_name(layer_id: StringName) -> String:
	match layer_id:
		SURFACE_LAYER:
			return "Surface"
		UNDERGROUND_LAYER:
			return "Underground"
		_:
			return String(layer_id)


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
