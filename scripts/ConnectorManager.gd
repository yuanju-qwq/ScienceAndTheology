class_name ConnectorManager
extends Node

const MapConnectorResource := preload("res://scripts/MapConnector.gd")

signal connectors_changed

@export var connectors: Array[MapConnectorResource] = []


func add_connector(connector: MapConnectorResource) -> void:
	if connector == null:
		return

	connectors.append(connector)
	connectors_changed.emit()


func remove_connector(connector_id: StringName) -> void:
	for index in range(connectors.size() - 1, -1, -1):
		var connector := connectors[index]
		if connector != null and connector.connector_id == connector_id:
			connectors.remove_at(index)
			connectors_changed.emit()


func get_connector_at(layer_id: StringName, cell_position: Vector2i) -> MapConnectorResource:
	for connector in connectors:
		if connector != null and connector.is_enterable_from(layer_id, cell_position):
			return connector

	return null


func has_connector_at(layer_id: StringName, cell_position: Vector2i) -> bool:
	return get_connector_at(layer_id, cell_position) != null


func get_connectors_for_layer(layer_id: StringName) -> Array[MapConnectorResource]:
	var layer_connectors: Array[MapConnectorResource] = []

	for connector in connectors:
		if connector == null:
			continue

		if connector.from_layer == layer_id or (not connector.one_way and connector.to_layer == layer_id):
			layer_connectors.append(connector)

	return layer_connectors


func get_target_for(layer_id: StringName, cell_position: Vector2i) -> Dictionary:
	var connector := get_connector_at(layer_id, cell_position)
	if connector == null:
		return {}

	return {
		"connector": connector,
		"layer": connector.get_target_layer_for(layer_id, cell_position),
		"cell": connector.get_target_cell_for(layer_id, cell_position),
	}


func validate_connectors() -> PackedStringArray:
	var issues := PackedStringArray()
	var entrance_keys := {}

	for index in range(connectors.size()):
		var connector := connectors[index]
		if connector == null:
			issues.append("Connector %d is empty." % index)
			continue

		if not connector.has_valid_route():
			issues.append("Connector %d has an invalid route." % index)

		_add_entrance_validation_issue(issues, entrance_keys, index, connector.from_layer, connector.from_cell)
		if not connector.one_way:
			_add_entrance_validation_issue(issues, entrance_keys, index, connector.to_layer, connector.to_cell)

	return issues


func _add_entrance_validation_issue(
		issues: PackedStringArray,
		entrance_keys: Dictionary,
		index: int,
		layer_id: StringName,
		cell_position: Vector2i) -> void:
	var entrance_key := _make_entrance_key(layer_id, cell_position)
	if entrance_keys.has(entrance_key):
		issues.append("Connector %d shares an entrance with connector %d." % [index, entrance_keys[entrance_key]])
	else:
		entrance_keys[entrance_key] = index


func _make_entrance_key(layer_id: StringName, cell_position: Vector2i) -> String:
	return "%s:%d:%d" % [String(layer_id), cell_position.x, cell_position.y]
