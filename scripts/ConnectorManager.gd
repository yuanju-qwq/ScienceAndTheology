# ConnectorManager — manages MapConnector resources using 3D cell coordinates.
# All lookups use dimension + Vector3i.
class_name ConnectorManager
extends Node

signal connectors_changed

@export var connectors: Array[MapConnector] = []


func add_connector(connector: MapConnector) -> void:
	if connector == null:
		return

	connectors.append(connector)
	connectors_changed.emit()


func add_generated_connectors(connector_data: Array) -> int:
	var added := 0

	for entry: Variant in connector_data:
		if not (entry is Dictionary):
			continue

		var connector := _make_connector_from_dict(entry as Dictionary)
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


func get_connector_at(dimension: StringName, cell_position: Vector3i) -> MapConnector:
	for connector in connectors:
		if connector != null and connector.is_enterable_from(dimension, cell_position):
			return connector

	return null


func has_connector_at(dimension: StringName, cell_position: Vector3i) -> bool:
	return get_connector_at(dimension, cell_position) != null


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


func get_connectors_for_dimension(dimension: StringName) -> Array[MapConnector]:
	var dim_connectors: Array[MapConnector] = []

	for connector in connectors:
		if connector == null:
			continue

		if connector.from_dimension == dimension or (not connector.one_way and connector.to_dimension == dimension):
			dim_connectors.append(connector)

	return dim_connectors


func get_target_for(dimension: StringName, cell_position: Vector3i) -> Dictionary:
	var connector := get_connector_at(dimension, cell_position)
	if connector == null:
		return {}

	return {
		"connector": connector,
		"dimension": connector.get_target_dimension_for(dimension, cell_position),
		"cell": connector.get_target_cell_for(dimension, cell_position),
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

		_add_entrance_validation_issue(issues, entrance_keys, index, connector.from_dimension, connector.from_cell)
		if not connector.one_way:
			_add_entrance_validation_issue(issues, entrance_keys, index, connector.to_dimension, connector.to_cell)

	return issues


func _add_entrance_validation_issue(
		issues: PackedStringArray,
		entrance_keys: Dictionary,
		index: int,
		dimension: StringName,
		cell_position: Vector3i) -> void:
	var entrance_key := _make_entrance_key(dimension, cell_position)
	if entrance_keys.has(entrance_key):
		issues.append("Connector %d shares an entrance with connector %d." % [index, entrance_keys[entrance_key]])
	else:
		entrance_keys[entrance_key] = index


func _make_entrance_key(dimension: StringName, cell_position: Vector3i) -> String:
	return "%s:%d:%d:%d" % [String(dimension), cell_position.x, cell_position.y, cell_position.z]


func _has_connector_id(connector_id: int) -> bool:
	if connector_id == 0:
		return false

	for connector in connectors:
		if connector != null and connector.connector_id == connector_id:
			return true

	return false


@warning_ignore("unsafe_call_argument", "int_as_enum_without_cast")
func _make_connector_from_dict(data: Dictionary) -> MapConnector:
	var connector := MapConnector.new()
	connector.connector_id = int(data.get("connector_id", 0))
	connector.from_dimension = StringName(str(data.get("from_dimension", "")))
	connector.from_cell = Vector3i(
			int(data.get("from_cell_x", 0)),
			int(data.get("from_cell_y", 0)),
			int(data.get("from_cell_z", 0)))
	connector.to_dimension = StringName(str(data.get("to_dimension", "")))
	connector.to_cell = Vector3i(
			int(data.get("to_cell_x", 0)),
			int(data.get("to_cell_y", 0)),
			int(data.get("to_cell_z", 0)))
	connector.one_way = bool(data.get("one_way", false))
	connector.locked = bool(data.get("locked", false))
	connector.connector_type = StringName(str(data.get("connector_type", "")))
	connector.activation_mode = int(data.get("activation_mode", MapConnector.ActivationMode.INTERACT)) as MapConnector.ActivationMode

	if not connector.has_valid_route():
		return null

	return connector
