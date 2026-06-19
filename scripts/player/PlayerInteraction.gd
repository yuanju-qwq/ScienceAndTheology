# PlayerInteraction — Handles all player interaction logic: mining, placing,
# connectors, mechanisms, furnace access, and creature hunting.
# Extracted from PlayerController to separate interaction from movement/input.
class_name PlayerInteraction
extends Node

signal block_mined(block_key: String)
signal machine_placed(machine_type: String)
signal crop_planted(species_key: String)
signal crop_harvested_interaction(species_key: String, count: int)

const REACH := 6.0
const ATTACK_REACH := 5.0
const BASE_ATTACK_DAMAGE := 0.1
const OVERWORLD: StringName = &"overworld"

var _player: PlayerController = null
var _cooldown_remaining := 0.0
var _attack_cooldown_remaining := 0.0
var _crop_signal_connected := false


func setup(player: PlayerController) -> void:
	_player = player
	_connect_crop_harvest_signal()


func _connect_crop_harvest_signal() -> void:
	if _crop_signal_connected:
		return
	var command_server := _player.get_command_server()
	if command_server == null:
		return
	if not command_server.crop_harvested.is_connected(_on_crop_harvested):
		command_server.crop_harvested.connect(_on_crop_harvested)
	_crop_signal_connected = true


func process_cooldown(delta: float) -> void:
	if _cooldown_remaining > 0.0:
		_cooldown_remaining = maxf(_cooldown_remaining - delta, 0.0)
	if _attack_cooldown_remaining > 0.0:
		_attack_cooldown_remaining = maxf(_attack_cooldown_remaining - delta, 0.0)


# --- Creature hunting ---

# Attempt to attack a creature in the player's view cone.
# Uses server-side distance + view cone detection (no Area3D needed).
# Damage is driven by the equipped weapon's attack_damage stat.
# Returns true if an attack was attempted (hit or miss).
@warning_ignore("unsafe_call_argument")
func try_attack_creature() -> bool:
	if _player._input_locked or _attack_cooldown_remaining > 0.0:
		return false

	var um: UniverseManager = _player._universe_manager
	if um == null or um._tick_system == null:
		return false

	var tick_system: GDTickSystem = um._tick_system
	var camera: Camera3D = _player.camera
	if camera == null:
		return false

	# Compute player position and look direction.
	var player_pos := _player.global_position
	var look_dir := -camera.global_transform.basis.z.normalized()

	# Get attack damage from equipment.
	var damage := BASE_ATTACK_DAMAGE
	var attack_cooldown := 0.5
	var reach := ATTACK_REACH
	if _player.equipment != null:
		var stats: Dictionary = _player.equipment.get_tool_stats()
		if stats.get("attack_damage", 0.0) > 0.0:
			damage = stats["attack_damage"]
		if stats.get("attack_cooldown", 0.0) > 0.0:
			attack_cooldown = stats["attack_cooldown"]
		if stats.get("range", 0.0) > 0.0:
			reach = stats["range"]

	# Get current dimension.
	var dimension := _player.get_current_dimension()

	# Submit attack to C++ side.
	var result: Dictionary = tick_system.attack_creature(
		dimension, player_pos, look_dir, reach, damage)

	if not bool(result.get("hit", false)):
		# Miss — still apply a short cooldown to prevent spam.
		_attack_cooldown_remaining = attack_cooldown * 0.5
		return false

	# Hit — apply full attack cooldown.
	_attack_cooldown_remaining = attack_cooldown

	_debug("attack hit creature %d (species %d), damage=%.2f, health=%.2f" % [
		int(result.get("creature_id", 0)),
		int(result.get("species_id", 0)),
		float(result.get("damage_dealt", 0.0)),
		float(result.get("remaining_health", 0.0)),
	])

	if bool(result.get("killed", false)):
		_on_creature_killed(result)

	return true


# Called when a creature is killed by the player.
# Handles loot drops from the species' drop table.
@warning_ignore("unsafe_call_argument")
func _on_creature_killed(result: Dictionary) -> void:
	var species_id := int(result.get("species_id", 0))
	_debug("creature killed! species_id=%d" % species_id)

	var um: UniverseManager = _player._universe_manager
	if um == null or um._tick_system == null:
		return
	var tick_system: GDTickSystem = um._tick_system

	# Query the species' drop table from C++ side.
	var drops: Array = tick_system.get_species_drops(species_id)
	if drops.is_empty():
		return

	# Process each drop entry with chance-based roll.
	for drop: Dictionary in drops:
		var item_key: String = str(drop.get("item_key", ""))
		var chance: float = float(drop.get("chance", 0.0))
		var min_count: int = int(drop.get("min_count", 1))
		var max_count: int = int(drop.get("max_count", 1))

		if item_key.is_empty():
			continue

		# Roll for drop chance.
		if randf() > chance:
			continue

		# Resolve item_key to item_id via ItemDatabase.
		var item_id := ItemDatabase.get_item_id_by_key(item_key)
		if item_id < 0:
			push_warning("PlayerInteraction: unknown drop item_key '%s'" % item_key)
			continue

		# Random count between min and max.
		var count := mini(randi_range(min_count, max_count), 64)
		if count <= 0:
			continue

		# Add to player inventory.
		if _player.inventory != null:
			_player.inventory.add_item(item_id, count)
			_player.inventory_changed.emit()
		_debug("  drop: %s x%d (item_id=%d)" % [item_key, count, item_id])


@warning_ignore("unsafe_call_argument")
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
		world.refresh_cell(StringName(target.get("dimension", OVERWORLD)), chunk, local)

	# Emit block_mined signal for quest system.
	var block_key := _resolve_block_key(material)
	if not block_key.is_empty():
		block_mined.emit(block_key)

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
	if try_till_farmland(target):
		return true
	if try_plant_crop(target):
		return true
	if try_fertilize_crop(target):
		return true
	if try_harvest_crop(target):
		return true
	if try_feed_creature():
		return true
	if try_place_world_object(target):
		return true
	return false


# Feed a creature the player is aiming at. If the target is a wild proxy
# inside a fence enclosure, it is captured. If it is a captive creature,
# feeding boosts taming or triggers breeding. Feed items are fruits.
@warning_ignore("unsafe_call_argument")
func try_feed_creature() -> bool:
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	# Only fruit items can be used as creature feed.
	if held_id != ItemDatabase.ITEM_CHERRY_FRUIT and held_id != ItemDatabase.ITEM_OLIVE_FRUIT:
		return false

	var um: UniverseManager = _player._universe_manager
	if um == null or um._tick_system == null:
		return false

	var tick_system: GDTickSystem = um._tick_system
	var camera: Camera3D = _player.camera
	if camera == null:
		return false

	var player_pos := _player.global_position
	var look_dir := -camera.global_transform.basis.z.normalized()
	var dimension := _player.get_current_dimension()

	var result: Dictionary = tick_system.feed_creature_at(
		dimension, player_pos, look_dir, ATTACK_REACH)

	if not bool(result.get("hit", false)):
		return false

	var outcome := String(result.get("outcome", "miss"))
	_debug("feed outcome: %s (creature %d, species %d)" % [
		outcome,
		int(result.get("creature_id", 0)),
		int(result.get("species_id", 0)),
	])

	# Consume one feed item on a successful interaction
	# (capture, taming, bred, or fed — but not on "no_enclosure" / "pen_full").
	if outcome == "captured" or outcome == "taming" \
			or outcome == "bred" or outcome == "fed":
		if _player.inventory != null:
			var slot := _player.inventory.find_item(held_id)
			if slot >= 0:
				_player.inventory.remove_from_slot(slot, 1)
				_player.inventory_changed.emit()

	return true


@warning_ignore("unsafe_call_argument")
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
		ItemDatabase.ITEM_FENCE:
			object_type = GameCommandServer.OBJECT_FENCE
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

	# Emit machine_placed signal for quest system.
	var machine_type := _resolve_machine_type(held_id)
	if not machine_type.is_empty():
		machine_placed.emit(machine_type)

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
	var connector: MapConnector = connector_manager.get_connector_at(dimension, cell)
	if connector == null:
		return false
	if auto_only and not connector.activates_on_enter():
		return false
	if not auto_only and not connector.requires_interaction():
		return false

	var target_dimension: StringName = connector.get_target_dimension_for(dimension, cell)
	var _target_cell: Vector3i = connector.get_target_cell_for(dimension, cell)
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
	var mechanism: MapMechanism = mechanism_manager.get_mechanism_at(dimension, cell)
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


@warning_ignore("unsafe_call_argument")
func try_open_furnace(auto_only: bool) -> bool:
	if auto_only:
		return false
	var furnace_manager: FurnaceManager = _player.furnace_manager
	var furnace_ui: FurnaceUI = _player.furnace_ui
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

	for candidate: Vector3i in candidates:
		if not furnace_manager.has_furnace(dimension, candidate):
			continue
		var data: GDFurnaceData = furnace_manager.get_furnace(dimension, candidate)
		furnace_ui.open(data, dimension, candidate, furnace_manager)
		_player._set_input_locked(true)
		_cooldown_remaining = _player.connector_cooldown
		return true
	return false


@warning_ignore("unsafe_call_argument")
func get_nearby_station() -> String:
	var world: ChunkRendererBridge = _player.world
	if world == null:
		return ""
	var cell := _player.get_current_cell()
	var dimension := _player.get_current_dimension()
	for offset: Vector3i in [
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


# Resolve a block key from a material ID for quest conditions.
# Strips the "snt:" prefix from material keys for cleaner matching.
@warning_ignore("unsafe_call_argument")
func _resolve_block_key(material_id: int) -> String:
	if _player == null or _player.world == null:
		return ""
	var world_data := _player.world.get_world_data()
	if world_data == null:
		return ""
	var def: Dictionary = world_data.get_terrain_material_def(material_id)
	var key: String = def.get("key", "")
	if key.is_empty():
		return str(material_id)
	# Strip "snt:" prefix for short key matching.
	if key.begins_with("snt:"):
		key = key.substr(4)
	return key


# Resolve a machine type string from an item ID for quest conditions.
# Uses the item key from ItemDatabase, falling back to the object type.
func _resolve_machine_type(item_id: int) -> String:
	var key := ItemDatabase.get_item_key_by_id(item_id)
	if not key.is_empty():
		return key
	# Fallback: match hold item to object type.
	match item_id:
		ItemDatabase.ITEM_WORKBENCH:
			return "workbench"
		ItemDatabase.ITEM_FURNACE:
			return "stone_furnace"
		ItemDatabase.ITEM_LADDER:
			return "ladder"
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

# Cached hotbar slot index when the station blueprint UI was opened.
var _cached_blueprint_slot: int = -1


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
	_cached_blueprint_slot = _player.selected_hotbar
	_player._set_input_locked(true)
	return true


# Called when the player confirms station creation in the setup UI.
@warning_ignore("unsafe_call_argument")
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
		if held_id == ItemDatabase.ITEM_STATION_BLUEPRINT and _cached_blueprint_slot >= 0:
			_player.inventory.set_slot(_cached_blueprint_slot, 0, 0, -1)
			_cached_blueprint_slot = -1
			_player.inventory_changed.emit()

	_debug("Station created: %s (%s)" % [station.display_name, String(station.dimension_id)])
