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


func add_generated_connectors(connector_data: Array) -> int:
	var added := 0

	for entry in connector_data:
		if not (entry is Dictionary):
			continue

		var connector := _make_connector_from_dict(entry)
		if connector == null or _has_connector_id(connector.connector_id):
			continue

		connectors.append(connector)
		added += 1

	if added > 0:
		connectors_changed.emit()

	return added


func remove_connector(connector_id: int) -> void:
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


func set_connector_locked(connector_id: int, is_locked: bool) -> void:
	var changed := false

	for connector in connectors:
		if connector != null and connector.connector_id == connector_id and connector.locked != is_locked:
			connector.locked = is_locked
			changed = true

	if changed:
		connectors_changed.emit()


func is_connector_locked(connector_id: int) -> bool:
	for connector in connectors:
		if connector != null and connector.connector_id == connector_id:
			return connector.locked

	return false


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


func _has_connector_id(connector_id: int) -> bool:
	if connector_id == 0:
		return false

	for connector in connectors:
		if connector != null and connector.connector_id == connector_id:
			return true

	return false


func _make_connector_from_dict(data: Dictionary) -> MapConnectorResource:
	var connector := MapConnectorResource.new()
	connector.connector_id = int(data.get("connector_id", 0))
	connector.from_layer = StringName(str(data.get("from_layer", "")))
	connector.from_cell = Vector2i(
			int(data.get("from_cell_x", 0)),
			int(data.get("from_cell_y", 0)))
	connector.to_layer = StringName(str(data.get("to_layer", "")))
	connector.to_cell = Vector2i(
			int(data.get("to_cell_x", 0)),
			int(data.get("to_cell_y", 0)))
	connector.one_way = bool(data.get("one_way", false))
	connector.locked = bool(data.get("locked", false))
	connector.connector_type = StringName(str(data.get("connector_type", "")))
	connector.activation_mode = int(data.get("activation_mode", MapConnectorResource.ActivationMode.INTERACT))

	if not connector.has_valid_route():
		return null

	return connector
