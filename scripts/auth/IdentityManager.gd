class_name IdentityManagerService
extends Node

const PROVIDER_STEAM := "steam"
const PROVIDER_OFFLINE := "offline"
const CHANNEL_STEAM := "steam"
const CHANNEL_OFFLINE := "offline"
const LAN_TRANSPORT_DIRECT := "direct_lan"
const LAN_TRANSPORT_STEAM_P2P := "steam_p2p"
const DEDICATED_TRANSPORT_DIRECT := "direct_dedicated"

const PROFILE_DIR := "user://identity/"
const OFFLINE_PROFILE_PATH := PROFILE_DIR + "offline_profile.json"
const BUILD_CHANNEL_SETTING := "snt/identity/build_channel"
const UUID_HEX_CHARS := "0123456789abcdef"

@export var debug_identity := false

var _identity: Dictionary = {}
var _offline_uuid: String = ""


func _ready() -> void:
	_resolve_identity()


func get_identity() -> Dictionary:
	if _identity.is_empty():
		_resolve_identity()
	return _identity.duplicate(true)


func get_build_channel() -> String:
	if _identity.is_empty():
		_resolve_identity()
	return str(_identity.get("build_channel", CHANNEL_OFFLINE))


func get_player_save_key() -> String:
	return get_player_uuid()


func get_player_uuid() -> String:
	if _identity.is_empty():
		_resolve_identity()
	return str(_identity.get("player_uuid", ""))


func can_host_lan() -> bool:
	if _identity.is_empty():
		_resolve_identity()
	return bool(_identity.get("can_host_lan", false))


func supports_steam_lan_transport() -> bool:
	if _identity.is_empty():
		_resolve_identity()
	return bool(_identity.get("supports_steam_lan_transport", false))


func get_lan_transport_modes() -> Array[String]:
	if not can_host_lan():
		return []
	var modes: Array[String] = [LAN_TRANSPORT_DIRECT]
	if supports_steam_lan_transport():
		modes.append(LAN_TRANSPORT_STEAM_P2P)
	return modes


func get_preferred_lan_transport() -> String:
	if supports_steam_lan_transport():
		return LAN_TRANSPORT_STEAM_P2P
	if can_host_lan():
		return LAN_TRANSPORT_DIRECT
	return ""


func get_dedicated_transport_mode() -> String:
	return DEDICATED_TRANSPORT_DIRECT


func get_lan_host_block_reason() -> String:
	if can_host_lan():
		return ""
	return "LAN hosting requires a resolved Steam account identity."


func build_login_identity() -> Dictionary:
	var identity := get_identity()
	return {
		"identity_provider": str(identity.get("provider", PROVIDER_OFFLINE)),
		"account_id": str(identity.get("account_id", "")),
		"display_name": str(identity.get("display_name", "")),
		"build_channel": str(identity.get("build_channel", CHANNEL_OFFLINE)),
		"player_uuid": str(identity.get("player_uuid", "")),
		"player_key": str(identity.get("player_key", "")),
		"supports_steam_lan_transport": bool(identity.get(
			"supports_steam_lan_transport", false)),
	}


func _resolve_identity() -> void:
	var build_channel := _detect_build_channel()
	var steam_id := _get_steam_account_id()
	var display_name := _get_display_name()

	var provider := PROVIDER_OFFLINE
	var account_id := _load_or_create_offline_uuid()
	var can_lan := false
	var supports_steam_lan := false

	if build_channel == CHANNEL_STEAM and steam_id != "":
		provider = PROVIDER_STEAM
		account_id = steam_id
		can_lan = true
		supports_steam_lan = _has_steam_networking_api()

	var player_uuid := _make_player_uuid(provider, account_id)
	var player_key := player_uuid
	var lan_transport_modes: Array[String] = []
	if can_lan:
		lan_transport_modes.append(LAN_TRANSPORT_DIRECT)
		if supports_steam_lan:
			lan_transport_modes.append(LAN_TRANSPORT_STEAM_P2P)
	_identity = {
		"provider": provider,
		"account_id": account_id,
		"display_name": display_name,
		"build_channel": build_channel,
		"player_uuid": player_uuid,
		"player_key": player_key,
		"can_host_lan": can_lan,
		"supports_steam_lan_transport": supports_steam_lan,
		"lan_transport_modes": lan_transport_modes,
	}

	if debug_identity:
		print("[IdentityManager] provider=%s channel=%s uuid=%s can_host_lan=%s steam_lan=%s" % [
			provider, build_channel, player_uuid, str(can_lan),
			str(supports_steam_lan)])


func _detect_build_channel() -> String:
	var env_channel := OS.get_environment("SNT_BUILD_CHANNEL").strip_edges().to_lower()
	if env_channel == CHANNEL_STEAM or env_channel == CHANNEL_OFFLINE:
		return env_channel

	if ProjectSettings.has_setting(BUILD_CHANNEL_SETTING):
		var configured := str(ProjectSettings.get_setting(BUILD_CHANNEL_SETTING)).to_lower()
		if configured == CHANNEL_STEAM or configured == CHANNEL_OFFLINE:
			return configured

	if OS.has_feature("steam"):
		return CHANNEL_STEAM

	return CHANNEL_OFFLINE


func _get_steam_account_id() -> String:
	var env_id := OS.get_environment("SNT_STEAM_ID").strip_edges()
	if env_id != "":
		return env_id

	var steam := get_node_or_null(^"/root/Steam")
	if steam == null:
		return ""

	for method_name in [&"getSteamID", &"get_steam_id", &"getAccountID", &"get_account_id"]:
		if steam.has_method(method_name):
			var value: Variant = steam.call(method_name)
			var text := str(value).strip_edges()
			if text != "" and text != "0":
				return text

	return ""


func _get_display_name() -> String:
	var env_name := OS.get_environment("SNT_PLAYER_NAME").strip_edges()
	if env_name != "":
		return env_name

	var steam := get_node_or_null(^"/root/Steam")
	if steam != null:
		for method_name in [&"getPersonaName", &"get_persona_name"]:
			if steam.has_method(method_name):
				var value: Variant = steam.call(method_name)
				var text := str(value).strip_edges()
				if text != "":
					return text

	var user_name := OS.get_environment("USERNAME").strip_edges()
	if user_name != "":
		return user_name
	return "Player"


func _has_steam_networking_api() -> bool:
	var env_value := OS.get_environment("SNT_STEAM_NETWORKING").strip_edges().to_lower()
	if env_value == "1" or env_value == "true" or env_value == "yes":
		return true
	if env_value == "0" or env_value == "false" or env_value == "no":
		return false

	var steam := get_node_or_null(^"/root/Steam")
	if steam == null:
		return false

	var has_send := steam.has_method(&"sendP2PPacket") \
			or steam.has_method(&"send_p2p_packet")
	var has_available := steam.has_method(&"isP2PPacketAvailable") \
			or steam.has_method(&"is_p2p_packet_available")
	var has_read := steam.has_method(&"readP2PPacket") \
			or steam.has_method(&"read_p2p_packet")
	return has_send and has_available and has_read


func _load_or_create_offline_uuid() -> String:
	if _offline_uuid != "":
		return _offline_uuid

	if FileAccess.file_exists(OFFLINE_PROFILE_PATH):
		var file := FileAccess.open(OFFLINE_PROFILE_PATH, FileAccess.READ)
		if file != null:
			var text := file.get_as_text()
			file.close()
			var json := JSON.new()
			if json.parse(text) == OK and json.data is Dictionary:
				var data: Dictionary = json.data
				var saved_uuid := str(data.get("offline_uuid", "")).strip_edges()
				if saved_uuid != "":
					_offline_uuid = saved_uuid
					return _offline_uuid

	if not DirAccess.dir_exists_absolute(PROFILE_DIR):
		DirAccess.make_dir_recursive_absolute(PROFILE_DIR)

	_offline_uuid = _generate_uuid()
	var profile := {
		"offline_uuid": _offline_uuid,
		"created_at": Time.get_datetime_string_from_system(),
	}
	var out := FileAccess.open(OFFLINE_PROFILE_PATH, FileAccess.WRITE)
	if out != null:
		out.store_string(JSON.stringify(profile, "\t"))
		out.close()
	else:
		push_warning("IdentityManager: failed to write offline identity profile")

	return _offline_uuid


func _generate_uuid() -> String:
	var rng := RandomNumberGenerator.new()
	rng.randomize()
	var part3 := _hex(rng.randi(), 4)
	part3 = "4" + part3.substr(1, 3)
	var part4 := _hex(rng.randi(), 4)
	var variant_prefix := "89ab".substr(rng.randi_range(0, 3), 1)
	part4 = variant_prefix + part4.substr(1, 3)
	return "%s-%s-%s-%s-%s%s%s" % [
		_hex(rng.randi(), 8),
		_hex(rng.randi(), 4),
		part3,
		part4,
		_hex(rng.randi(), 4),
		_hex(rng.randi(), 4),
		_hex(rng.randi(), 4),
	]


func _hex(value: int, width: int) -> String:
	var text := "%x" % value
	while text.length() < width:
		text = "0" + text
	if text.length() > width:
		text = text.substr(text.length() - width, width)
	return text


func _make_player_uuid(provider: String, account_id: String) -> String:
	if provider == PROVIDER_OFFLINE and _is_uuid(account_id):
		return account_id.to_lower()
	return _make_deterministic_uuid("%s:%s" % [provider, account_id])


func _make_deterministic_uuid(input: String) -> String:
	var ctx := HashingContext.new()
	var err := ctx.start(HashingContext.HASH_SHA256)
	if err != OK:
		return _generate_uuid()
	ctx.update(input.to_utf8_buffer())
	var digest := ctx.finish()
	var hex_text := _bytes_to_hex(digest)
	if hex_text.length() < 32:
		return _generate_uuid()
	var uuid_hex := hex_text.substr(0, 32)
	uuid_hex = uuid_hex.substr(0, 12) + "5" + uuid_hex.substr(13)
	var variant_index := UUID_HEX_CHARS.find(uuid_hex.substr(16, 1).to_lower())
	if variant_index < 0:
		variant_index = 0
	var variant_char := "89ab".substr(variant_index % 4, 1)
	uuid_hex = uuid_hex.substr(0, 16) + variant_char + uuid_hex.substr(17)
	return "%s-%s-%s-%s-%s" % [
		uuid_hex.substr(0, 8),
		uuid_hex.substr(8, 4),
		uuid_hex.substr(12, 4),
		uuid_hex.substr(16, 4),
		uuid_hex.substr(20, 12),
	]


func _bytes_to_hex(bytes: PackedByteArray) -> String:
	var result := ""
	for value in bytes:
		result += _hex(int(value), 2)
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
		elif UUID_HEX_CHARS.find(ch) < 0:
			return false
	return true


func _sanitize_key(raw: String) -> String:
	var result := ""
	for i in range(raw.length()):
		var ch := raw.substr(i, 1)
		if ch.is_valid_identifier() or ch.is_valid_int() or ch == "-" or ch == "_":
			result += ch
		else:
			result += "_"
	return result
