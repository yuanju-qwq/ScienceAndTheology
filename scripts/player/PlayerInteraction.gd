# PlayerInteraction — Handles all player interaction logic: mining, placing,
# connectors, mechanisms, and furnace access.
# Extracted from PlayerController to separate interaction from movement/input.
class_name PlayerInteraction
extends Node

const REACH := 6.0
const OVERWORLD: StringName = &"overworld"

var _player: PlayerController = null
var _cooldown_remaining := 0.0


func setup(player: PlayerController) -> void:
	_player = player


func process_cooldown(delta: float) -> void:
	if _cooldown_remaining > 0.0:
		_cooldown_remaining = maxf(_cooldown_remaining - delta, 0.0)


func try_mine_target(target: Dictionary) -> bool:
	var command_server := _player.get_command_server()
	if command_server == null or target.is_empty():
		return false

	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if material == 0:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_MINE_BLOCK,
		"dimension": target.get("dimension", OVERWORLD),
		"chunk": target.get("chunk", Vector3i.ZERO),
		"local": target.get("local", Vector3i.ZERO),
		"cell": target.get("cell", Vector3i.ZERO),
		"expected_material": material,
	})
	if not bool(result.get("ok", false)):
		_debug("mine rejected: %s" % str(result.get("reason", "unknown")))
		return false

	var world: ChunkRendererBridge = _player.world
	if world:
		var chunk: Vector3i = target.get("chunk", Vector3i.ZERO)
		var local: Vector3i = target.get("local", Vector3i.ZERO)
		world.refresh_cell(target.get("dimension", OVERWORLD), chunk, local)
	_player.inventory_changed.emit()
	return true


func try_place_or_interact(target: Dictionary) -> bool:
	if try_open_furnace(false):
		return true
	if try_use_connector(false):
		return true
	if try_activate_mechanism(false):
		return true
	if try_use_station_blueprint():
		return true
	if try_place_world_object(target):
		return true
	return false


func try_place_world_object(target: Dictionary) -> bool:
	var command_server := _player.get_command_server()
	var equipment: GDPlayerEquipment = _player.equipment
	if target.is_empty() or command_server == null or equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var object_type := &""
	match held_id:
		ItemDatabase.ITEM_WORKBENCH:
			object_type = GameCommandServer.OBJECT_WORKBENCH
		ItemDatabase.ITEM_FURNACE:
			object_type = GameCommandServer.OBJECT_FURNACE
		ItemDatabase.ITEM_LADDER:
			object_type = GameCommandServer.OBJECT_LADDER
		_:
			return false

	var place_cell: Vector3i = target.get("place_cell", _player.get_current_cell())
	var world: ChunkRendererBridge = _player.world
	if world and _player.global_position.distance_to(world.cell_to_world_position(place_cell)) > REACH:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_OBJECT,
		"object_type": object_type,
		"dimension": _player.get_current_dimension(),
		"cell": place_cell,
		"item_id": held_id,
	})
	if not bool(result.get("ok", false)):
		_debug("place rejected: %s" % str(result.get("reason", "unknown")))
		return false

	_player.inventory_changed.emit()
	return true


func try_auto_cell_events() -> void:
	var cell := _player.get_current_cell()
	if cell == _player._last_cell:
		return
	_player._last_cell = cell
	if _cooldown_remaining <= 0.0:
		if try_use_connector(true):
			return
		try_activate_mechanism(true)


func try_use_connector(auto_only: bool) -> bool:
	if _player._input_locked or _cooldown_remaining > 0.0:
		return false
	var connector_manager: ConnectorManager = _player.connector_manager
	if connector_manager == null:
		return false

	var dimension := _player.get_current_dimension()
	var cell := _player.get_current_cell()
	var connector = connector_manager.get_connector_at(dimension, cell)
	if connector == null:
		return false
	if auto_only and not connector.activates_on_enter():
		return false
	if not auto_only and not connector.requires_interaction():
		return false

	var target_dimension: StringName = connector.get_target_dimension_for(dimension, cell)
	var target_cell: Vector3i = connector.get_target_cell_for(dimension, cell)
	if target_dimension == &"":
		return false

	_cooldown_remaining = _player.connector_cooldown
	_player.connector_used.emit(connector.connector_id, dimension, target_dimension)
	return true


func try_activate_mechanism(auto_only: bool) -> bool:
	if _player._input_locked:
		return false
	var mechanism_manager: MechanismManager = _player.mechanism_manager
	if mechanism_manager == null:
		return false

	var dimension := _player.get_current_dimension()
	var cell := _player.get_current_cell()
	var mechanism = mechanism_manager.get_mechanism_at(dimension, cell)
	if mechanism == null:
		return false
	if auto_only and not mechanism.activates_on_enter():
		return false
	if not auto_only and not mechanism.requires_interaction():
		return false
	if not mechanism_manager.activate_mechanism(mechanism.mechanism_id):
		return false

	_cooldown_remaining = _player.connector_cooldown
	_player.mechanism_activated.emit(mechanism.mechanism_id, dimension)
	return true


func try_open_furnace(auto_only: bool) -> bool:
	if auto_only:
		return false
	var furnace_manager: FurnaceManager = _player.furnace_manager
	var furnace_ui = _player.furnace_ui
	if furnace_manager == null or furnace_ui == null:
		return false
	var cell := _player.get_current_cell()
	var dimension := _player.get_current_dimension()
	var candidates := [cell]
	var target := _player._target
	if not target.is_empty():
		candidates.append(target.get("cell", cell))
		candidates.append(target.get("place_cell", cell))
	candidates.append_array([
		cell + Vector3i.RIGHT,
		cell + Vector3i.LEFT,
		cell + Vector3i.UP,
		cell + Vector3i.DOWN,
		cell + Vector3i.FORWARD,
		cell + Vector3i.BACK,
	])

	for candidate in candidates:
		if not furnace_manager.has_furnace(dimension, candidate):
			continue
		var data = furnace_manager.get_furnace(dimension, candidate)
		furnace_ui.open(data, dimension, candidate, furnace_manager)
		_player._set_input_locked(true)
		_cooldown_remaining = _player.connector_cooldown
		return true
	return false


func get_nearby_station() -> String:
	var world: ChunkRendererBridge = _player.world
	if world == null:
		return ""
	var cell := _player.get_current_cell()
	var dimension := _player.get_current_dimension()
	for offset in [
		Vector3i.ZERO,
		Vector3i.RIGHT,
		Vector3i.LEFT,
		Vector3i.UP,
		Vector3i.DOWN,
		Vector3i.FORWARD,
		Vector3i.BACK,
	]:
		var info := world.get_cell_info(cell + offset, dimension)
		var data: Dictionary = info.get("data", {})
		var material := int(data.get("material", 0))
		if material == world.get_workbench_material_id():
			return "workbench"
	return ""


func _debug(message: String) -> void:
	if not _player.debug_interactions:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - _player._last_debug_time < _player.debug_interval:
		return
	_player._last_debug_time = now
	print("PlayerInteraction: ", message)


# --- Station blueprint interaction ---

# Station setup UI reference (lazily created).
var _station_setup_ui: StationSetupUI = null


# Try to use the Station Blueprint item from the player's main hand.
# Opens the station setup UI. Returns true if the blueprint was used.
func try_use_station_blueprint() -> bool:
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_STATION_BLUEPRINT:
		return false

	# Lazily create the station setup UI.
	if _station_setup_ui == null:
		_station_setup_ui = StationSetupUI.new()
		_station_setup_ui.name = "StationSetupUI"
		_player.get_viewport().get_node_or_null("/root").add_child(_station_setup_ui)
		_station_setup_ui.station_confirmed.connect(_on_station_confirmed)

	if _station_setup_ui.is_open():
		return false

	_station_setup_ui.open()
	_player._set_input_locked(true)
	return true


# Called when the player confirms station creation in the setup UI.
func _on_station_confirmed(params: Dictionary) -> void:
	_player._set_input_locked(false)

	# Find the UniverseManager.
	var um: UniverseManager = _player._universe_manager
	if um == null:
		push_warning("PlayerInteraction: cannot create station — no UniverseManager")
		return

	# Find the parent planet (the player's current planet).
	var parent_planet: PlanetDescriptor = um.active_planet
	if parent_planet == null:
		push_warning("PlayerInteraction: cannot create station — not on a planet")
		return

	var display_name: String = params.get("display_name", "Outpost")
	var station_type: int = params.get("station_type", 0)
	var orbit_height: float = params.get("orbit_height", 2000.0)
	var gravity_mult: float = params.get("gravity_multiplier", 1.0)

	var station := um.create_station(
		display_name, parent_planet, orbit_height, station_type, gravity_mult)
	if station == null:
		push_warning("PlayerInteraction: station creation failed")
		return

	# Consume the blueprint item.
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment != null:
		var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
		if held_id == ItemDatabase.ITEM_STATION_BLUEPRINT:
			_player.inventory.remove_slot(_player.selected_hotbar)
			_player.inventory_changed.emit()

	_debug("Station created: %s (%s)" % [station.display_name, String(station.dimension_id)])
