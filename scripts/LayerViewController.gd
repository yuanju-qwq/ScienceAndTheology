class_name LayerViewController
extends Node

const LayerViewRegionResource := preload("res://scripts/LayerViewRegion.gd")

const SURFACE_LAYER: StringName = &"surface"
const UNDERGROUND_LAYER: StringName = &"underground"

@export var layer_controller_path: NodePath = ^".."
@export var player_path: NodePath = ^"../Player"
@export var create_prototype_regions := true
@export var view_regions: Array[LayerViewRegionResource] = []

@onready var layer_controller = get_node_or_null(layer_controller_path)
@onready var player = get_node_or_null(player_path)

var _last_layer: StringName = &""
var _last_cell := Vector2i(999999, 999999)
var _last_region_id: StringName = &"__unset"


func _ready() -> void:
	if create_prototype_regions and view_regions.is_empty():
		_create_prototype_regions()

	_set_all_hints_visible(false)
	call_deferred("_update_view_context", true)


func _process(_delta: float) -> void:
	_update_view_context(false)


func _update_view_context(force_update: bool) -> void:
	var current_layer := _get_current_layer()
	var player_cell := _get_player_cell()
	var region := _get_active_region(current_layer, player_cell)
	var region_id: StringName = &""
	if region != null:
		region_id = region.region_id

	if not force_update \
			and current_layer == _last_layer \
			and player_cell == _last_cell \
			and region_id == _last_region_id:
		return

	_last_layer = current_layer
	_last_cell = player_cell
	_last_region_id = region_id

	_set_all_hints_visible(false)

	if layer_controller == null:
		return

	if region == null:
		_clear_layer_context()
		return

	_set_region_hints_visible(region, true)
	_apply_region_context(region, current_layer)


func _apply_region_context(region: LayerViewRegionResource, current_layer: StringName) -> void:
	var layer_alphas := {}
	var status_suffix := region.status_suffix

	if region.is_dual_layer_view():
		layer_alphas[String(current_layer)] = region.active_alpha
		layer_alphas[String(_get_other_layer(current_layer))] = region.secondary_alpha
	elif region.show_revealed_layer and region.revealed_layer != &"" and region.revealed_layer != current_layer:
		layer_alphas[String(region.revealed_layer)] = region.secondary_alpha

	if layer_controller.has_method("set_layer_view_context"):
		layer_controller.set_layer_view_context(layer_alphas, status_suffix)


func _clear_layer_context() -> void:
	if layer_controller.has_method("clear_layer_view_context"):
		layer_controller.clear_layer_view_context()


func _get_active_region(layer_id: StringName, cell_position: Vector2i) -> LayerViewRegionResource:
	var local_region: LayerViewRegionResource = null

	for region in view_regions:
		if region == null or not region.contains(layer_id, cell_position):
			continue

		if region.is_dual_layer_view():
			return region

		if local_region == null:
			local_region = region

	return local_region


func _set_all_hints_visible(is_visible: bool) -> void:
	for region in view_regions:
		if region != null:
			_set_region_hints_visible(region, is_visible)


func _set_region_hints_visible(region: LayerViewRegionResource, is_visible: bool) -> void:
	for hint_path in region.hint_node_paths:
		_set_node_visible(hint_path, is_visible)


func _set_node_visible(node_path: NodePath, is_visible: bool) -> void:
	if node_path.is_empty():
		return

	var target := get_node_or_null(node_path)
	if target == null:
		return

	if target is CanvasItem:
		(target as CanvasItem).visible = is_visible
	else:
		_set_bool_property_if_present(target, &"visible", is_visible)


func _get_current_layer() -> StringName:
	if layer_controller == null:
		return SURFACE_LAYER

	return layer_controller.current_layer


func _get_player_cell() -> Vector2i:
	if player == null or not player.has_method("get_current_cell"):
		return Vector2i.ZERO

	return player.get_current_cell()


func _get_other_layer(layer_id: StringName) -> StringName:
	if layer_id == SURFACE_LAYER:
		return UNDERGROUND_LAYER

	return SURFACE_LAYER


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


func _create_prototype_regions() -> void:
	var surface_cave_peek := _make_region(
			&"surface_cave_mouth_peek",
			SURFACE_LAYER,
			Vector2i(2, -1),
			2,
			LayerViewRegionResource.ViewMode.LOCAL_PEEK,
			UNDERGROUND_LAYER,
			"  Below",
			[])
	surface_cave_peek.show_revealed_layer = false
	add_region(surface_cave_peek)

	var underground_light_peek := _make_region(
			&"underground_skylight_peek",
			UNDERGROUND_LAYER,
			Vector2i(2, 0),
			2,
			LayerViewRegionResource.ViewMode.LOCAL_PEEK,
			SURFACE_LAYER,
			"  Above",
			[])
	underground_light_peek.show_revealed_layer = false
	add_region(underground_light_peek)

	var rift_dual_view := _make_region(
			&"rift_dual_layer_view",
			&"",
			Vector2i(-3, 1),
			2,
			LayerViewRegionResource.ViewMode.DUAL_LAYER,
			&"",
			"  Dual",
			[])
	rift_dual_view.active_alpha = 0.88
	rift_dual_view.secondary_alpha = 0.52
	add_region(rift_dual_view)


func add_region(region: LayerViewRegionResource) -> void:
	if region == null:
		return

	view_regions.append(region)


func _make_region(
		region_id: StringName,
		trigger_layer: StringName,
		center_cell: Vector2i,
		radius: int,
		view_mode: int,
		revealed_layer: StringName,
		status_suffix: String,
		hint_node_paths: Array) -> LayerViewRegionResource:
	var region := LayerViewRegionResource.new()
	region.region_id = region_id
	region.trigger_layer = trigger_layer
	region.center_cell = center_cell
	region.radius = radius
	region.view_mode = view_mode
	region.revealed_layer = revealed_layer
	region.status_suffix = status_suffix

	var typed_hint_paths: Array[NodePath] = []
	for hint_path in hint_node_paths:
		if hint_path is NodePath:
			typed_hint_paths.append(hint_path)
		else:
			typed_hint_paths.append(NodePath(str(hint_path)))

	region.hint_node_paths = typed_hint_paths
	return region
