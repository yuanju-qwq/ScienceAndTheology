class_name GodotSteamP2PAdapter
extends Node

signal session_requested(remote_steam_id: String)
signal session_failed(remote_steam_id: String, reason: String)

const DEFAULT_RELIABLE_SEND_TYPE := 2
const DEFAULT_UNRELIABLE_SEND_TYPE := 1

@export var debug_steam_p2p := false
@export var auto_accept_sessions := false

var _steam: Object = null
var _signals_connected := false


func _ready() -> void:
	setup()


func setup(steam_object: Object = null) -> bool:
	var next_steam: Object = steam_object if steam_object != null else get_node_or_null(^"/root/Steam")
	if next_steam != _steam:
		_signals_connected = false
	_steam = next_steam
	if _steam == null:
		if debug_steam_p2p:
			print("[GodotSteamP2PAdapter] Steam singleton not found")
		return false

	_connect_signals()
	if has_method_name(&"allowP2PPacketRelay"):
		_call_steam(&"allowP2PPacketRelay", [true])
	return is_available()


func is_available() -> bool:
	return _steam != null and has_legacy_p2p()


func has_legacy_p2p() -> bool:
	return _get_send_packet_method() != &"" \
			and _get_packet_available_method() != &"" \
			and _get_read_packet_method() != &""


func run_callbacks() -> void:
	if _steam == null:
		return
	for method_name in [&"run_callbacks", &"runCallbacks"]:
		if has_method_name(method_name):
			_call_steam(method_name)
			return


func get_local_steam_id() -> String:
	if _steam == null:
		return ""
	for method_name in [&"getSteamID", &"get_steam_id", &"getAccountID", &"get_account_id"]:
		if has_method_name(method_name):
			var value: Variant = _call_steam(method_name)
			var text := str(value).strip_edges()
			if text != "" and text != "0":
				return text
	return ""


func accept_session(remote_steam_id: String) -> bool:
	if _steam == null or remote_steam_id == "":
		return false
	if has_method_name(&"acceptP2PSessionWithUser"):
		return bool(_call_steam(&"acceptP2PSessionWithUser", [_parse_steam_id(remote_steam_id)]))
	if has_method_name(&"accept_p2p_session_with_user"):
		return bool(_call_steam(&"accept_p2p_session_with_user", [_parse_steam_id(remote_steam_id)]))
	return true


func close_session(remote_steam_id: String) -> void:
	if _steam == null or remote_steam_id == "":
		return
	if has_method_name(&"closeP2PSessionWithUser"):
		_call_steam(&"closeP2PSessionWithUser", [_parse_steam_id(remote_steam_id)])
	elif has_method_name(&"close_p2p_session_with_user"):
		_call_steam(&"close_p2p_session_with_user", [_parse_steam_id(remote_steam_id)])


func send_packet(remote_steam_id: String, data: PackedByteArray,
		channel: int = 0, reliable: bool = true) -> bool:
	if not is_available() or remote_steam_id == "" or data.is_empty():
		return false
	var send_type := _get_send_type(reliable)
	var result: Variant = _call_steam(_get_send_packet_method(), [
		_parse_steam_id(remote_steam_id),
		data,
		send_type,
		channel,
	])
	return bool(result)


func read_packets(channel: int = 0, max_packets: int = 64) -> Array[Dictionary]:
	var packets: Array[Dictionary] = []
	if not is_available():
		return packets

	var count := 0
	var available_method: StringName = _get_packet_available_method()
	var read_method: StringName = _get_read_packet_method()
	while count < max_packets:
		var available: Variant = _call_steam(available_method, [channel])
		var packet_size := _extract_available_size(available)
		if packet_size <= 0:
			break

		var raw_packet: Variant = _call_steam(read_method, [packet_size, channel])
		var packet: Dictionary = _normalize_packet(raw_packet, channel)
		if not packet.is_empty():
			packets.append(packet)
		count += 1

	return packets


func has_method_name(method_name: StringName) -> bool:
	return _steam != null and _steam.has_method(method_name)


func _connect_signals() -> void:
	if _steam == null or _signals_connected:
		return

	if _steam.has_signal(&"p2p_session_request"):
		var cb := Callable(self, "_on_p2p_session_request")
		if not _steam.is_connected(&"p2p_session_request", cb):
			_steam.connect(&"p2p_session_request", cb)

	if _steam.has_signal(&"p2p_session_connect_fail"):
		var fail_cb := Callable(self, "_on_p2p_session_connect_fail")
		if not _steam.is_connected(&"p2p_session_connect_fail", fail_cb):
			_steam.connect(&"p2p_session_connect_fail", fail_cb)

	_signals_connected = true


func _on_p2p_session_request(remote_id: Variant) -> void:
	var remote := str(remote_id)
	if auto_accept_sessions:
		accept_session(remote)
	if debug_steam_p2p:
		print("[GodotSteamP2PAdapter] session request from %s" % remote)
	session_requested.emit(remote)


func _on_p2p_session_connect_fail(remote_id: Variant, error_code: Variant = "") -> void:
	var remote := str(remote_id)
	var reason := str(error_code)
	if debug_steam_p2p:
		print("[GodotSteamP2PAdapter] session failed remote=%s reason=%s" %
			[remote, reason])
	session_failed.emit(remote, reason)


func _call_steam(method_name: StringName, args: Array = []) -> Variant:
	if _steam == null or not _steam.has_method(method_name):
		return null
	return _steam.callv(method_name, args)


func _get_send_type(reliable: bool) -> int:
	if reliable:
		return _get_steam_constant("P2P_SEND_RELIABLE", DEFAULT_RELIABLE_SEND_TYPE)
	return _get_steam_constant("P2P_SEND_UNRELIABLE_NO_DELAY",
		DEFAULT_UNRELIABLE_SEND_TYPE)


func _get_send_packet_method() -> StringName:
	for method_name in [&"sendP2PPacket", &"send_p2p_packet"]:
		if has_method_name(method_name):
			return method_name
	return &""


func _get_packet_available_method() -> StringName:
	for method_name in [&"isP2PPacketAvailable", &"is_p2p_packet_available"]:
		if has_method_name(method_name):
			return method_name
	return &""


func _get_read_packet_method() -> StringName:
	for method_name in [&"readP2PPacket", &"read_p2p_packet"]:
		if has_method_name(method_name):
			return method_name
	return &""


func _get_steam_constant(name: String, fallback: int) -> int:
	if _steam == null:
		return fallback
	var value: Variant = _steam.get(name)
	if value == null:
		return fallback
	return int(value)


func _extract_available_size(value: Variant) -> int:
	match typeof(value):
		TYPE_INT, TYPE_FLOAT:
			return int(value)
		TYPE_DICTIONARY:
			var data: Dictionary = value
			if data.has("size"):
				return int(data.get("size", 0))
			if data.has("packet_size"):
				return int(data.get("packet_size", 0))
			if data.has("available") and not bool(data.get("available", false)):
				return 0
	return 0


func _normalize_packet(value: Variant, channel: int) -> Dictionary:
	if not (value is Dictionary):
		return {}

	var packet: Dictionary = value
	var data: PackedByteArray = PackedByteArray()
	var raw_data: Variant = packet.get("data", PackedByteArray())
	if raw_data is PackedByteArray:
		data = raw_data
	elif raw_data is Array:
		for byte_value in raw_data:
			data.append(int(byte_value) & 0xFF)

	if data.is_empty():
		return {}

	var remote := ""
	for key in [
		"steam_id_remote",
		"steamIDRemote",
		"remote_steam_id",
		"remoteSteamID",
		"steam_id",
		"steamID",
		"remote_id",
		"id",
	]:
		if packet.has(key):
			remote = str(packet.get(key))
			break

	return {
		"remote_steam_id": remote,
		"channel": int(packet.get("channel", channel)),
		"data": data,
	}


func _parse_steam_id(value: String) -> Variant:
	if value.is_valid_int():
		return int(value)
	return value
