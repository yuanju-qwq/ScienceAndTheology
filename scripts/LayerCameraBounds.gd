class_name LayerCameraBounds
extends Camera2D

const SURFACE_LAYER: StringName = &"surface"
const UNDERGROUND_LAYER: StringName = &"underground"

@export var layer_controller_path: NodePath = ^"../.."
@export var surface_bounds := Rect2i(-320, -192, 640, 384)
@export var underground_bounds := Rect2i(-240, -144, 480, 288)

@onready var layer_controller = get_node_or_null(layer_controller_path)


func _ready() -> void:
	if layer_controller != null and layer_controller.has_signal("layer_changed"):
		layer_controller.layer_changed.connect(_on_layer_changed)

	_apply_bounds_for_layer(_get_current_layer())


func _on_layer_changed(_old_layer: StringName, new_layer: StringName) -> void:
	_apply_bounds_for_layer(new_layer)


func _get_current_layer() -> StringName:
	if layer_controller == null:
		return SURFACE_LAYER

	return layer_controller.current_layer


func _apply_bounds_for_layer(layer_id: StringName) -> void:
	match layer_id:
		SURFACE_LAYER:
			_apply_bounds(surface_bounds)
		UNDERGROUND_LAYER:
			_apply_bounds(underground_bounds)
		_:
			_clear_bounds()


func _apply_bounds(bounds: Rect2i) -> void:
	limit_left = bounds.position.x
	limit_top = bounds.position.y
	limit_right = bounds.position.x + bounds.size.x
	limit_bottom = bounds.position.y + bounds.size.y

	force_update_scroll()


func _clear_bounds() -> void:
	limit_left = -10000000
	limit_top = -10000000
	limit_right = 10000000
	limit_bottom = 10000000

	force_update_scroll()
