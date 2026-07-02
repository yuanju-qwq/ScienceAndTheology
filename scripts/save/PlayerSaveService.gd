class_name PlayerSaveServiceNode
extends Node

const FORMAT_VERSION := 1
const PLAYERS_DIR := "players"
const EQUIPMENT_SLOT_COUNT := 7

@export var debug_player_save := false


func load_player(save_dir: String, identity: Dictionary) -> Dictionary:
	var path := get_player_path(save_dir, identity)
	if path == "":
		return {}
	if not FileAccess.file_exists(path):
		var legacy_path := get_legacy_player_path(save_dir, identity)
		if legacy_path != "" and legacy_path != path and FileAccess.file_exists(legacy_path):
			path = legacy_path
		else:
			if debug_player_save:
				print("[PlayerSaveService] no player save at %s" % path)
			return {}

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		push_warning("PlayerSaveService: failed to open %s" % path)
		return {}

	var text := file.get_as_text()
	file.close()

	var json := JSON.new()
	if json.parse(text) != OK:
		push_warning("PlayerSaveService: failed to parse %s" % path)
		return {}
	if not (json.data is Dictionary):
		push_warning("PlayerSaveService: %s is not a dictionary" % path)
		return {}

	var data: Dictionary = json.data
	if debug_player_save:
		print("[PlayerSaveService] loaded %s" % path)
	return data


func save_player(save_dir: String, identity: Dictionary, player_data: Dictionary) -> bool:
	var players_dir := get_players_dir(save_dir)
	if players_dir == "":
		return false
	if not DirAccess.dir_exists_absolute(players_dir):
		DirAccess.make_dir_recursive_absolute(players_dir)

	var path := get_player_path(save_dir, identity)
	if path == "":
		return false

	var data := player_data.duplicate(true)
	data["format_version"] = FORMAT_VERSION
	data["identity"] = identity.duplicate(true)
	data["player_uuid"] = get_player_uuid(identity)
	data["player_key"] = get_player_key(identity)
	data["legacy_player_key"] = get_legacy_player_key(identity)
	data["updated_at"] = Time.get_datetime_string_from_system()

	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_warning("PlayerSaveService: failed to write %s" % path)
		return false
	file.store_string(JSON.stringify(data, "\t"))
	file.close()

	if debug_player_save:
		print("[PlayerSaveService] saved %s" % path)
	return true


func get_players_dir(save_dir: String) -> String:
	if save_dir.strip_edges() == "":
		return ""
	return _join_path(save_dir, PLAYERS_DIR)


func get_player_path(save_dir: String, identity: Dictionary) -> String:
	var key := get_player_key(identity)
	if save_dir.strip_edges() == "" or key == "":
		return ""
	return _join_path(get_players_dir(save_dir), key + ".json")


func get_legacy_player_path(save_dir: String, identity: Dictionary) -> String:
	var key := get_legacy_player_key(identity)
	if save_dir.strip_edges() == "" or key == "":
		return ""
	return _join_path(get_players_dir(save_dir), key + ".json")


func get_player_key(identity: Dictionary) -> String:
	var uuid := get_player_uuid(identity)
	if uuid != "":
		return _sanitize_key(uuid)
	var key := str(identity.get("player_key", "")).strip_edges()
	if key != "" and key.find("_") < 0:
		return _sanitize_key(key)
	return get_legacy_player_key(identity)


func get_player_uuid(identity: Dictionary) -> String:
	var uuid := str(identity.get("player_uuid", "")).strip_edges().to_lower()
	if uuid != "":
		return uuid
	var key := str(identity.get("player_key", "")).strip_edges().to_lower()
	if _is_uuid(key):
		return key
	var provider := str(identity.get("provider", "offline")).strip_edges()
	var account_id := str(identity.get("account_id", "")).strip_edges()
	if provider == "offline" and _is_uuid(account_id):
		return account_id.to_lower()
	return ""


func get_legacy_player_key(identity: Dictionary) -> String:
	var provider := str(identity.get("provider", "offline")).strip_edges()
	var account_id := str(identity.get("account_id", "")).strip_edges()
	if account_id == "":
		return ""
	return _sanitize_key("%s_%s" % [provider, account_id])


func export_inventory(inventory: Object) -> Dictionary:
	if inventory == null:
		return {}
	var slot_count := int(inventory.call("get_slot_count"))
	var slots: Array = []
	for i in range(slot_count):
		var slot: Dictionary = inventory.call("get_slot", i)
		slots.append({
			"item_id": int(slot.get("item_id", 0)),
			"count": int(slot.get("count", 0)),
			"secondary_id": int(slot.get("secondary_id", -1)),
		})
	return {
		"width": int(inventory.call("get_width")),
		"height": int(inventory.call("get_height")),
		"max_stack": int(inventory.call("get_max_stack")),
		"slots": slots,
	}


func apply_inventory(inventory: Object, data: Dictionary) -> bool:
	if inventory == null or data.is_empty():
		return false

	var width := int(data.get("width", inventory.call("get_width")))
	var height := int(data.get("height", inventory.call("get_height")))
	if width > 0 and height > 0:
		inventory.call("init", width, height)

	if data.has("max_stack"):
		inventory.call("set_max_stack", int(data.get("max_stack", inventory.call("get_max_stack"))))

	inventory.call("clear")
	var slots: Array = data.get("slots", [])
	var limit := mini(slots.size(), int(inventory.call("get_slot_count")))
	for i in range(limit):
		if not (slots[i] is Dictionary):
			continue
		var slot: Dictionary = slots[i]
		var item_id := int(slot.get("item_id", 0))
		var count := int(slot.get("count", 0))
		var secondary_id := int(slot.get("secondary_id", -1))
		if item_id > 0 and count > 0:
			inventory.call("set_slot", i, item_id, count, secondary_id)
	return true


func export_equipment(equipment: Object) -> Dictionary:
	if equipment == null:
		return {}
	var slots: Array = []
	for slot_index in range(EQUIPMENT_SLOT_COUNT):
		slots.append({
			"slot": slot_index,
			"item_id": int(equipment.call("get_equipped", slot_index)),
		})
	return {"slots": slots}


func apply_equipment(equipment: Object, data: Dictionary) -> bool:
	if equipment == null or data.is_empty():
		return false
	equipment.call("clear")
	var slots: Array = data.get("slots", [])
	for entry in slots:
		if not (entry is Dictionary):
			continue
		var slot_index := int(entry.get("slot", -1))
		var item_id := int(entry.get("item_id", 0))
		if slot_index >= 0 and slot_index < EQUIPMENT_SLOT_COUNT and item_id > 0:
			equipment.call("equip", slot_index, item_id)
	return true


func _join_path(base: String, child: String) -> String:
	if base.ends_with("/") or base.ends_with("\\"):
		return base + child
	return base + "/" + child


func _sanitize_key(raw: String) -> String:
	var result := ""
	for i in range(raw.length()):
		var ch := raw.substr(i, 1)
		if ch.is_valid_identifier() or ch.is_valid_int() or ch == "-" or ch == "_":
			result += ch
		else:
			result += "_"
	return result


func _is_uuid(value: String) -> bool:
	var text := value.strip_edges().to_lower()
	if text.length() != 36:
		return false
	for i in range(text.length()):
		var ch := text.substr(i, 1)
		if i == 8 or i == 13 or i == 18 or i == 23:
			if ch != "-":
				return false
		elif "0123456789abcdef".find(ch) < 0:
			return false
	return true
