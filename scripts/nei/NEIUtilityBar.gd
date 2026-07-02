class_name NEIUtilityBar
extends Control

## NEIUtilityBar is a row of toggle buttons for NEI's utility features.
## Active only when NEISettings.mode == UTILITY.
## Mirrors codechicken.nei utility buttons: magnet, delete, infinite, etc.

signal utility_toggled(name: String, enabled: bool)

const BUTTON_SIZE := Vector2(32, 32)
const BUTTON_SPACING := 4
const BAR_HEIGHT := 40

var player: PlayerController = null
var _buttons: Dictionary = {}  # name -> Button
var _bg: ColorRect


func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build_ui()
	if NEISettings != null:
		NEISettings.mode_changed.connect(_on_mode_changed)
		NEISettings.utility_toggled.connect(_on_utility_changed)
		_on_mode_changed(NEISettings.mode)


func set_player(p: PlayerController) -> void:
	player = p


func _build_ui() -> void:
	var viewport_size := get_viewport_rect().size
	size = Vector2(viewport_size.x, BAR_HEIGHT)
	position = Vector2(0, 0)

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.055, 0.058, 0.07, 0.88)
	_bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_bg)

	var defs := _utility_defs()
	var total_w := defs.size() * (BUTTON_SIZE.x + BUTTON_SPACING) - BUTTON_SPACING
	var start_x := (viewport_size.x - total_w) / 2.0
	for i in range(defs.size()):
		var def: Dictionary = defs[i]
		var btn := Button.new()
		btn.text = tr(str(def.get("label_key", "")))
		btn.tooltip_text = tr(str(def.get("tooltip_key", "")))
		btn.toggle_mode = true
		btn.button_pressed = false
		btn.position = Vector2(start_x + i * (BUTTON_SIZE.x + BUTTON_SPACING), 4)
		btn.size = BUTTON_SIZE
		btn.add_theme_font_size_override("font_size", 10)
		btn.pressed.connect(_on_button_pressed.bind(str(def.get("name"))))
		add_child(btn)
		_buttons[str(def.get("name"))] = btn

	get_viewport().size_changed.connect(_reposition)


# Definition of all utility buttons.
func _utility_defs() -> Array[Dictionary]:
	return [
		{"name": "magnet", "label_key": "nei.util.magnet", "tooltip_key": "nei.util.magnet_tip"},
		{"name": "delete", "label_key": "nei.util.delete", "tooltip_key": "nei.util.delete_tip"},
		{"name": "infinite", "label_key": "nei.util.infinite", "tooltip_key": "nei.util.infinite_tip"},
		{"name": "chunk_loader", "label_key": "nei.util.chunk_loader", "tooltip_key": "nei.util.chunk_loader_tip"},
		{"name": "block_highlight", "label_key": "nei.util.block_highlight", "tooltip_key": "nei.util.block_highlight_tip"},
		{"name": "entity_radar", "label_key": "nei.util.entity_radar", "tooltip_key": "nei.util.entity_radar_tip"},
		{"name": "time_dawn", "label_key": "nei.util.time_dawn", "tooltip_key": "nei.util.time_dawn_tip"},
		{"name": "time_noon", "label_key": "nei.util.time_noon", "tooltip_key": "nei.util.time_noon_tip"},
		{"name": "time_dusk", "label_key": "nei.util.time_dusk", "tooltip_key": "nei.util.time_dusk_tip"},
		{"name": "time_midnight", "label_key": "nei.util.time_midnight", "tooltip_key": "nei.util.time_midnight_tip"},
		{"name": "rain", "label_key": "nei.util.rain", "tooltip_key": "nei.util.rain_tip"},
	]


func _reposition() -> void:
	var viewport_size := get_viewport_rect().size
	size = Vector2(viewport_size.x, BAR_HEIGHT)
	if _bg != null:
		_bg.size = size
	var defs := _utility_defs()
	var total_w := defs.size() * (BUTTON_SIZE.x + BUTTON_SPACING) - BUTTON_SPACING
	var start_x := (viewport_size.x - total_w) / 2.0
	for i in range(defs.size()):
		var name := str(defs[i].get("name"))
		if _buttons.has(name):
			_buttons[name].position = Vector2(start_x + i * (BUTTON_SIZE.x + BUTTON_SPACING), 4)


func _on_mode_changed(new_mode: int) -> void:
	visible = new_mode == NEISettings.Mode.UTILITY


func _on_utility_changed(name: String, enabled: bool) -> void:
	if _buttons.has(name):
		_buttons[name].button_pressed = enabled


func _on_button_pressed(name: String) -> void:
	# Time and weather are one-shot actions, not toggles.
	if name.begins_with("time_"):
		_apply_time_action(name)
		# Reset button visual since these are not toggles.
		if _buttons.has(name):
			_buttons[name].button_pressed = false
		return
	if name == "rain":
		_apply_rain_toggle()
		return
	if NEISettings != null:
		NEISettings.toggle_utility(name)


# Apply a time-of-day action. Attempts C++ binding; no-ops if unsupported.
func _apply_time_action(name: String) -> void:
	var target_time := 0.5
	match name:
		"time_dawn": target_time = 0.25
		"time_noon": target_time = 0.5
		"time_dusk": target_time = 0.75
		"time_midnight": target_time = 0.0
	if player == null or player.universe_manager == null:
		return
	var tick_sys = player.universe_manager.tick_system
	if tick_sys == null:
		return
	# Try set_time_of_day if the binding exposes it; otherwise log.
	if tick_sys.has_method("set_time_of_day"):
		tick_sys.set_time_of_day(target_time)
	else:
		print("[NEI] set_time_of_day not available in current tick system")


# Toggle rain. No-ops if weather system is unavailable.
func _apply_rain_toggle() -> void:
	if player == null or player.universe_manager == null:
		return
	var tick_sys = player.universe_manager.tick_system
	if tick_sys == null:
		return
	if tick_sys.has_method("set_raining"):
		tick_sys.set_raining(not tick_sys.has_method("is_raining") or not tick_sys.is_raining())
	else:
		print("[NEI] weather control not available in current tick system")


# Process utility effects each frame (magnet, etc.).
func _process(_delta: float) -> void:
	if NEISettings == null or NEISettings.mode != NEISettings.Mode.UTILITY:
		return
	if NEISettings.get_utility("magnet"):
		_process_magnet()


# Magnet mode: pull dropped item entities toward the player.
func _process_magnet() -> void:
	if player == null:
		return
	# Delegate to player helper if available; magnet logic lives there.
	if player.has_method("_nei_magnet_pull"):
		player._nei_magnet_pull()
