# ShipCoreInteraction — prototype player-side activation for dynamic ship assembly.
#
# Temporary gameplay rule:
# - ITEM_MACHINE_HULL_BASIC acts as the prototype ship core tool.
# - Press G while looking at a block to assemble connected blocks of the same
#   material into a DynamicStructureEntity.
# - This avoids accidentally collecting a whole planet made of mixed terrain.
#
# Later replacement:
# - Add a real ITEM_SHIP_CORE and placed ship_core controller block.
# - Use right-click on the placed core to assemble/disassemble.
class_name ShipCoreInteraction
extends Node

@export var player_path: NodePath = ^".."
@export var ship_command_bridge_path: NodePath = ^"../../ShipCommandBridge"
@export var activation_key := KEY_G
@export var max_ship_blocks := 512
@export var require_ship_core_item := true

var _player: PlayerController = null
var _ship_bridge: ShipCommandBridge = null


func _ready() -> void:
	_player = get_node_or_null(player_path) as PlayerController
	_ship_bridge = get_node_or_null(ship_command_bridge_path) as ShipCommandBridge


func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey):
		return
	var key_event := event as InputEventKey
	if not key_event.pressed or key_event.echo:
		return
	if key_event.keycode != activation_key:
		return
	if _try_assemble_target():
		get_viewport().set_input_as_handled()


func _try_assemble_target() -> bool:
	if _player == null or _ship_bridge == null:
		return false
	if _player.input_locked:
		return false
	var target: Dictionary = _player.target
	if target.is_empty():
		return false

	var equipment: GDPlayerEquipment = _player.equipment
	if equipment == null:
		return false
	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if require_ship_core_item and held_id != ItemDatabase.ITEM_MACHINE_HULL_BASIC:
		return false

	var data: Dictionary = target.get("data", {})
	var material_id := int(data.get("material", 0))
	if material_id <= 0:
		return false

	var dimension: StringName = target.get("dimension", PlayerController.OVERWORLD)
	var seed_cell: Vector3i = target.get("cell", Vector3i.ZERO)
	var result: Dictionary = _ship_bridge.assemble_from_cell(
		dimension, seed_cell, [material_id], max_ship_blocks)
	if not bool(result.get("ok", false)):
		print("ShipCoreInteraction: assemble rejected: %s" % str(result.get("reason", "unknown")))
		return false

	# Consume the prototype core item in survival. Creative keeps it.
	if require_ship_core_item and _player.game_mode != PlayerController.GameMode.CREATIVE:
		if _player.inventory != null:
			var slot := _player.inventory.find_item(held_id)
			if slot >= 0:
				_player.inventory.remove_from_slot(slot, 1)
				_player.inventory_changed.emit()

	print("ShipCoreInteraction: assembled structure %s from %s material=%d" % [
		str(result.get("structure", {}).get("structure_id", 0)), str(seed_cell), material_id])
	return true
