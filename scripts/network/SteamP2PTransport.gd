class_name SteamP2PTransport
extends Node

const GodotSteamP2PAdapterScript := preload("res://scripts/network/GodotSteamP2PAdapter.gd")

signal started(role: String)
signal stopped
signal peer_connected(remote_steam_id: String)
signal peer_disconnected(remote_steam_id: String)
signal connection_failed(remote_steam_id: String, reason: String)
signal packet_received(remote_steam_id: String, channel: int,
		data: PackedByteArray, reliable: bool)

const ROLE_NONE := "none"
const ROLE_HOST := "host"
const ROLE_CLIENT := "client"

const CHANNEL_RELIABLE := 0
const CHANNEL_UNRELIABLE := 1
const DEFAULT_PEER_TIMEOUT_SEC := 30.0

@export var debug_transport := false
@export var peer_timeout_sec := DEFAULT_PEER_TIMEOUT_SEC

var adapter: Node = null
var role := ROLE_NONE
var host_steam_id := ""
var local_steam_id := ""
var _peers: Dictionary = {}
var _running := false


func _ready() -> void:
	if adapter == null:
		adapter = GodotSteamP2PAdapterScript.new()
		adapter.name = "GodotSteamP2PAdapter"
		add_child(adapter)
	_connect_adapter()


func is_available() -> bool:
	_ensure_adapter()
	return adapter != null and bool(adapter.call("setup"))


func start_host(allowed_peers: Array[String] = []) -> bool:
	_ensure_adapter()
	if adapter == null or not bool(adapter.call("setup")):
		push_warning("SteamP2PTransport: GodotSteam P2P is not available")
		return false

	local_steam_id = str(adapter.call("get_local_steam_id"))
	if local_steam_id == "":
		push_warning("SteamP2PTransport: local Steam ID is unavailable")
		return false

	role = ROLE_HOST
	host_steam_id = local_steam_id
	_running = true
	adapter.set("auto_accept_sessions", true)
	_peers.clear()
	for peer_id in allowed_peers:
		if peer_id != "":
			_register_peer(peer_id, false)
	if debug_transport:
		print("[SteamP2PTransport] host started steam_id=%s" % local_steam_id)
	started.emit(role)
	return true


func connect_to_host(remote_host_steam_id: String) -> bool:
	_ensure_adapter()
	if adapter == null or not bool(adapter.call("setup")):
		push_warning("SteamP2PTransport: GodotSteam P2P is not available")
		return false

	local_steam_id = str(adapter.call("get_local_steam_id"))
	if local_steam_id == "":
		push_warning("SteamP2PTransport: local Steam ID is unavailable")
		return false
	if remote_host_steam_id.strip_edges() == "":
		push_warning("SteamP2PTransport: host Steam ID is empty")
		return false

	role = ROLE_CLIENT
	host_steam_id = remote_host_steam_id.strip_edges()
	_running = true
	adapter.set("auto_accept_sessions", false)
	_register_peer(host_steam_id, true)

	# Send a small reliable hello so Steam starts the P2P session negotiation.
	var hello := PackedByteArray()
	hello.append_array("SNT_STEAM_P2P_HELLO".to_utf8_buffer())
	var ok := bool(adapter.call("send_packet",
		host_steam_id, hello, CHANNEL_RELIABLE, true))
	if debug_transport:
		print("[SteamP2PTransport] client connecting local=%s host=%s sent_hello=%s" %
			[local_steam_id, host_steam_id, str(ok)])
	started.emit(role)
	return ok


func poll() -> void:
	if not _running or adapter == null:
		return

	adapter.call("run_callbacks")
	_poll_channel(CHANNEL_RELIABLE, true)
	_poll_channel(CHANNEL_UNRELIABLE, false)
	_expire_peers()


func send_reliable(remote_steam_id: String, data: PackedByteArray) -> bool:
	return _send(remote_steam_id, data, CHANNEL_RELIABLE, true)


func send_unreliable(remote_steam_id: String, data: PackedByteArray) -> bool:
	return _send(remote_steam_id, data, CHANNEL_UNRELIABLE, false)


func broadcast_reliable(data: PackedByteArray) -> int:
	return _broadcast(data, CHANNEL_RELIABLE, true)


func broadcast_unreliable(data: PackedByteArray) -> int:
	return _broadcast(data, CHANNEL_UNRELIABLE, false)


func disconnect_peer(remote_steam_id: String) -> void:
	if adapter != null:
		adapter.call("close_session", remote_steam_id)
	var existed := _peers.has(remote_steam_id)
	_peers.erase(remote_steam_id)
	if existed:
		peer_disconnected.emit(remote_steam_id)


func stop() -> void:
	if not _running:
		return
	for peer_id in _peers.keys():
		if adapter != null:
			adapter.call("close_session", str(peer_id))
	_peers.clear()
	_running = false
	role = ROLE_NONE
	host_steam_id = ""
	stopped.emit()


func get_peer_ids() -> Array[String]:
	var result: Array[String] = []
	for peer_id in _peers.keys():
		result.append(str(peer_id))
	return result


func has_peer(remote_steam_id: String) -> bool:
	return _peers.has(remote_steam_id)


func get_connection_info() -> Dictionary:
	return {
		"role": role,
		"running": _running,
		"local_steam_id": local_steam_id,
		"host_steam_id": host_steam_id,
		"peer_count": _peers.size(),
		"peers": get_peer_ids(),
	}


func _ensure_adapter() -> void:
	if adapter != null:
		return
	adapter = GodotSteamP2PAdapterScript.new()
	adapter.name = "GodotSteamP2PAdapter"
	add_child(adapter)
	_connect_adapter()


func _connect_adapter() -> void:
	if adapter == null:
		return
	adapter.set("auto_accept_sessions", role == ROLE_HOST)
	var request_cb := Callable(self, "_on_session_requested")
	if adapter.has_signal(&"session_requested") \
			and not adapter.is_connected(&"session_requested", request_cb):
		adapter.connect(&"session_requested", request_cb)
	var fail_cb := Callable(self, "_on_session_failed")
	if adapter.has_signal(&"session_failed") \
			and not adapter.is_connected(&"session_failed", fail_cb):
		adapter.connect(&"session_failed", fail_cb)


func _on_session_requested(remote_steam_id: String) -> void:
	if role != ROLE_HOST:
		return
	if adapter != null:
		adapter.call("accept_session", remote_steam_id)
	_register_peer(remote_steam_id, true)


func _on_session_failed(remote_steam_id: String, reason: String) -> void:
	var existed := _peers.has(remote_steam_id)
	_peers.erase(remote_steam_id)
	if existed:
		peer_disconnected.emit(remote_steam_id)
	connection_failed.emit(remote_steam_id, reason)


func _send(remote_steam_id: String, data: PackedByteArray,
		channel: int, reliable: bool) -> bool:
	if not _running or adapter == null or remote_steam_id == "" or data.is_empty():
		return false
	var ok := bool(adapter.call("send_packet", remote_steam_id, data, channel, reliable))
	if ok:
		_register_peer(remote_steam_id, true)
	elif debug_transport:
		print("[SteamP2PTransport] send failed remote=%s channel=%d" %
			[remote_steam_id, channel])
	return ok


func _broadcast(data: PackedByteArray, channel: int, reliable: bool) -> int:
	var sent := 0
	for peer_id in _peers.keys():
		if _send(str(peer_id), data, channel, reliable):
			sent += 1
	return sent


func _poll_channel(channel: int, reliable: bool) -> void:
	var packets: Array = adapter.call("read_packets", channel)
	for packet in packets:
		var remote := str(packet.get("remote_steam_id", ""))
		var data: PackedByteArray = packet.get("data", PackedByteArray())
		if remote == "" or data.is_empty():
			continue
		_register_peer(remote, true)
		if _is_internal_packet(data):
			continue
		packet_received.emit(remote, channel, data, reliable)


func _register_peer(remote_steam_id: String, connected: bool) -> void:
	var now := Time.get_ticks_msec()
	var was_known := _peers.has(remote_steam_id)
	var entry: Dictionary = _peers.get(remote_steam_id, {})
	entry["connected"] = connected
	entry["last_seen_msec"] = now
	_peers[remote_steam_id] = entry
	if connected and not was_known:
		if debug_transport:
			print("[SteamP2PTransport] peer connected %s" % remote_steam_id)
		peer_connected.emit(remote_steam_id)


func _expire_peers() -> void:
	if peer_timeout_sec <= 0.0:
		return
	var now := Time.get_ticks_msec()
	var timeout_msec := int(peer_timeout_sec * 1000.0)
	var expired: Array[String] = []
	for peer_id in _peers.keys():
		var entry: Dictionary = _peers[peer_id]
		var last_seen := int(entry.get("last_seen_msec", now))
		if now - last_seen > timeout_msec:
			expired.append(str(peer_id))
	for peer_id in expired:
		disconnect_peer(peer_id)


func _is_internal_packet(data: PackedByteArray) -> bool:
	if data.size() != 19:
		return false
	return data.get_string_from_utf8() == "SNT_STEAM_P2P_HELLO"
