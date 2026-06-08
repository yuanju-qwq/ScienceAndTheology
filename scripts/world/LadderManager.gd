class_name LadderManager extends Node

const SURFACE_LAYER: StringName = &"surface"
const UNDERGROUND_LAYER: StringName = &"underground"

signal ladder_placed(layer: StringName, cell: Vector2i)
signal ladder_removed(layer: StringName, cell: Vector2i)

var _ladders: Dictionary = {}
var _connector_manager: Node = null
var _next_connector_id := 100

func _ready() -> void:
	_connector_manager = _find_connector_manager()


func _find_connector_manager():
	var node = get_parent()
	while node:
		if node is ConnectorManager:
			return node
		if node.has_method(&"get_node") and node.has_node(&"ConnectorManager"):
			return node.get_node(&"ConnectorManager")
		node = node.get_parent()
	return null


func place_ladder(layer: StringName, cell: Vector2i) -> bool:
	if layer != SURFACE_LAYER and layer != UNDERGROUND_LAYER:
		return false

	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if _ladders.has(key):
		return false

	var other_layer := _get_other_layer(layer)
	var other_key := "%s,%d,%d" % [other_layer, cell.x, cell.y]

	if _ladders.has(other_key):
		return false

	_ladders[key] = true
	_ladders[other_key] = true
	_create_connector(layer, cell)
	ladder_placed.emit(layer, cell)
	ladder_placed.emit(other_layer, cell)
	return true


func remove_ladder(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if not _ladders.has(key):
		return false

	var other_layer := _get_other_layer(layer)
	var other_key := "%s,%d,%d" % [other_layer, cell.x, cell.y]

	_ladders.erase(key)
	_ladders.erase(other_key)
	_remove_connector(layer, cell)
	ladder_removed.emit(layer, cell)
	ladder_removed.emit(other_layer, cell)
	return true


func has_ladder(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	return _ladders.has(key)


func get_other_side(layer: StringName, cell: Vector2i) -> Dictionary:
	var other_layer := _get_other_layer(layer)
	return { "layer": other_layer, "cell": cell }


func clear() -> void:
	_ladders.clear()


func _create_connector(layer: StringName, cell: Vector2i) -> void:
	if _connector_manager == null:
		return
	var other_layer := _get_other_layer(layer)
	var connector := MapConnector.new()
	connector.connector_id = _next_connector_id
	_next_connector_id += 1
	connector.from_layer = layer
	connector.from_cell = cell
	connector.to_layer = other_layer
	connector.to_cell = cell
	connector.connector_type = &"ladder"
	connector.activation_mode = MapConnector.ActivationMode.INTERACT
	connector.one_way = false
	_connector_manager.add_connector(connector)


func _remove_connector(layer: StringName, cell: Vector2i) -> void:
	if _connector_manager == null:
		return
	var connector := _connector_manager.get_connector_at(layer, cell)
	if connector != null:
		_connector_manager.remove_connector(connector.connector_id)


func _get_other_layer(layer: StringName) -> StringName:
	if layer == SURFACE_LAYER:
		return UNDERGROUND_LAYER
	return SURFACE_LAYER
