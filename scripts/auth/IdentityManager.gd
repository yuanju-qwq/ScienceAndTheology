class_name IdentityManagerService
extends Node

const PROVIDER_STEAM := "steam"
const PROVIDER_OFFLINE := "offline"
const CHANNEL_STEAM := "steam"
const CHANNEL_OFFLINE := "offline"

const PROFILE_DIR := "user://identity/"
const OFFLINE_PROFILE_PATH := PROFILE_DIR + "offline_profile.json"
const BUILD_CHANNEL_SETTING := "snt/identity/build_channel"

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
	if _identity.is_empty():
		_resolve_identity()
	return str(_identity.get("player_key", ""))


func can_host_lan() -> bool:
	if _identity.is_empty():
		_resolve_identity()
	return bool(_identity.get("can_host_lan", false))


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
		"player_key": str(identity.get("player_key", "")),
	}


func _resolve_identity() -> void:
	var build_channel := _detect_build_channel()
	var steam_id := _get_steam_account_id()
	var display_name := _get_display_name()

	var provider := PROVIDER_OFFLINE
	var account_id := _load_or_create_offline_uuid()
	var can_lan := false

	if build_channel == CHANNEL_STEAM and steam_id != "":
		provider = PROVIDER_STEAM
		account_id = steam_id
		can_lan = true

	var player_key := _make_player_key(provider, account_id)
	_identity = {
		"provider": provider,
		"account_id": account_id,
		"display_name": display_name,
		"build_channel": build_channel,
		"player_key": player_key,
		"can_host_lan": can_lan,
	}

	if debug_identity:
		print("[IdentityManager] provider=%s channel=%s key=%s can_host_lan=%s" % [
			provider, build_channel, player_key, str(can_lan)])


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
	return "%s-%s-%s-%s-%s%s%s" % [
		_hex(rng.randi(), 8),
		_hex(rng.randi(), 4),
		_hex(rng.randi(), 4),
		_hex(rng.randi(), 4),
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


func _make_player_key(provider: String, account_id: String) -> String:
	return _sanitize_key("%s_%s" % [provider, account_id])


func _sanitize_key(raw: String) -> String:
	var result := ""
	for i in range(raw.length()):
		var ch := raw.substr(i, 1)
		if ch.is_valid_identifier() or ch.is_valid_int() or ch == "-" or ch == "_":
			result += ch
		else:
			result += "_"
	return result
