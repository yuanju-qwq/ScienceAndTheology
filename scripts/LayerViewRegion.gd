class_name LayerViewRegion
extends Resource

enum ViewMode {
	LOCAL_PEEK,
	DUAL_LAYER,
}

@export var region_id: StringName = &""
@export var trigger_layer: StringName = WorldLayers.SURFACE
@export var center_cell: Vector2i = Vector2i.ZERO
@export var radius := 1
@export var view_mode := ViewMode.LOCAL_PEEK
@export var revealed_layer: StringName = WorldLayers.UNDERGROUND
@export var active_alpha := 1.0
@export var secondary_alpha := 0.42
@export var show_revealed_layer := false
@export var status_suffix := ""
@export var hint_node_paths: Array[NodePath] = []


func contains(layer_id: StringName, cell_position: Vector2i) -> bool:
	if trigger_layer != &"" and trigger_layer != layer_id:
		return false

	return abs(cell_position.x - center_cell.x) <= radius \
			and abs(cell_position.y - center_cell.y) <= radius


func is_dual_layer_view() -> bool:
	return view_mode == ViewMode.DUAL_LAYER


func is_local_peek() -> bool:
	return view_mode == ViewMode.LOCAL_PEEK
