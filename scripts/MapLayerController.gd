extends Node2D

signal layer_changed(old_layer: StringName, new_layer: StringName)

@export var current_layer: StringName = WorldLayers.SURFACE
@export var surface_layer_path: NodePath = ^"Layers/SurfaceLayer"
@export var underground_layer_path: NodePath = ^"Layers/UndergroundLayer"
@export var layer_status_label_path: NodePath = ^"UI/LayerStatusLabel"
@export var enable_debug_layer_toggle := true

@onready var surface_layer: Node = get_node_or_null(surface_layer_path)
@onready var underground_layer: Node = get_node_or_null(underground_layer_path)
@onready var layer_status_label: Label = get_node_or_null(layer_status_label_path) as Label

var input_locked := false
var _layer_view_alphas := {}
var _layer_view_status_suffix := ""
var _base_layer_modulates := {}
var _base_layer_z_indices := {}


func _ready() -> void:
	_capture_base_layer_visuals()
	_apply_layer_state()


func _unhandled_input(event: InputEvent) -> void:
	if input_locked:
		return

	if not enable_debug_layer_toggle:
		return

	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_TAB:
		toggle_layer()
		get_viewport().set_input_as_handled()


func change_layer(target_layer: StringName) -> void:
	if target_layer == current_layer:
		return

	if not WorldLayers.is_valid_layer(target_layer):
		push_warning("Unknown map layer: %s" % target_layer)
		return

	var old_layer := current_layer
	current_layer = target_layer
	_apply_layer_state()
	layer_changed.emit(old_layer, current_layer)


func toggle_layer() -> void:
	change_layer(WorldLayers.other_layer(current_layer))


func is_current_layer(layer_id: StringName) -> bool:
	return current_layer == layer_id


func set_input_locked(is_locked: bool) -> void:
	input_locked = is_locked


func set_layer_view_context(layer_alphas: Dictionary, status_suffix := "") -> void:
	_layer_view_alphas = layer_alphas.duplicate(true)
	_layer_view_status_suffix = status_suffix
	_apply_layer_state()


func clear_layer_view_context() -> void:
	if _layer_view_alphas.is_empty() and _layer_view_status_suffix == "":
		return

	_layer_view_alphas.clear()
	_layer_view_status_suffix = ""
	_apply_layer_state()


func get_current_layer_node() -> Node:
	return get_layer_node(current_layer)


func get_current_tile_layer() -> TileMapLayer:
	return get_current_layer_node() as TileMapLayer


func get_layer_node(layer_id: StringName) -> Node:
	match layer_id:
		WorldLayers.SURFACE:
			return surface_layer
		WorldLayers.UNDERGROUND:
			return underground_layer
		_:
			return null


func _apply_layer_state() -> void:
	_apply_layer_activity(surface_layer, WorldLayers.SURFACE)
	_apply_layer_activity(underground_layer, WorldLayers.UNDERGROUND)
	_update_layer_status_label()


func _apply_layer_activity(layer: Node, layer_id: StringName) -> void:
	if layer == null:
		return

	var is_active := current_layer == layer_id
	var visual_alpha := _get_layer_visual_alpha(layer_id, is_active)
	_set_layer_visual(layer, layer_id, visual_alpha, is_active)

	_set_bool_property_if_present(layer, &"enabled", is_active)
	_set_bool_property_if_present(layer, &"collision_enabled", is_active)
	_set_bool_property_if_present(layer, &"navigation_enabled", is_active)

	layer.process_mode = Node.PROCESS_MODE_INHERIT if is_active else Node.PROCESS_MODE_DISABLED


func _update_layer_status_label() -> void:
	if layer_status_label == null:
		return

	layer_status_label.text = "Layer: %s" % _get_layer_display_name(current_layer)
	if _layer_view_status_suffix != "":
		layer_status_label.text += _layer_view_status_suffix


func _capture_base_layer_visuals() -> void:
	_capture_layer_visual(WorldLayers.SURFACE, surface_layer)
	_capture_layer_visual(WorldLayers.UNDERGROUND, underground_layer)


func _capture_layer_visual(layer_id: StringName, layer: Node) -> void:
	if layer == null:
		return

	if layer is CanvasItem:
		var canvas_item := layer as CanvasItem
		_base_layer_modulates[String(layer_id)] = canvas_item.modulate
		_base_layer_z_indices[String(layer_id)] = canvas_item.z_index


func _get_layer_visual_alpha(layer_id: StringName, is_active: bool) -> float:
	var layer_key := String(layer_id)
	if _layer_view_alphas.has(layer_key):
		return clampf(float(_layer_view_alphas[layer_key]), 0.0, 1.0)

	if is_active:
		return 1.0

	return 0.0


func _set_layer_visual(layer: Node, layer_id: StringName, alpha: float, is_active: bool) -> void:
	var is_visible := alpha > 0.0
	layer.visible = is_visible

	if not (layer is CanvasItem):
		return

	var canvas_item := layer as CanvasItem
	var layer_key := String(layer_id)
	var base_modulate := Color.WHITE
	if _base_layer_modulates.has(layer_key):
		base_modulate = _base_layer_modulates[layer_key]

	base_modulate.a *= alpha
	canvas_item.modulate = base_modulate

	var base_z_index := 0
	if _base_layer_z_indices.has(layer_key):
		base_z_index = int(_base_layer_z_indices[layer_key])

	canvas_item.z_index = base_z_index if is_active else base_z_index + 8


func _get_layer_display_name(layer_id: StringName) -> String:
	return WorldLayers.display_name(layer_id)


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
