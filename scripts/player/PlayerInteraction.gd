# PlayerInteraction — Handles all player interaction logic: mining, placing,
# connectors, mechanisms, furnace access, and creature hunting.
# Extracted from PlayerController to separate interaction from movement/input.
class_name PlayerInteraction
extends Node

signal block_mined(block_key: String)
signal machine_placed(machine_type: String)
signal crop_planted(species_key: String)
signal crop_fertilized(species_key: String)
signal crop_harvested_interaction(species_key: String, count: int)

const REACH := 6.0
const ATTACK_REACH := 5.0
const BASE_ATTACK_DAMAGE := 0.1
const OVERWORLD: StringName = &"overworld"
const PLACEABLE_TERRAIN_MATERIAL_KEYS := {
	"workbench": "snt:workbench",
	"fence": "snt:fence",
}
const PLACEABLE_WORLD_OBJECT_TYPES := {
	"stone_furnace": &"furnace",
	# Ladder placement is still server-validated because it needs a wall.
	"ladder": &"ladder",
}
const QUEST_MACHINE_PLACEABLE_KEYS := {
	"workbench": true,
	"stone_furnace": true,
}

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


func _is_interaction_blocked() -> bool:
	return _player.game_mode == PlayerController.GameMode.OBSERVER


func _is_creative() -> bool:
	return _player.game_mode == PlayerController.GameMode.CREATIVE


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
	if _is_interaction_blocked():
		return false
	if _player.input_locked or _attack_cooldown_remaining > 0.0:
		return false

	var um: UniverseManager = _player.universe_manager
	if um == null or um.tick_system == null:
		return false

	var tick_system: GDTickSystem = um.tick_system
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

	var um: UniverseManager = _player.universe_manager
	if um == null or um.tick_system == null:
		return
	var tick_system: GDTickSystem = um.tick_system

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
	if _is_interaction_blocked():
		return false
	var command_server := _player.get_command_server()
	if command_server == null or target.is_empty():
		return false

	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if material == 0:
		return false

	# Skip cooldown in CREATIVE mode.
	if not _is_creative() and _cooldown_remaining > 0.0:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_MINE_BLOCK,
		"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE,
		"dimension": target.get("dimension", OVERWORLD),
		"chunk": target.get("chunk", Vector3i.ZERO),
		"local": target.get("local", Vector3i.ZERO),
		"cell": target.get("cell", Vector3i.ZERO),
		"expected_material": material,
	})
	if not bool(result.get("ok", false)):
		_debug("mine rejected: %s" % str(result.get("reason", "unknown")))
		return false

	if not _is_creative():
		_cooldown_remaining = float(result.get("mine_time", 0.5))

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
	if try_use_connector(false):
		return true
	if try_activate_mechanism(false):
		return true
	if try_use_station_blueprint():
		return true
	# TFC interactions in priority order
	if try_forage_wild(target):
		return true
	if try_use_knapping():
		return true
	# TFC structure interactions (placed blocks)
	if try_tfc_charcoal_pit(target):
		return true
	if try_tfc_pit_kiln(target):
		return true
	if try_tfc_bloomery(target):
		return true
	if try_tfc_anvil(target):
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
	if try_place_block(target):
		return true
	return false


# Feed a creature the player is aiming at. If the target is a wild proxy
# inside a fence enclosure, it is captured. If it is a captive creature,
# feeding boosts taming or triggers breeding. Feed items are fruits.
@warning_ignore("unsafe_call_argument")
func try_feed_creature() -> bool:
	if _is_interaction_blocked():
		return false
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	# Only fruit items can be used as creature feed (skip in CREATIVE mode).
	if not _is_creative() and held_id != ItemDatabase.ITEM_CHERRY_FRUIT \
			and held_id != ItemDatabase.ITEM_OLIVE_FRUIT:
		return false

	var um: UniverseManager = _player.universe_manager
	if um == null or um.tick_system == null:
		return false

	var tick_system: GDTickSystem = um.tick_system
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
	# Skip item consumption in CREATIVE mode.
	if not _is_creative() and (outcome == "captured" or outcome == "taming" \
			or outcome == "bred" or outcome == "fed"):
		if _player.inventory != null:
			var slot := _player.inventory.find_item(held_id)
			if slot >= 0:
				_player.inventory.remove_from_slot(slot, 1)
				_player.inventory_changed.emit()

	return true


@warning_ignore("unsafe_call_argument")
func try_place_world_object(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var command_server := _player.get_command_server()
	var equipment: GDPlayerEquipment = _player.equipment
	if target.is_empty() or command_server == null or equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)

	# CREATIVE mode: no item requirement for placement.
	if _is_creative() and held_id <= 0:
		# Try to deduce the object type from the target context.
		return false
	var held_key := ItemDatabase.get_item_key_by_id(held_id)
	if held_key.is_empty():
		return false
	var object_type: StringName = PLACEABLE_WORLD_OBJECT_TYPES.get(held_key, &"")
	if object_type == &"":
		return false

	var anchor_cell: Vector3i = target.get("build_anchor_cell", Vector3i.ZERO)
	var build_direction: Vector3i = target.get("build_direction", Vector3i.ZERO)
	if not GDPlanetBuildFrame.is_axis_direction(build_direction):
		return false

	var place_cell := _resolve_global_adjacent_place_cell(
		anchor_cell, build_direction)
	var build_mode := GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES
	var build_semantic := -1

	var world: ChunkRendererBridge = _player.world
	if world and _player.global_position.distance_to(
			world.cell_to_world_position(place_cell)) > REACH:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_OBJECT,
		"object_type": object_type,
		"dimension": _player.get_current_dimension(),
		"cell": place_cell,
		"anchor_cell": anchor_cell,
		"build_direction": build_direction,
		"build_mode": build_mode,
		"build_semantic": build_semantic,
		"item_id": held_id,
	})
	if not bool(result.get("ok", false)):
		_debug("place rejected: %s" % str(result.get("reason", "unknown")))
		return false

	# Emit machine_placed signal for quest system.
	var machine_type := _resolve_machine_type(held_id)
	if not machine_type.is_empty():
		machine_placed.emit(machine_type)

	# CREATIVE mode: don't consume the placed item.
	if _is_creative() and _player.inventory != null:
		_player.inventory.add_item(held_id, 1)

	_player.inventory_changed.emit()
	return true


func try_place_block(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	if target.is_empty():
		return false
	var command_server := _player.get_command_server()
	var equipment: GDPlayerEquipment = _player.equipment
	if command_server == null or equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id <= 0:
		return false
	var held_key := ItemDatabase.get_item_key_by_id(held_id)
	if held_key.is_empty():
		return false
	var material_id := _resolve_place_block_material_id(held_id)
	if material_id <= 0:
		return false

	var anchor_cell: Vector3i = target.get("build_anchor_cell", Vector3i.ZERO)
	var build_direction: Vector3i = target.get("build_direction", Vector3i.ZERO)
	if not GDPlanetBuildFrame.is_axis_direction(build_direction):
		return false
	var place_cell := _resolve_global_adjacent_place_cell(anchor_cell, build_direction)
	var world: ChunkRendererBridge = _player.world
	if world and _player.global_position.distance_to(
			world.cell_to_world_position(place_cell)) > REACH:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_BLOCK,
		"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE,
		"dimension": _player.get_current_dimension(),
		"cell": place_cell,
		"anchor_cell": anchor_cell,
		"build_direction": build_direction,
		"build_mode": GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES,
		"build_semantic": -1,
		"item_id": held_id,
		"material": material_id,
	})
	if not bool(result.get("ok", false)):
		_debug("place_block rejected: %s" % str(result.get("reason", "unknown")))
		return false

	if world:
		var chunk := world.cell_to_chunk(place_cell)
		var local := world.cell_to_local(place_cell)
		world.refresh_cell(_player.get_current_dimension(), chunk, local)

	if QUEST_MACHINE_PLACEABLE_KEYS.has(held_key):
		machine_placed.emit(held_key)

	if _is_creative() and _player.inventory != null:
		_player.inventory.add_item(held_id, 1)

	_player.inventory_changed.emit()
	return true


func _resolve_place_block_material_id(item_id: int) -> int:
	var world: ChunkRendererBridge = _player.world
	if world == null or world.worldgen_config == null:
		return 0
	var item_key := ItemDatabase.get_item_key_by_id(item_id)
	if item_key.is_empty():
		return 0

	var material_key := _item_key_to_terrain_material_key(item_key)
	if material_key.is_empty():
		return 0
	return int(world.worldgen_config.get_material_id(material_key))


func _item_key_to_terrain_material_key(item_key: String) -> String:
	if PLACEABLE_TERRAIN_MATERIAL_KEYS.has(item_key):
		return str(PLACEABLE_TERRAIN_MATERIAL_KEYS[item_key])

	if item_key.begins_with("block."):
		return "snt:" + item_key.substr("block.".length())

	match item_key:
		"log.oak":
			return "snt:oak_wood"
		"log.birch":
			return "snt:birch_wood"
		"log.spruce":
			return "snt:spruce_wood"
		"log.acacia":
			return "snt:acacia_wood"
		"log.maple":
			return "snt:maple_wood"
		"log.sequoia":
			return "snt:sequoia_wood"
		"log.cherry":
			return "snt:cherry_wood"
		"log.olive":
			return "snt:olive_wood"
		_:
			return ""


# Resolve placement on the fixed global voxel lattice.
# Gravity/planet-local rules must be handled by support/collapse checks, not by
# changing which cell is adjacent to the clicked face.
func _resolve_global_adjacent_place_cell(
		anchor_cell: Vector3i, face_direction: Vector3i) -> Vector3i:
	return Vector3i(
		anchor_cell.x + face_direction.x,
		anchor_cell.y + face_direction.y,
		anchor_cell.z + face_direction.z)


# --- TFC: Wild crop foraging ---
# Right-click a mature wild crop to harvest it without destroying the block.
# The crop resets to sprout stage and can regrow.
@warning_ignore("unsafe_call_argument")
func try_forage_wild(target: Dictionary) -> bool:
	if _is_interaction_blocked() or target.is_empty():
		return false
	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if not _is_crop_material(material):
		return false
	# Only wild crops (non-farmland plants) can be foraged.
	# Check the cell below — if it's not farmland, it's wild.
	var below_data: Dictionary = _get_cell_data(target, Vector3i.DOWN)
	if int(below_data.get("material", 0)) == BuiltinTerrainContent.MAT_FARMLAND:
		return false

	var command_server := _player.get_command_server()
	if command_server == null:
		return false
	var dimension = target.get("dimension", OVERWORLD)
	var cell: Vector3i = target.get("cell", Vector3i.ZERO)
	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_FORAGE_WILD,
		"dimension": dimension,
		"cell": cell,
	})
	if not bool(result.get("ok", false)):
		return false
	_debug("foraged wild crop at %s" % str(cell))
	return true


# Helper: get cell data at a relative offset from the target.
@warning_ignore("unsafe_call_argument")
func _get_cell_data(target: Dictionary, offset: Vector3i) -> Dictionary:
	var world: ChunkRendererBridge = _player.world
	if world == null:
		return {}
	var cell: Vector3i = target.get("cell", Vector3i.ZERO) + offset
	var dim: String = target.get("dimension", OVERWORLD)
	return world.get_cell_info(cell, StringName(dim)).get("data", {})


# --- TFC: Knapping interaction ---
# Hold a knappable stone (flint/chert) and right-click to open KnappingUI.
func try_use_knapping() -> bool:
	if _is_interaction_blocked():
		return false
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null:
		return false
	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_FLINT and held_id != ItemDatabase.ITEM_CHERT:
		return false
	var knapping_ui: KnappingUI = _player.knapping_ui
	if knapping_ui == null:
		return false
	if knapping_ui.is_open():
		return false
	knapping_ui.open(held_id)
	_player.set_input_locked(true)
	return true


# --- TFC structure interactions ---

# Charcoal pit: place, add log, cover, light, collect
@warning_ignore("unsafe_call_argument")
func try_tfc_charcoal_pit(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var mgr: CharcoalPitManager = _player.charcoal_pit_manager
	if mgr == null:
		return false
	var equip: GDPlayerEquipment = _player.equipment
	if equip == null:
		return false
	var held := equip.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var cell: Vector3i = target.get("cell", _player.get_current_cell())
	var dim := _player.get_current_dimension()
	var wood_log := ItemDatabase.mat_item(ItemDatabase.MATERIAL_WOOD, ItemDatabase.FORM_DUST)

	# Light pit with flint and steel first, or collect if ready
	if held == ItemDatabase.ITEM_FLINT_AND_STEEL and mgr.has_pit(dim, cell):
		var collect_result: Dictionary = mgr.collect(dim, cell)
		if bool(collect_result.get("ok", false)):
			var count := int(collect_result.get("count", 0))
			if count > 0:
				var cmd := _player.get_command_server()
				if cmd:
					cmd.submit_command({"type": GameCommandServer.COMMAND_ADD_INVENTORY_ITEM,
						"item_id": ItemDatabase.ITEM_CHARCOAL, "count": count,
						"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE})
			return true
		return mgr.light(dim, cell)

	# Cover pit with dirt/block (any solid block item)
	if mgr.has_pit(dim, cell):
		var data: Dictionary = _get_cell_data(target, Vector3i.ZERO)
		var mat := int(data.get("material", 0))
		if mat > 0 and mat != BuiltinTerrainContent.MAT_AIR:
			if mgr.cover(dim, cell):
				return true
		if held > 0 and mgr.cover(dim, cell):
			return true

	# Add log to pit
	if held == wood_log and mgr.has_pit(dim, cell):
		if mgr.add_log(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	# Place new pit with log
	if held == wood_log:
		if mgr.place_pit(dim, cell):
			mgr.add_log(dim, cell)
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	return false


# Pit kiln: place, add pottery, cover, light, collect
@warning_ignore("unsafe_call_argument")
func try_tfc_pit_kiln(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var mgr: PitKilnManager = _player.pit_kiln_manager
	if mgr == null:
		return false
	var equip: GDPlayerEquipment = _player.equipment
	if equip == null:
		return false
	var held := equip.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var cell: Vector3i = target.get("cell", _player.get_current_cell())
	var dim := _player.get_current_dimension()

	# Light or collect
	if mgr.has_kiln(dim, cell):
		# Try collect first
		var collect_result: Dictionary = mgr.collect(dim, cell)
		if bool(collect_result.get("ok", false)):
			var item_id := int(collect_result.get("item_id", 0))
			var count := int(collect_result.get("count", 0))
			if count > 0 and item_id > 0:
				var cmd := _player.get_command_server()
				if cmd:
					cmd.submit_command({"type": GameCommandServer.COMMAND_ADD_INVENTORY_ITEM,
						"item_id": item_id, "count": count,
						"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE})
			return true
		# Not ready — try light
		if held == ItemDatabase.ITEM_FLINT_AND_STEEL:
			return mgr.light(dim, cell)

	# Cover with straw
	if held == ItemDatabase.ITEM_STRAW and mgr.has_kiln(dim, cell):
		if mgr.cover(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	# Insert unfired pottery
	var is_unfired := held in [
		ItemDatabase.ITEM_UNFIRED_BOWL, ItemDatabase.ITEM_UNFIRED_JUG,
		ItemDatabase.ITEM_UNFIRED_CRUCIBLE, ItemDatabase.ITEM_UNFIRED_BRICK]
	if is_unfired and mgr.has_kiln(dim, cell):
		if mgr.insert_input(dim, cell, held):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	# Place new kiln with unfired pottery
	if is_unfired:
		if mgr.place_kiln(dim, cell):
			mgr.insert_input(dim, cell, held)
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	return false


# Bloomery: place, add ore/charcoal, light, bellows, break
@warning_ignore("unsafe_call_argument")
func try_tfc_bloomery(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var mgr: BloomeryManager = _player.bloomery_manager
	if mgr == null:
		return false
	var equip: GDPlayerEquipment = _player.equipment
	if equip == null:
		return false
	var held := equip.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var cell: Vector3i = target.get("cell", _player.get_current_cell())
	var dim := _player.get_current_dimension()
	var ws := _player.world

	# Break bloomery to collect iron bloom
	if mgr.has_bloomery(dim, cell):
		var break_result: Dictionary = mgr.break_bloomery(dim, cell)
		if bool(break_result.get("ok", false)):
			var yield_count := int(break_result.get("yield", 0))
			if yield_count > 0:
				var cmd := _player.get_command_server()
				if cmd:
					cmd.submit_command({"type": GameCommandServer.COMMAND_ADD_INVENTORY_ITEM,
						"item_id": ItemDatabase.ITEM_IRON_BLOOM, "count": yield_count,
						"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE})
			return true

	# Use bellows
	if held == ItemDatabase.ITEM_BELLOWS and mgr.has_bloomery(dim, cell):
		return mgr.use_bellows(dim, cell)

	# Light with flint and steel
	if held == ItemDatabase.ITEM_FLINT_AND_STEEL and mgr.has_bloomery(dim, cell):
		return mgr.light_bloomery(dim, cell)

	# Add charcoal
	if held == ItemDatabase.ITEM_CHARCOAL and mgr.has_bloomery(dim, cell):
		if mgr.add_charcoal(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	# Add iron crushed
	if held == ItemDatabase.iron_crushed() and mgr.has_bloomery(dim, cell):
		if mgr.add_ore(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	# Place new bloomery controller
	if held == ItemDatabase.ITEM_REFRACTORY_BRICK and not mgr.has_bloomery(dim, cell):
		if mgr.place_bloomery(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	return false


# Anvil: place, weld bloom into wrought iron
@warning_ignore("unsafe_call_argument")
func try_tfc_anvil(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var mgr: AnvilManager = _player.anvil_manager
	if mgr == null:
		return false
	var equip: GDPlayerEquipment = _player.equipment
	if equip == null:
		return false
	var held := equip.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var cell: Vector3i = target.get("cell", _player.get_current_cell())
	var dim := _player.get_current_dimension()

	# Weld: hold iron_bloom near anvil, hammer in inventory
	if held == ItemDatabase.ITEM_IRON_BLOOM and mgr.has_anvil(dim, cell):
		# Check if player has hammer in inventory
		var has_hammer := false
		var hammer_slot := -1
		if _player.inventory:
			for i in _player.inventory.slot_count():
				var data: Dictionary = _player.inventory.get_slot(i)
				if int(data.get("item_id", 0)) == ItemDatabase.ITEM_HAMMER and int(data.get("count", 0)) > 0:
					has_hammer = true
					hammer_slot = i
					break
		if not has_hammer:
			return false
		var weld_result: Dictionary = mgr.weld(dim, cell)
		if bool(weld_result.get("ok", false)):
			# Consume bloom
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
			# Grant wrought iron ingot
			var cmd := _player.get_command_server()
			if cmd:
				cmd.submit_command({"type": GameCommandServer.COMMAND_ADD_INVENTORY_ITEM,
					"item_id": ItemDatabase.ITEM_WROUGHT_IRON_INGOT, "count": 1,
					"player_handle": GameCommandServer.LOCAL_PLAYER_HANDLE})
			_player.inventory_changed.emit()
			return true

	# Place anvil
	if held == ItemDatabase.ITEM_ANVIL:
		if mgr.place_anvil(dim, cell):
			if not _is_creative() and _player.inventory:
				var slot := _find_item_inv(held)
				if slot >= 0:
					_player.inventory.remove_from_slot(slot, 1)
					_player.inventory_changed.emit()
			return true

	return false


# Helper: find item slot in player inventory
func _find_item_inv(item_id: int) -> int:
	if _player.inventory == null:
		return -1
	for i in _player.inventory.slot_count():
		var data: Dictionary = _player.inventory.get_slot(i)
		if int(data.get("item_id", 0)) == item_id and int(data.get("count", 0)) > 0:
			return i
	return -1


# --- Farming interactions (Tier 1 planting system) ---
# Three tick strategies are used for different plant types:
#   - Normal plants: random ticks (not implemented yet)
#   - Crops / industrial plants: scheduled ticks via CropGrowthSystem (priority 9)
#   - Special plants: entity ticks (Tier 5, future)
# Tier 1 only uses the scheduled-tick crop path. Player interactions here
# cover tilling, planting, fertilizing, and harvesting.

# Till a dirt block into farmland using a shovel.
@warning_ignore("unsafe_call_argument")
func try_till_farmland(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null or target.is_empty():
		return false
	# Require a shovel in the main hand (skip in CREATIVE mode).
	if not _is_creative():
		var stats: Dictionary = equipment.get_tool_stats()
		if int(stats.get("type", 0)) != ToolDef.ToolType.SHOVEL:
			return false
	# Target must be a dirt block.
	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if material != BuiltinTerrainContent.MAT_DIRT:
		return false

	var command_server := _player.get_command_server()
	if command_server == null:
		return false
	var dimension = target.get("dimension", OVERWORLD)
	var cell: Vector3i = target.get("cell", Vector3i.ZERO)
	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_TILL_FARMLAND,
		"dimension": dimension,
		"cell": cell,
	})
	if not bool(result.get("ok", false)):
		_debug("till rejected: %s" % str(result.get("reason", "unknown")))
		return false
	# terrain_cell_synced auto-refreshes the chunk via ChunkRendererBridge.
	_debug("tilled farmland at %s" % str(cell))
	return true


# Plant a seed on farmland. The crop occupies the air cell above the farmland.
@warning_ignore("unsafe_call_argument")
func try_plant_crop(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null or target.is_empty():
		return false
	# Held item must be a seed.
	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id <= 0 and not _is_creative():
		return false
	var item_key := ItemDatabase.get_item_key_by_id(held_id)
	if not item_key.begins_with("seed.") and not _is_creative():
		return false
	# Target must be farmland.
	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if material != BuiltinTerrainContent.MAT_FARMLAND:
		return false

	var command_server := _player.get_command_server()
	if command_server == null:
		return false
	var dimension = target.get("dimension", OVERWORLD)
	# The crop goes in the air cell above the farmland.
	var crop_cell: Vector3i = target.get("cell", Vector3i.ZERO) + Vector3i.UP
	# species_key is the seed key without the "seed." prefix.
	var species_key := item_key.substr(5)
	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLANT_CROP,
		"dimension": dimension,
		"cell": crop_cell,
		"item_id": held_id,
		"species_key": species_key,
	})
	if not bool(result.get("ok", false)):
		_debug("plant rejected: %s" % str(result.get("reason", "unknown")))
		return false
	# CREATIVE mode: don't consume the seed.
	if _is_creative() and _player.inventory != null and held_id > 0:
		_player.inventory.add_item(held_id, 1)
	crop_planted.emit(species_key)
	_player.inventory_changed.emit()
	_debug("planted %s at %s" % [species_key, str(crop_cell)])
	return true


# Fertilize a crop with bone meal to advance one growth stage.
@warning_ignore("unsafe_call_argument")
func try_fertilize_crop(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null or target.is_empty():
		return false
	# Held item must be bone meal (skip in CREATIVE mode).
	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_BONE_MEAL and not _is_creative():
		return false
	# Target must be a crop block (material in crop stage range).
	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if not _is_crop_material(material):
		return false

	var command_server := _player.get_command_server()
	if command_server == null:
		return false
	var dimension = target.get("dimension", OVERWORLD)
	var cell: Vector3i = target.get("cell", Vector3i.ZERO)
	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_FERTILIZE,
		"dimension": dimension,
		"cell": cell,
		"item_id": held_id,
	})
	if not bool(result.get("ok", false)):
		_debug("fertilize rejected: %s" % str(result.get("reason", "unknown")))
		return false
	# CREATIVE mode: don't consume the bone meal.
	if _is_creative() and _player.inventory != null and held_id > 0:
		_player.inventory.add_item(held_id, 1)
	crop_fertilized.emit("")  # species_key not returned by command; empty for generic tracking
	_player.inventory_changed.emit()
	_debug("fertilized crop at %s" % str(cell))
	return true


# Harvest a mature crop. Works with any held item (or empty hand).
@warning_ignore("unsafe_call_argument")
func try_harvest_crop(target: Dictionary) -> bool:
	if _is_interaction_blocked():
		return false
	if target.is_empty():
		return false
	# Target must be a crop block.
	var data: Dictionary = target.get("data", {})
	var material := int(data.get("material", 0))
	if not _is_crop_material(material):
		return false
	# Ensure the crop_harvested signal is connected before submitting.
	_connect_crop_harvest_signal()

	var command_server := _player.get_command_server()
	if command_server == null:
		return false
	var dimension = target.get("dimension", OVERWORLD)
	var cell: Vector3i = target.get("cell", Vector3i.ZERO)
	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_HARVEST_CROP,
		"dimension": dimension,
		"cell": cell,
	})
	if not bool(result.get("ok", false)):
		_debug("harvest rejected: %s" % str(result.get("reason", "unknown")))
		return false
	# Items are granted in _on_crop_harvested via the crop_harvested signal.
	var species_key: String = result.get("species_key", "")
	crop_harvested_interaction.emit(species_key, int(result.get("crop_count", 0)))
	_debug("harvested crop at %s" % str(cell))
	return true


# Check if a material ID is within the crop stage range (79-102).
func _is_crop_material(material: int) -> bool:
	return material >= BuiltinTerrainContent.MAT_WHEAT_SEED \
		and material <= BuiltinTerrainContent.MAT_PUMPKIN_MATURE


# Signal handler for crop_harvested — grants crop + byproduct items to inventory.
# C++ emits this signal because it doesn't own the item_id <-> item_key mapping.
@warning_ignore("unsafe_call_argument")
func _on_crop_harvested(_dimension, _cell: Vector3i, _species_key: String,
		crop_count: int, crop_item_key: String,
		byproduct_item_key: String, byproduct_count: int) -> void:
	if _player == null or _player.inventory == null:
		return
	# Grant main crop product.
	if not crop_item_key.is_empty() and crop_count > 0:
		var crop_id := ItemDatabase.get_item_id_by_key(crop_item_key)
		if crop_id >= 0:
			_player.inventory.add_item(crop_id, crop_count)
			_debug("harvest granted %s x%d" % [crop_item_key, crop_count])
		else:
			push_warning("PlayerInteraction: unknown crop item key '%s'" % crop_item_key)
	# Grant byproduct (usually seeds).
	if not byproduct_item_key.is_empty() and byproduct_count > 0:
		var byp_id := ItemDatabase.get_item_id_by_key(byproduct_item_key)
		if byp_id >= 0:
			_player.inventory.add_item(byp_id, byproduct_count)
			_debug("harvest byproduct %s x%d" % [byproduct_item_key, byproduct_count])
		else:
			push_warning("PlayerInteraction: unknown byproduct item key '%s'" % byproduct_item_key)
	_player.inventory_changed.emit()


func try_auto_cell_events() -> void:
	var cell := _player.get_current_cell()
	if cell == _player.last_cell:
		return
	_player.last_cell = cell
	if _cooldown_remaining <= 0.0:
		if try_use_connector(true):
			return
		try_activate_mechanism(true)


func try_use_connector(auto_only: bool) -> bool:
	if _player.input_locked or _cooldown_remaining > 0.0:
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
	if _player.input_locked:
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
		ItemDatabase.ITEM_CAMPFIRE:
			return "campfire"
		ItemDatabase.ITEM_LADDER:
			return "ladder"
	return ""


func _debug(message: String) -> void:
	if not _player.debug_interactions:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - _player.last_debug_time < _player.debug_interval:
		return
	_player.last_debug_time = now
	print("PlayerInteraction: ", message)


# --- Station blueprint interaction ---

# Station setup UI reference (lazily created).
var _station_setup_ui: StationSetupUI = null

# Cached hotbar slot index when the station blueprint UI was opened.
var _cached_blueprint_slot: int = -1


# Migration-only connector station prototype. U5 replaces this flow with a
# SpaceStation Sector; new formal station gameplay must not depend on it.
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
	_player.set_input_locked(true)
	return true


# Called when the player confirms station creation in the setup UI.
@warning_ignore("unsafe_call_argument")
func _on_station_confirmed(params: Dictionary) -> void:
	_player.set_input_locked(false)

	# Find the UniverseManager.
	var um: UniverseManager = _player.universe_manager
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
