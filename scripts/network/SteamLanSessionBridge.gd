class_name SteamLanSessionBridge
extends Node

const SteamP2PTransportScript := preload("res://scripts/network/SteamP2PTransport.gd")

signal host_started(local_steam_id: String)
signal client_started(host_steam_id: String)
signal login_accepted(player_handle: int)
signal login_rejected(reason: String)
signal remote_player_connected(player_handle: int, steam_id: String, identity: Dictionary)
signal remote_player_disconnected(player_handle: int, steam_id: String)
signal command_result_received(result: Dictionary)
signal sync_event_received(event_name: String, payload: Dictionary)
signal position_received(player_handle: int, payload: Dictionary)

const ROLE_NONE := "none"
const ROLE_HOST := "host"
const ROLE_CLIENT := "client"

const PACKET_LOGIN_REQUEST := "login_request"
const PACKET_LOGIN_ACCEPT := "login_accept"
const PACKET_LOGIN_REJECT := "login_reject"
const PACKET_COMMAND := "command"
const PACKET_COMMAND_RESULT := "command_result"
const PACKET_SYNC_EVENT := "sync_event"
const PACKET_POSITION := "position"
const PROTOCOL_VERSION := 1
const LOCAL_HOST_PLAYER_HANDLE := 1

const REMOTE_INVENTORY_WIDTH := 9
const REMOTE_INVENTORY_HEIGHT := 4

@export var debug_session := false

var transport: Node = null
var command_server: Node = null
var role := ROLE_NONE
var assigned_player_handle := 0
var local_identity: Dictionary = {}
var server_identity: Dictionary = {}
var save_dir := ""

var _next_remote_player_handle := 2
var _remote_by_steam_id: Dictionary = {}
var _remote_by_player_handle: Dictionary = {}


func _ready() -> void:
	_ensure_transport()


func start_host(cmd_server: Node, identity: Dictionary,
		world_save_dir: String = "") -> bool:
	if cmd_server == null:
		push_warning("SteamLanSessionBridge: command_server is required for host")
		return false
	if not cmd_server.has_method(&"register_player") \
			or not cmd_server.has_method(&"submit_command") \
			or not cmd_server.has_method(&"unregister_player"):
		push_warning("SteamLanSessionBridge: command_server is missing multiplayer methods")
		return false
	if not _is_steam_identity(identity):
		push_warning("SteamLanSessionBridge: host requires Steam identity")
		return false
	if not _has_player_uuid(identity):
		push_warning("SteamLanSessionBridge: host identity missing player_uuid")
		return false

	_ensure_transport()
	command_server = cmd_server
	local_identity = identity.duplicate(true)
	server_identity = local_identity.duplicate(true)
	save_dir = world_save_dir
	role = ROLE_HOST
	assigned_player_handle = LOCAL_HOST_PLAYER_HANDLE
	_connect_transport()
	_connect_command_server_signals()

	var ok := bool(transport.call("start_host"))
	if not ok:
		role = ROLE_NONE
		return false
	var info: Dictionary = transport.call("get_connection_info")
	var local_steam_id := str(info.get("local_steam_id", ""))
	host_started.emit(local_steam_id)
	return true


func connect_to_host(host_steam_id: String, identity: Dictionary) -> bool:
	if not _is_steam_identity(identity):
		push_warning("SteamLanSessionBridge: Steam P2P client requires Steam identity")
		return false
	if not _has_player_uuid(identity):
		push_warning("SteamLanSessionBridge: Steam P2P client identity missing player_uuid")
		return false

	_ensure_transport()
	local_identity = identity.duplicate(true)
	role = ROLE_CLIENT
	assigned_player_handle = 0
	_connect_transport()

	var ok := bool(transport.call("connect_to_host", host_steam_id))
	if not ok:
		role = ROLE_NONE
		return false

	_send_login_request(host_steam_id)
	client_started.emit(host_steam_id)
	return true


func poll() -> void:
	if transport != null:
		transport.call("poll")


func stop() -> void:
	if transport != null:
		transport.call("stop")
	if role == ROLE_HOST:
		_unregister_all_remote_players()
	role = ROLE_NONE
	assigned_player_handle = 0
	command_server = null


func submit_command(command: Dictionary, client_tick: int = 0) -> bool:
	if role != ROLE_CLIENT or assigned_player_handle <= 0:
		return false
	if transport == null:
		return false
	var host_steam_id := str(transport.get("host_steam_id"))
	if host_steam_id == "":
		return false

	var command_copy := command.duplicate(true)
	command_copy["player_handle"] = assigned_player_handle
	var packet := _base_packet(PACKET_COMMAND)
	packet["client_tick"] = client_tick
	packet["command"] = command_copy
	return _send_packet(host_steam_id, packet, true)


func send_position(payload: Dictionary) -> bool:
	if role != ROLE_CLIENT or assigned_player_handle <= 0:
		return false
	var host_steam_id := str(transport.get("host_steam_id")) if transport != null else ""
	if host_steam_id == "":
		return false
	var packet := _base_packet(PACKET_POSITION)
	packet["player_handle"] = assigned_player_handle
	packet["payload"] = payload.duplicate(true)
	return _send_packet(host_steam_id, packet, false)


func get_remote_players() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for steam_id in _remote_by_steam_id.keys():
		result.append((_remote_by_steam_id[steam_id] as Dictionary).duplicate(true))
	return result


func _ensure_transport() -> void:
	if transport != null:
		return
	transport = SteamP2PTransportScript.new()
	transport.name = "SteamP2PTransport"
	add_child(transport)


func _connect_transport() -> void:
	if transport == null:
		return
	var packet_cb := Callable(self, "_on_transport_packet_received")
	if not transport.is_connected(&"packet_received", packet_cb):
		transport.connect(&"packet_received", packet_cb)
	var disconnected_cb := Callable(self, "_on_transport_peer_disconnected")
	if not transport.is_connected(&"peer_disconnected", disconnected_cb):
		transport.connect(&"peer_disconnected", disconnected_cb)
	var failed_cb := Callable(self, "_on_transport_connection_failed")
	if not transport.is_connected(&"connection_failed", failed_cb):
		transport.connect(&"connection_failed", failed_cb)


func _connect_command_server_signals() -> void:
	if command_server == null:
		return
	_connect_command_signal(&"terrain_cell_synced", "_on_terrain_cell_synced")
	_connect_command_signal(&"world_object_synced", "_on_world_object_synced")
	_connect_command_signal(&"crop_harvested", "_on_crop_harvested")


func _connect_command_signal(signal_name: StringName, method_name: String) -> void:
	if command_server == null or not command_server.has_signal(signal_name):
		return
	var cb := Callable(self, method_name)
	if not command_server.is_connected(signal_name, cb):
		command_server.connect(signal_name, cb)


func _on_transport_packet_received(remote_steam_id: String, _channel: int,
		data: PackedByteArray, _reliable: bool) -> void:
	var packet := _decode_packet(data)
	if packet.is_empty():
		return

	var packet_type := str(packet.get("packet", ""))
	if role == ROLE_HOST:
		_handle_host_packet(remote_steam_id, packet_type, packet)
	elif role == ROLE_CLIENT:
		_handle_client_packet(remote_steam_id, packet_type, packet)


func _handle_host_packet(remote_steam_id: String, packet_type: String,
		packet: Dictionary) -> void:
	match packet_type:
		PACKET_LOGIN_REQUEST:
			_handle_login_request(remote_steam_id, packet)
		PACKET_COMMAND:
			_handle_remote_command(remote_steam_id, packet)
		PACKET_POSITION:
			_handle_remote_position(remote_steam_id, packet)


func _handle_client_packet(_remote_steam_id: String, packet_type: String,
		packet: Dictionary) -> void:
	match packet_type:
		PACKET_LOGIN_ACCEPT:
			assigned_player_handle = int(packet.get("player_handle", 0))
			server_identity = packet.get("server_identity", {})
			login_accepted.emit(assigned_player_handle)
		PACKET_LOGIN_REJECT:
			login_rejected.emit(str(packet.get("reason", "login rejected")))
		PACKET_COMMAND_RESULT:
			var result: Dictionary = packet.get("result", {})
			command_result_received.emit(result)
		PACKET_SYNC_EVENT:
			var event_name := str(packet.get("event", ""))
			var payload: Dictionary = packet.get("payload", {})
			sync_event_received.emit(event_name, payload)
		PACKET_POSITION:
			var player_handle := int(packet.get("player_handle", 0))
			var payload: Dictionary = packet.get("payload", {})
			position_received.emit(player_handle, payload)


func _handle_login_request(remote_steam_id: String, packet: Dictionary) -> void:
	var identity: Dictionary = packet.get("identity", {})
	var reject_reason := _validate_remote_identity(remote_steam_id, identity)
	if reject_reason != "":
		_send_login_reject(remote_steam_id, reject_reason)
		return

	if _remote_by_steam_id.has(remote_steam_id):
		var existing: Dictionary = _remote_by_steam_id[remote_steam_id]
		_send_login_accept(remote_steam_id, int(existing.get("player_handle", 0)))
		return

	var player_handle := _next_remote_player_handle
	_next_remote_player_handle += 1

	var inventory := GDPlayerInventory.new()
	inventory.init(REMOTE_INVENTORY_WIDTH, REMOTE_INVENTORY_HEIGHT)
	var equipment := GDPlayerEquipment.new()
	_load_remote_player_data(identity, inventory, equipment)

	if command_server == null or not bool(command_server.call(
			"register_player", player_handle, inventory, equipment)):
		_send_login_reject(remote_steam_id, "failed to register player")
		return

	var remote := {
		"steam_id": remote_steam_id,
		"player_handle": player_handle,
		"player_uuid": str(identity.get("player_uuid", "")),
		"identity": identity.duplicate(true),
		"inventory": inventory,
		"equipment": equipment,
		"last_position": {},
	}
	_remote_by_steam_id[remote_steam_id] = remote
	_remote_by_player_handle[player_handle] = remote

	_send_login_accept(remote_steam_id, player_handle)
	remote_player_connected.emit(player_handle, remote_steam_id, identity)
	if debug_session:
		print("[SteamLanSessionBridge] remote player connected handle=%d steam=%s" %
			[player_handle, remote_steam_id])


func _handle_remote_command(remote_steam_id: String, packet: Dictionary) -> void:
	if command_server == null:
		return
	if not _remote_by_steam_id.has(remote_steam_id):
		_send_login_reject(remote_steam_id, "not logged in")
		return

	var remote: Dictionary = _remote_by_steam_id[remote_steam_id]
	var command: Dictionary = packet.get("command", {})
	command["player_handle"] = int(remote.get("player_handle", 0))
	var result: Variant = command_server.call("submit_command", command)
	var response := _base_packet(PACKET_COMMAND_RESULT)
	response["client_tick"] = int(packet.get("client_tick", 0))
	response["result"] = result
	_send_packet(remote_steam_id, response, true)


func _handle_remote_position(remote_steam_id: String, packet: Dictionary) -> void:
	if not _remote_by_steam_id.has(remote_steam_id):
		return
	var remote: Dictionary = _remote_by_steam_id[remote_steam_id]
	var payload: Dictionary = packet.get("payload", {})
	remote["last_position"] = payload.duplicate(true)
	_remote_by_steam_id[remote_steam_id] = remote
	var player_handle := int(remote.get("player_handle", 0))
	position_received.emit(player_handle, payload)

	var relay := _base_packet(PACKET_POSITION)
	relay["player_handle"] = player_handle
	relay["payload"] = payload
	_broadcast_except(remote_steam_id, relay, false)


func _on_transport_peer_disconnected(remote_steam_id: String) -> void:
	_unregister_remote_player(remote_steam_id)


func _on_transport_connection_failed(remote_steam_id: String, reason: String) -> void:
	if role == ROLE_CLIENT:
		login_rejected.emit(reason)
	else:
		_unregister_remote_player(remote_steam_id)


func _unregister_remote_player(remote_steam_id: String) -> void:
	if not _remote_by_steam_id.has(remote_steam_id):
		return
	var remote: Dictionary = _remote_by_steam_id[remote_steam_id]
	var player_handle := int(remote.get("player_handle", 0))
	_save_remote_player_data(remote)
	if command_server != null and player_handle > 0:
		command_server.call("unregister_player", player_handle)
	_remote_by_steam_id.erase(remote_steam_id)
	_remote_by_player_handle.erase(player_handle)
	remote_player_disconnected.emit(player_handle, remote_steam_id)


func _unregister_all_remote_players() -> void:
	var steam_ids: Array[String] = []
	for steam_id in _remote_by_steam_id.keys():
		steam_ids.append(str(steam_id))
	for steam_id in steam_ids:
		_unregister_remote_player(steam_id)


func _load_remote_player_data(identity: Dictionary, inventory: GDPlayerInventory,
		equipment: GDPlayerEquipment) -> void:
	if save_dir == "":
		return
	var service: Node = get_node_or_null(^"/root/PlayerSaveService")
	if service == null:
		return
	var value: Variant = service.call("load_player", save_dir, identity)
	if not (value is Dictionary):
		return
	var data: Dictionary = value
	if data.is_empty():
		return
	service.call("apply_inventory", inventory, data.get("inventory", {}))
	service.call("apply_equipment", equipment, data.get("equipment", {}))


func _save_remote_player_data(remote: Dictionary) -> void:
	if save_dir == "":
		return
	var service: Node = get_node_or_null(^"/root/PlayerSaveService")
	if service == null:
		return
	var identity: Dictionary = remote.get("identity", {})
	var inventory_value: Variant = service.call(
		"export_inventory", remote.get("inventory", null))
	var equipment_value: Variant = service.call(
		"export_equipment", remote.get("equipment", null))
	var inventory_data: Dictionary = inventory_value if inventory_value is Dictionary else {}
	var equipment_data: Dictionary = equipment_value if equipment_value is Dictionary else {}
	var data := {
		"identity": identity.duplicate(true),
		"position": remote.get("last_position", {}),
		"inventory": inventory_data,
		"equipment": equipment_data,
	}
	service.call("save_player", save_dir, identity, data)


func _send_login_request(remote_steam_id: String) -> void:
	var packet := _base_packet(PACKET_LOGIN_REQUEST)
	packet["identity"] = local_identity.duplicate(true)
	_send_packet(remote_steam_id, packet, true)


func _send_login_accept(remote_steam_id: String, player_handle: int) -> void:
	var packet := _base_packet(PACKET_LOGIN_ACCEPT)
	packet["player_handle"] = player_handle
	packet["server_identity"] = server_identity.duplicate(true)
	_send_packet(remote_steam_id, packet, true)


func _send_login_reject(remote_steam_id: String, reason: String) -> void:
	var packet := _base_packet(PACKET_LOGIN_REJECT)
	packet["reason"] = reason
	_send_packet(remote_steam_id, packet, true)


func _broadcast_sync_event(event_name: String, payload: Dictionary) -> void:
	if role != ROLE_HOST:
		return
	var packet := _base_packet(PACKET_SYNC_EVENT)
	packet["event"] = event_name
	packet["payload"] = payload
	_broadcast(packet, true)


func _broadcast(packet: Dictionary, reliable: bool) -> int:
	var count := 0
	for steam_id in _remote_by_steam_id.keys():
		if _send_packet(str(steam_id), packet, reliable):
			count += 1
	return count


func _broadcast_except(excluded_steam_id: String, packet: Dictionary,
		reliable: bool) -> int:
	var count := 0
	for steam_id in _remote_by_steam_id.keys():
		var steam_id_str := str(steam_id)
		if steam_id_str == excluded_steam_id:
			continue
		if _send_packet(steam_id_str, packet, reliable):
			count += 1
	return count


func _send_packet(remote_steam_id: String, packet: Dictionary, reliable: bool) -> bool:
	if transport == null:
		return false
	var bytes := _encode_packet(packet)
	if reliable:
		return bool(transport.call("send_reliable", remote_steam_id, bytes))
	return bool(transport.call("send_unreliable", remote_steam_id, bytes))


func _base_packet(packet_type: String) -> Dictionary:
	return {
		"packet": packet_type,
		"protocol_version": PROTOCOL_VERSION,
	}


func _encode_packet(packet: Dictionary) -> PackedByteArray:
	return var_to_bytes(packet)


func _decode_packet(bytes: PackedByteArray) -> Dictionary:
	var value: Variant = bytes_to_var(bytes)
	if value is Dictionary:
		return value
	return {}


func _validate_remote_identity(remote_steam_id: String, identity: Dictionary) -> String:
	if not _is_steam_identity(identity):
		return "Steam P2P requires Steam identity"
	if not _has_player_uuid(identity):
		return "missing player uuid"
	var account_id := str(identity.get("account_id", ""))
	if account_id != remote_steam_id:
		return "Steam identity mismatch"
	return ""


func _is_steam_identity(identity: Dictionary) -> bool:
	return str(identity.get("provider", "")) == "steam" \
			and str(identity.get("account_id", "")) != ""


func _has_player_uuid(identity: Dictionary) -> bool:
	return _is_uuid(str(identity.get("player_uuid", "")))


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


func _on_terrain_cell_synced(dimension: StringName, chunk: Vector3i,
		local: Vector3i, old_material: int, new_material: int) -> void:
	_broadcast_sync_event("terrain_cell_synced", {
		"dimension": String(dimension),
		"chunk": chunk,
		"local": local,
		"old_material": old_material,
		"new_material": new_material,
	})


func _on_world_object_synced(kind: StringName, action: StringName,
		dimension: StringName, cell: Vector3i) -> void:
	_broadcast_sync_event("world_object_synced", {
		"kind": String(kind),
		"action": String(action),
		"dimension": String(dimension),
		"cell": cell,
	})


func _on_crop_harvested(dimension: StringName, cell: Vector3i,
		species_key: String, crop_count: int, crop_item_key: String,
		byproduct_item_key: String, byproduct_count: int) -> void:
	_broadcast_sync_event("crop_harvested", {
		"dimension": String(dimension),
		"cell": cell,
		"species_key": species_key,
		"crop_count": crop_count,
		"crop_item_key": crop_item_key,
		"byproduct_item_key": byproduct_item_key,
		"byproduct_count": byproduct_count,
	})
