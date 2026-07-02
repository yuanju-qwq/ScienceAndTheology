# MainMenu — Title screen with single-player world selection, multiplayer, settings, and quit.
# Builds all UI programmatically to match the project's ConsoleUI / InventoryUI style.
extends Control

# ── Constants ──────────────────────────────────────────────────────────────────

const SAVE_ROOT := "user://saves/"
const META_FILE := "meta.json"
const WORLD_SCENE_PATH := "res://WorldMap.tscn"

# ── Theme colors ──────────────────────────────────────────────────────────────

const COLOR_BG := Color(0.03, 0.035, 0.05, 1.0)
const COLOR_PANEL := Color(0.06, 0.065, 0.08, 0.95)
const COLOR_BUTTON := Color(0.10, 0.11, 0.14, 1.0)
const COLOR_BUTTON_HOVER := Color(0.18, 0.20, 0.26, 1.0)
const COLOR_BUTTON_PRESSED := Color(0.25, 0.28, 0.35, 1.0)
const COLOR_ACCENT := Color(0.35, 0.55, 0.75, 1.0)
const COLOR_TEXT := Color(0.85, 0.87, 0.90, 1.0)
const COLOR_TEXT_DIM := Color(0.50, 0.52, 0.56, 1.0)
const COLOR_LIST_ITEM := Color(0.08, 0.085, 0.10, 1.0)
const COLOR_LIST_ITEM_SELECTED := Color(0.15, 0.20, 0.28, 1.0)
const COLOR_LIST_ITEM_HOVER := Color(0.12, 0.14, 0.18, 1.0)

# ── Layout metrics ────────────────────────────────────────────────────────────

const BUTTON_WIDTH := 260
const BUTTON_HEIGHT := 44
const BUTTON_GAP := 10
const TITLE_FONT_SIZE := 42
const SUBTITLE_FONT_SIZE := 16
const LIST_ITEM_HEIGHT := 40
const LIST_ITEM_GAP := 4
const PANEL_WIDTH := 500
const PANEL_HEIGHT := 480
const SEARCH_HEIGHT := 30
const SORT_WIDTH := 120

# ── State ─────────────────────────────────────────────────────────────────────

enum State { MAIN_MENU, WORLD_LIST, NEW_WORLD_DIALOG, SETTINGS }

var _state: State = State.MAIN_MENU
var _world_list: Array[Dictionary] = []
var _world_list_filtered: Array[Dictionary] = []
var _selected_world_index: int = -1
var _search_text: String = ""
var _sort_mode: int = 0  # 0=date newest, 1=date oldest, 2=name a→z, 3=name z→a

# ── Node references (built in _build_ui) ──────────────────────────────────────

var _main_menu_vbox: VBoxContainer
var _world_list_panel: Control
var _world_list_container: VBoxContainer
var _search_input: LineEdit
var _sort_option: OptionButton
var _new_world_panel: Control
var _new_world_input: LineEdit
var _game_mode_option: OptionButton
var _permission_option: OptionButton
var _universe_mode_option: OptionButton
var _seed_input: LineEdit
var _world_settings_btn: Button
var _planet_settings_btn: Button
var _rename_dialog: Window
var _rename_input: LineEdit
var _rename_world_folder: String = ""
var _ws_window: Window
var _ws_day_night: CheckBox
var _ws_day_length: SpinBox
var _ws_seasons: CheckBox
var _ws_ecosystem: CheckBox
var _ws_collapse: CheckBox
var _ws_gravity: CheckBox
var _ps_window: Window
var _ps_size: OptionButton
var _ps_terrain: OptionButton
var _ps_sea_level: OptionButton
var _ps_caves: OptionButton
var _ps_atmosphere: OptionButton
var _temp_gameplay_config: Dictionary = {}
var _temp_planet_overrides: Dictionary = {}
var _status_label: Label
var _settings_ui: SettingsUI

# ── Lifecycle ─────────────────────────────────────────────────────────────────


func _ready() -> void:
	_ensure_save_root()
	_sync_identity_to_session()
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_build_ui()
	_show_main_menu()


func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED:
		_recenter_panels()


# ── Save directory helpers ────────────────────────────────────────────────────


func _ensure_save_root() -> void:
	if not DirAccess.dir_exists_absolute(SAVE_ROOT):
		DirAccess.make_dir_recursive_absolute(SAVE_ROOT)


func _scan_worlds() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var dir := DirAccess.open(SAVE_ROOT)
	if dir == null:
		return result
	dir.list_dir_begin()
	var folder_name := dir.get_next()
	while folder_name != "":
		if dir.current_is_dir() and not folder_name.begins_with("."):
			var meta_path := SAVE_ROOT + folder_name + "/" + META_FILE
			var entry := _load_world_meta(meta_path, folder_name)
			if not entry.is_empty():
				result.append(entry)
		folder_name = dir.get_next()
	dir.list_dir_end()
	result.sort_custom(_compare_worlds_by_date)
	return result


func _load_world_meta(path: String, folder_name: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"name": folder_name, "folder": folder_name, "date": "", "universe_mode": ""}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {"name": folder_name, "folder": folder_name, "date": "", "universe_mode": ""}
	var json_text := f.get_as_text()
	f.close()
	var json := JSON.new()
	if json.parse(json_text) != OK:
		return {"name": folder_name, "folder": folder_name, "date": "", "universe_mode": ""}
	var data: Dictionary = json.data
	return {
		"name": str(data.get("name", folder_name)),
		"folder": folder_name,
		"date": str(data.get("created_at", "")),
		"universe_mode": str(data.get("universe_mode", "solar_system")),
		"universe_seed": int(data.get("universe_seed", 0) as int),
		"game_mode": int(data.get("game_mode", 0)),
		"permission_level": int(data.get("permission_level", 1)),
		"gameplay_config": data.get("gameplay_config", {}),
		"planet_overrides": data.get("planet_overrides", {}),
	}


func _compare_worlds_by_date(a: Dictionary, b: Dictionary) -> bool:
	return str(a.get("date", "")) > str(b.get("date", ""))


func _create_world(world_name: String, mode: String, seed_value: int) -> bool:
	var folder_name := world_name.strip_edges().validate_filename()
	if folder_name == "":
		folder_name = "World_%d" % Time.get_unix_time_from_system()
	var full_path := SAVE_ROOT + folder_name + "/"
	if DirAccess.dir_exists_absolute(full_path):
		_set_status("World already exists!")
		return false
	DirAccess.make_dir_recursive_absolute(full_path)
	var meta := {
		"name": world_name.strip_edges(),
		"folder": folder_name,
		"created_at": Time.get_datetime_string_from_system(),
		"universe_mode": mode,
		"universe_seed": seed_value,
		"game_mode": _get_selected_game_mode(),
		"permission_level": _get_selected_permission_level(),
		"gameplay_config": _temp_gameplay_config.duplicate(),
		"planet_overrides": _temp_planet_overrides.duplicate(),
	}
	var json_text := JSON.stringify(meta, "\t")
	var f := FileAccess.open(full_path + META_FILE, FileAccess.WRITE)
	if f == null:
		_set_status("Failed to create world!")
		return false
	f.store_string(json_text)
	f.close()
	GameSession.world_name = meta["name"]
	GameSession.save_path = full_path
	GameSession.universe_mode = mode
	GameSession.universe_seed = seed_value
	GameSession.game_mode = meta["game_mode"]
	GameSession.permission_level = meta["permission_level"]
	GameSession.gameplay_config = meta["gameplay_config"]
	GameSession.planet_overrides = meta["planet_overrides"]
	return true


func _load_world(entry: Dictionary) -> void:
	GameSession.world_name = str(entry.get("name", ""))
	GameSession.save_path = SAVE_ROOT + str(entry.get("folder", "")) + "/"
	GameSession.universe_mode = str(entry.get("universe_mode", "solar_system"))
	GameSession.universe_seed = int(entry.get("universe_seed", 0) as int)
	GameSession.game_mode = int(entry.get("game_mode", 0))
	GameSession.permission_level = int(entry.get("permission_level", 1))
	var gc: Dictionary = entry.get("gameplay_config", {})
	if not gc.is_empty():
		GameSession.gameplay_config = gc
	var po: Dictionary = entry.get("planet_overrides", {})
	if not po.is_empty():
		GameSession.planet_overrides = po


# ── Scene transition ──────────────────────────────────────────────────────────


func _start_game() -> void:
	get_tree().change_scene_to_file(WORLD_SCENE_PATH)


# ── UI construction ───────────────────────────────────────────────────────────


func _build_ui() -> void:
	# Full-screen dark background.
	var bg := ColorRect.new()
	bg.name = "Background"
	bg.color = COLOR_BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(bg)

	# Title label.
	var title := Label.new()
	title.name = "TitleLabel"
	title.text = tr("menu.game_title")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", TITLE_FONT_SIZE)
	title.add_theme_color_override("font_color", COLOR_TEXT)
	title.set_anchors_preset(Control.PRESET_CENTER_TOP)
	title.offset_top = -120
	title.offset_left = -200
	title.offset_right = 200
	title.offset_bottom = -80
	add_child(title)

	# Subtitle label.
	var subtitle := Label.new()
	subtitle.name = "SubtitleLabel"
	subtitle.text = tr("menu.game_subtitle")
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_font_size_override("font_size", SUBTITLE_FONT_SIZE)
	subtitle.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	subtitle.set_anchors_preset(Control.PRESET_CENTER_TOP)
	subtitle.offset_top = -78
	subtitle.offset_left = -200
	subtitle.offset_right = 200
	subtitle.offset_bottom = -58
	add_child(subtitle)

	# Status label (for feedback messages).
	_status_label = Label.new()
	_status_label.name = "StatusLabel"
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status_label.add_theme_font_size_override("font_size", 14)
	_status_label.add_theme_color_override("font_color", COLOR_ACCENT)
	_status_label.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	_status_label.offset_top = 40
	_status_label.offset_left = -300
	_status_label.offset_right = 300
	_status_label.offset_bottom = 60
	add_child(_status_label)

	_build_main_menu()
	_build_world_list_panel()
	_build_new_world_dialog()
	_build_settings_ui()


# ── Main menu buttons ─────────────────────────────────────────────────────────


func _build_main_menu() -> void:
	_main_menu_vbox = VBoxContainer.new()
	_main_menu_vbox.name = "MainMenuVBox"
	_main_menu_vbox.add_theme_constant_override("separation", BUTTON_GAP)
	add_child(_main_menu_vbox)

	var labels := ["menu.single_player", "menu.multiplayer", "menu.settings", "menu.quit"]
	var callbacks: Array[Callable] = [
		_on_single_player,
		_on_multiplayer,
		_on_settings,
		_on_quit,
	]

	for i in labels.size():
		var btn := _make_button(labels[i] as String, BUTTON_WIDTH, BUTTON_HEIGHT)
		btn.pressed.connect(callbacks[i])
		_main_menu_vbox.add_child(btn)


# ── World list panel ──────────────────────────────────────────────────────────


func _build_world_list_panel() -> void:
	_world_list_panel = Control.new()
	_world_list_panel.name = "WorldListPanel"
	_world_list_panel.visible = false
	add_child(_world_list_panel)

	# Panel background.
	var panel_bg := PanelContainer.new()
	panel_bg.name = "PanelBg"
	panel_bg.custom_minimum_size = Vector2(PANEL_WIDTH, PANEL_HEIGHT)
	var panel_style := StyleBoxFlat.new()
	panel_style.bg_color = COLOR_PANEL
	panel_style.border_color = Color(0.15, 0.16, 0.20, 1.0)
	panel_style.set_border_width_all(1)
	panel_style.set_content_margin_all(12)
	panel_style.set_corner_radius_all(4)
	panel_bg.add_theme_stylebox_override("panel", panel_style)
	_world_list_panel.add_child(panel_bg)

	# Inner vertical layout.
	var vbox := VBoxContainer.new()
	vbox.name = "InnerVBox"
	vbox.add_theme_constant_override("separation", 8)
	panel_bg.add_child(vbox)

	# Title row.
	var title_row := HBoxContainer.new()
	title_row.add_theme_constant_override("separation", 8)

	var title_label := Label.new()
	title_label.text = tr("menu.select_world")
	title_label.add_theme_font_size_override("font_size", 22)
	title_label.add_theme_color_override("font_color", COLOR_TEXT)
	title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title_row.add_child(title_label)

	var refresh_btn := _make_button("menu.refresh", 60, 28)
	refresh_btn.add_theme_font_size_override("font_size", 12)
	refresh_btn.pressed.connect(_on_refresh_worlds)
	title_row.add_child(refresh_btn)

	vbox.add_child(title_row)

	# Search row.
	var search_row := HBoxContainer.new()
	search_row.add_theme_constant_override("separation", 6)

	_search_input = LineEdit.new()
	_search_input.name = "SearchInput"
	_search_input.placeholder_text = tr("menu.search_world")
	_search_input.custom_minimum_size = Vector2(0, SEARCH_HEIGHT)
	_search_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_search_input.text_changed.connect(_on_search_changed)
	search_row.add_child(_search_input)

	_sort_option = OptionButton.new()
	_sort_option.name = "SortOption"
	_sort_option.custom_minimum_size = Vector2(SORT_WIDTH, SEARCH_HEIGHT)
	_sort_option.add_item(tr("menu.sort_newest"), 0)
	_sort_option.add_item(tr("menu.sort_oldest"), 1)
	_sort_option.add_item(tr("menu.sort_name_az"), 2)
	_sort_option.add_item(tr("menu.sort_name_za"), 3)
	_sort_option.selected = 0
	_sort_option.item_selected.connect(_on_sort_changed)
	search_row.add_child(_sort_option)

	vbox.add_child(search_row)

	# Scrollable world list.
	var scroll := ScrollContainer.new()
	scroll.name = "WorldScroll"
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	vbox.add_child(scroll)

	_world_list_container = VBoxContainer.new()
	_world_list_container.name = "WorldListItems"
	_world_list_container.add_theme_constant_override("separation", LIST_ITEM_GAP)
	_world_list_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_world_list_container)

	# Bottom button row.
	var btn_row := HBoxContainer.new()
	btn_row.name = "ButtonRow"
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var btn_new := _make_button("menu.new_world", 100, BUTTON_HEIGHT)
	btn_new.pressed.connect(_on_new_world)
	btn_row.add_child(btn_new)

	var btn_load := _make_button("menu.load_world", 100, BUTTON_HEIGHT)
	btn_load.pressed.connect(_on_load_world)
	btn_row.add_child(btn_load)

	var btn_rename := _make_button("menu.rename", 80, BUTTON_HEIGHT)
	btn_rename.pressed.connect(_on_rename_world)
	btn_row.add_child(btn_rename)

	var btn_delete := _make_button("menu.delete", 80, BUTTON_HEIGHT)
	btn_delete.pressed.connect(_on_delete_world)
	btn_delete.add_theme_color_override("font_color", Color(0.90, 0.45, 0.45, 1.0))
	btn_delete.add_theme_color_override("font_hover_color", Color(1.0, 0.55, 0.55, 1.0))
	btn_row.add_child(btn_delete)

	var btn_back := _make_button("menu.back", 80, BUTTON_HEIGHT)
	btn_back.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	btn_back.pressed.connect(_show_main_menu)
	btn_row.add_child(btn_back)

	vbox.add_child(btn_row)


# ── New world dialog ──────────────────────────────────────────────────────────


func _build_settings_ui() -> void:
	_settings_ui = SettingsUI.new()
	_settings_ui.name = "SettingsUI"
	_settings_ui.closed.connect(_show_main_menu)
	add_child(_settings_ui)


func _build_new_world_dialog() -> void:
	_new_world_panel = Control.new()
	_new_world_panel.name = "NewWorldPanel"
	_new_world_panel.visible = false
	add_child(_new_world_panel)

	var dialog_bg := PanelContainer.new()
	dialog_bg.name = "DialogBg"
	dialog_bg.custom_minimum_size = Vector2(460, 340)
	var dialog_style := StyleBoxFlat.new()
	dialog_style.bg_color = COLOR_PANEL
	dialog_style.border_color = Color(0.15, 0.16, 0.20, 1.0)
	dialog_style.set_border_width_all(1)
	dialog_style.set_content_margin_all(16)
	dialog_style.set_corner_radius_all(4)
	dialog_bg.add_theme_stylebox_override("panel", dialog_style)
	_new_world_panel.add_child(dialog_bg)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 8)
	dialog_bg.add_child(vbox)

	# Dialog title.
	var dialog_title := Label.new()
	dialog_title.text = tr("menu.new_world_title")
	dialog_title.add_theme_font_size_override("font_size", 20)
	dialog_title.add_theme_color_override("font_color", COLOR_TEXT)
	vbox.add_child(dialog_title)

	# Separator.
	vbox.add_child(_make_separator())

	# World name input row.
	var name_row := HBoxContainer.new()
	name_row.add_theme_constant_override("separation", 8)
	var name_label := Label.new()
	name_label.text = tr("menu.world_name")
	name_label.add_theme_font_size_override("font_size", 14)
	name_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	name_label.custom_minimum_size = Vector2(80, 0)
	name_row.add_child(name_label)
	_new_world_input = LineEdit.new()
	_new_world_input.name = "WorldNameInput"
	_new_world_input.placeholder_text = tr("menu.world_name_placeholder")
	_new_world_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_new_world_input.custom_minimum_size = Vector2(0, 32)
	name_row.add_child(_new_world_input)
	vbox.add_child(name_row)

	# Game mode selection row.
	var gm_row := HBoxContainer.new()
	gm_row.add_theme_constant_override("separation", 8)
	var gm_label := Label.new()
	gm_label.text = tr("menu.game_mode")
	gm_label.add_theme_font_size_override("font_size", 14)
	gm_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	gm_label.custom_minimum_size = Vector2(80, 0)
	gm_row.add_child(gm_label)
	_game_mode_option = OptionButton.new()
	_game_mode_option.name = "GameModeOption"
	_game_mode_option.add_item(tr("menu.mode_survival"), 0)
	_game_mode_option.add_item(tr("menu.mode_creative"), 1)
	_game_mode_option.add_item(tr("menu.mode_observer"), 2)
	_game_mode_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_game_mode_option.custom_minimum_size = Vector2(0, 32)
	_game_mode_option.selected = 0
	gm_row.add_child(_game_mode_option)
	vbox.add_child(gm_row)

	# Permission level selection row.
	var perm_row := HBoxContainer.new()
	perm_row.add_theme_constant_override("separation", 8)
	var perm_label := Label.new()
	perm_label.text = tr("menu.default_permission")
	perm_label.add_theme_font_size_override("font_size", 14)
	perm_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	perm_label.custom_minimum_size = Vector2(80, 0)
	perm_row.add_child(perm_label)
	_permission_option = OptionButton.new()
	_permission_option.name = "PermissionOption"
	_permission_option.add_item(tr("menu.perm_player"), 0)
	_permission_option.add_item(tr("menu.perm_cheater"), 1)
	_permission_option.add_item(tr("menu.perm_admin"), 2)
	_permission_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_permission_option.custom_minimum_size = Vector2(0, 32)
	_permission_option.selected = 1
	perm_row.add_child(_permission_option)
	vbox.add_child(perm_row)

	# Universe mode selection row.
	var mode_row := HBoxContainer.new()
	mode_row.add_theme_constant_override("separation", 8)
	var mode_label := Label.new()
	mode_label.text = tr("menu.universe_mode")
	mode_label.add_theme_font_size_override("font_size", 14)
	mode_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	mode_label.custom_minimum_size = Vector2(80, 0)
	mode_row.add_child(mode_label)
	_universe_mode_option = OptionButton.new()
	_universe_mode_option.name = "UniverseModeOption"
	_universe_mode_option.add_item(tr("menu.universe_standard"), 0)
	_universe_mode_option.add_item(tr("menu.universe_random"), 1)
	_universe_mode_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_universe_mode_option.custom_minimum_size = Vector2(0, 32)
	_universe_mode_option.item_selected.connect(_on_universe_mode_changed)
	mode_row.add_child(_universe_mode_option)
	vbox.add_child(mode_row)

	# Seed input row.
	var seed_row := HBoxContainer.new()
	seed_row.add_theme_constant_override("separation", 8)
	var seed_label := Label.new()
	seed_label.text = tr("menu.world_seed")
	seed_label.add_theme_font_size_override("font_size", 14)
	seed_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	seed_label.custom_minimum_size = Vector2(80, 0)
	seed_row.add_child(seed_label)
	_seed_input = LineEdit.new()
	_seed_input.name = "SeedInput"
	_seed_input.placeholder_text = tr("menu.seed_placeholder")
	_seed_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_seed_input.custom_minimum_size = Vector2(0, 32)
	seed_row.add_child(_seed_input)
	var randomize_btn := _make_button("🎲", 36, 32)
	randomize_btn.add_theme_font_size_override("font_size", 16)
	randomize_btn.pressed.connect(_on_randomize_seed)
	seed_row.add_child(randomize_btn)
	vbox.add_child(seed_row)

	# Separator.
	vbox.add_child(_make_separator())

	# Config buttons row.
	var cfg_row := HBoxContainer.new()
	cfg_row.add_theme_constant_override("separation", 10)
	cfg_row.alignment = BoxContainer.ALIGNMENT_CENTER

	_world_settings_btn = _make_button("menu.world_settings", 180, 36)
	_world_settings_btn.pressed.connect(_open_world_settings)
	cfg_row.add_child(_world_settings_btn)

	_planet_settings_btn = _make_button("menu.planet_settings", 180, 36)
	_planet_settings_btn.pressed.connect(_open_planet_settings)
	cfg_row.add_child(_planet_settings_btn)

	vbox.add_child(cfg_row)

	# Separator.
	vbox.add_child(_make_separator())

	# Bottom button row.
	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var btn_create := _make_button("menu.create_world", 140, BUTTON_HEIGHT)
	btn_create.pressed.connect(_on_create_world)
	btn_row.add_child(btn_create)

	var btn_cancel := _make_button("menu.cancel", 120, BUTTON_HEIGHT)
	btn_cancel.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	btn_cancel.pressed.connect(_on_cancel_new_world)
	btn_row.add_child(btn_cancel)

	vbox.add_child(btn_row)

	# Build the two config popup windows.
	_build_world_settings_window()
	_build_planet_settings_window()


# ── UI helper factories ──────────────────────────────────────────────────────


func _make_separator() -> HSeparator:
	var sep := HSeparator.new()
	sep.modulate = Color(0.30, 0.32, 0.38, 0.5)
	return sep


func _make_label(text: String, width: int) -> Label:
	var label := Label.new()
	label.text = tr(text)
	label.add_theme_font_size_override("font_size", 12)
	label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	if width > 0:
		label.custom_minimum_size = Vector2(width, 0)
	return label


func _make_checkbox(label: String, default_value: bool) -> CheckBox:
	var cb := CheckBox.new()
	cb.text = tr(label)
	cb.button_pressed = default_value
	cb.add_theme_font_size_override("font_size", 13)
	cb.add_theme_color_override("font_color", COLOR_TEXT)
	return cb


func _make_spinbox(min_val: float, max_val: float, default_val: float, step_val: float) -> SpinBox:
	var sb := SpinBox.new()
	sb.min_value = min_val
	sb.max_value = max_val
	sb.value = default_val
	sb.step = step_val
	sb.custom_minimum_size = Vector2(90, 28)
	sb.add_theme_color_override("", COLOR_TEXT)
	return sb


# ── Config popup builders ─────────────────────────────────────────────────────


func _build_world_settings_window() -> void:
	_ws_window = Window.new()
	_ws_window.title = tr("menu.world_settings")
	_ws_window.transient = true
	_ws_window.exclusive = true
	_ws_window.initial_position = Window.WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN
	_ws_window.close_requested.connect(_close_world_settings)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 8)
	vbox.anchor_right = 1.0
	vbox.anchor_bottom = 1.0
	_ws_window.add_child(vbox)

	# Padding.
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(margin)

	var inner := VBoxContainer.new()
	inner.add_theme_constant_override("separation", 8)
	margin.add_child(inner)

	# Day/night cycle.
	var dn_row := HBoxContainer.new()
	dn_row.add_theme_constant_override("separation", 8)
	_ws_day_night = _make_checkbox("menu.day_night_cycle", true)
	dn_row.add_child(_ws_day_night)
	dn_row.add_child(_make_label("menu.day_length", 100))
	_ws_day_length = _make_spinbox(60, 3600, 600, 50)
	dn_row.add_child(_ws_day_length)
	dn_row.add_child(_make_label("", 0))
	inner.add_child(dn_row)

	# Seasons toggle.
	var sn_row := HBoxContainer.new()
	sn_row.add_theme_constant_override("separation", 8)
	_ws_seasons = _make_checkbox("menu.seasons", true)
	sn_row.add_child(_ws_seasons)
	sn_row.add_child(_make_label("", 0))
	inner.add_child(sn_row)

	# Ecosystem toggle.
	var ec_row := HBoxContainer.new()
	ec_row.add_theme_constant_override("separation", 8)
	_ws_ecosystem = _make_checkbox("menu.ecosystem", true)
	ec_row.add_child(_ws_ecosystem)
	ec_row.add_child(_make_label("", 0))
	inner.add_child(ec_row)

	# Collapse toggle.
	var cl_row := HBoxContainer.new()
	cl_row.add_theme_constant_override("separation", 8)
	_ws_collapse = _make_checkbox("menu.collapse", true)
	cl_row.add_child(_ws_collapse)
	cl_row.add_child(_make_label("", 0))
	inner.add_child(cl_row)

	# Gravity toggle.
	var gr_row := HBoxContainer.new()
	gr_row.add_theme_constant_override("separation", 8)
	_ws_gravity = _make_checkbox("menu.gravity", true)
	gr_row.add_child(_ws_gravity)
	gr_row.add_child(_make_label("", 0))
	inner.add_child(gr_row)

	# Spacer.
	inner.add_child(Control.new())

	# Buttons.
	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var save_btn := _make_button("menu.save", 100, 34)
	save_btn.pressed.connect(_save_world_settings)
	btn_row.add_child(save_btn)

	var cancel_btn := _make_button("menu.cancel", 100, 34)
	cancel_btn.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	cancel_btn.pressed.connect(_close_world_settings)
	btn_row.add_child(cancel_btn)

	inner.add_child(btn_row)


func _open_world_settings() -> void:
	# Populate from temp config (or defaults if empty).
	_ws_day_night.button_pressed = _temp_gameplay_config.get("enable_day_night", true)
	_ws_day_length.value = _temp_gameplay_config.get("day_length_seconds", 600.0)
	_ws_seasons.button_pressed = _temp_gameplay_config.get("enable_season_colors", true)
	_ws_ecosystem.button_pressed = _temp_gameplay_config.get("enable_ecosystem", true)
	_ws_collapse.button_pressed = _temp_gameplay_config.get("enable_collapse", true)
	_ws_gravity.button_pressed = _temp_gameplay_config.get("enable_gravity_fall", true)
	add_child(_ws_window)
	_ws_window.popup_centered(Vector2i(360, 320))


func _save_world_settings() -> void:
	_temp_gameplay_config = {
		"enable_day_night": _ws_day_night.button_pressed,
		"day_length_seconds": _ws_day_length.value,
		"enable_season_colors": _ws_seasons.button_pressed,
		"enable_ecosystem": _ws_ecosystem.button_pressed,
		"enable_collapse": _ws_collapse.button_pressed,
		"enable_gravity_fall": _ws_gravity.button_pressed,
	}
	_close_world_settings()


func _close_world_settings() -> void:
	if _ws_window and _ws_window.is_inside_tree():
		_ws_window.queue_free()


func _build_planet_settings_window() -> void:
	_ps_window = Window.new()
	_ps_window.title = tr("menu.planet_settings")
	_ps_window.transient = true
	_ps_window.exclusive = true
	_ps_window.initial_position = Window.WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN
	_ps_window.close_requested.connect(_close_planet_settings)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 8)
	vbox.anchor_right = 1.0
	vbox.anchor_bottom = 1.0
	_ps_window.add_child(vbox)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 12)
	margin.add_theme_constant_override("margin_right", 12)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(margin)

	var inner := VBoxContainer.new()
	inner.add_theme_constant_override("separation", 10)
	margin.add_child(inner)

	# Size preset.
	var sz_row := HBoxContainer.new()
	sz_row.add_theme_constant_override("separation", 8)
	sz_row.add_child(_make_label("menu.planet_size", 120))
	_ps_size = OptionButton.new()
	_ps_size.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ps_size.add_item(tr("menu.default_random"), 0)
	_ps_size.add_item(tr("menu.size_small"), 1)
	_ps_size.add_item(tr("menu.size_medium"), 2)
	_ps_size.add_item(tr("menu.size_large"), 3)
	_ps_size.add_item(tr("menu.size_huge"), 4)
	_ps_size.selected = 0
	sz_row.add_child(_ps_size)
	inner.add_child(sz_row)

	# Terrain preset.
	var tr_row := HBoxContainer.new()
	tr_row.add_theme_constant_override("separation", 8)
	tr_row.add_child(_make_label("menu.terrain", 120))
	_ps_terrain = OptionButton.new()
	_ps_terrain.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ps_terrain.add_item(tr("menu.default_random"), 0)
	_ps_terrain.add_item(tr("menu.terrain_flat"), 1)
	_ps_terrain.add_item(tr("menu.terrain_hilly"), 2)
	_ps_terrain.add_item(tr("menu.terrain_mountain"), 3)
	_ps_terrain.add_item(tr("menu.terrain_extreme"), 4)
	_ps_terrain.selected = 0
	tr_row.add_child(_ps_terrain)
	inner.add_child(tr_row)

	# Sea level preset.
	var sl_row := HBoxContainer.new()
	sl_row.add_theme_constant_override("separation", 8)
	sl_row.add_child(_make_label("menu.sea_level", 120))
	_ps_sea_level = OptionButton.new()
	_ps_sea_level.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ps_sea_level.add_item(tr("menu.default_random"), 0)
	_ps_sea_level.add_item(tr("menu.sea_none"), 1)
	_ps_sea_level.add_item(tr("menu.sea_low"), 2)
	_ps_sea_level.add_item(tr("menu.sea_medium"), 3)
	_ps_sea_level.add_item(tr("menu.sea_high"), 4)
	_ps_sea_level.selected = 0
	sl_row.add_child(_ps_sea_level)
	inner.add_child(sl_row)

	# Cave density preset.
	var cv_row := HBoxContainer.new()
	cv_row.add_theme_constant_override("separation", 8)
	cv_row.add_child(_make_label("menu.caves", 120))
	_ps_caves = OptionButton.new()
	_ps_caves.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ps_caves.add_item(tr("menu.default_random"), 0)
	_ps_caves.add_item(tr("menu.caves_sparse"), 1)
	_ps_caves.add_item(tr("menu.caves_normal"), 2)
	_ps_caves.add_item(tr("menu.caves_dense"), 3)
	_ps_caves.selected = 0
	cv_row.add_child(_ps_caves)
	inner.add_child(cv_row)

	# Atmosphere preset.
	var at_row := HBoxContainer.new()
	at_row.add_theme_constant_override("separation", 8)
	at_row.add_child(_make_label("menu.atmosphere", 120))
	_ps_atmosphere = OptionButton.new()
	_ps_atmosphere.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_ps_atmosphere.add_item(tr("menu.default_random"), 0)
	_ps_atmosphere.add_item(tr("menu.atmos_vacuum"), 1)
	_ps_atmosphere.add_item(tr("menu.atmos_thin"), 2)
	_ps_atmosphere.add_item(tr("menu.atmos_breathable"), 3)
	_ps_atmosphere.add_item(tr("menu.atmos_toxic"), 4)
	_ps_atmosphere.add_item(tr("menu.atmos_corrosive"), 5)
	_ps_atmosphere.selected = 0
	at_row.add_child(_ps_atmosphere)
	inner.add_child(at_row)

	# Spacer.
	inner.add_child(Control.new())

	# Buttons.
	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var save_btn := _make_button("menu.save", 100, 34)
	save_btn.pressed.connect(_save_planet_settings)
	btn_row.add_child(save_btn)

	var cancel_btn := _make_button("menu.cancel", 100, 34)
	cancel_btn.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	cancel_btn.pressed.connect(_close_planet_settings)
	btn_row.add_child(cancel_btn)

	inner.add_child(btn_row)


func _open_planet_settings() -> void:
	var po := _temp_planet_overrides
	_ps_size.selected = _preset_index(po.get("size_preset", "default"), ["default", "small", "medium", "large", "huge"])
	_ps_terrain.selected = _preset_index(po.get("terrain_preset", "default"), ["default", "flat", "hilly", "mountainous", "extreme"])
	_ps_sea_level.selected = _preset_index(po.get("sea_level_preset", "default"), ["default", "none", "low", "medium", "high"])
	_ps_caves.selected = _preset_index(po.get("cave_preset", "default"), ["default", "sparse", "normal", "dense"])
	_ps_atmosphere.selected = _preset_index(po.get("atmosphere_preset", "default"), ["default", "none", "thin", "breathable", "toxic", "corrosive"])
	add_child(_ps_window)
	_ps_window.popup_centered(Vector2i(360, 360))


func _save_planet_settings() -> void:
	_temp_planet_overrides = {
		"size_preset": _preset_name(_ps_size.selected, ["default", "small", "medium", "large", "huge"]),
		"terrain_preset": _preset_name(_ps_terrain.selected, ["default", "flat", "hilly", "mountainous", "extreme"]),
		"sea_level_preset": _preset_name(_ps_sea_level.selected, ["default", "none", "low", "medium", "high"]),
		"cave_preset": _preset_name(_ps_caves.selected, ["default", "sparse", "normal", "dense"]),
		"atmosphere_preset": _preset_name(_ps_atmosphere.selected, ["default", "none", "thin", "breathable", "toxic", "corrosive"]),
	}
	_close_planet_settings()


func _close_planet_settings() -> void:
	if _ps_window and _ps_window.is_inside_tree():
		_ps_window.queue_free()


func _preset_index(value: String, options: Array[String]) -> int:
	for i in options.size():
		if options[i] == value:
			return i
	return 0


func _preset_name(index: int, options: Array[String]) -> String:
	if index >= 0 and index < options.size():
		return options[index]
	return "default"


# ── Button factory ────────────────────────────────────────────────────────────


func _make_button(text: String, width: int, height: int) -> Button:
	var btn := Button.new()
	btn.text = tr(text)
	btn.custom_minimum_size = Vector2(width, height)

	var normal := StyleBoxFlat.new()
	normal.bg_color = COLOR_BUTTON
	normal.set_corner_radius_all(4)
	normal.set_content_margin_all(8)
	normal.set_border_width_all(1)
	normal.border_color = Color(0.18, 0.19, 0.22, 1.0)

	var hover := normal.duplicate() as StyleBoxFlat
	hover.bg_color = COLOR_BUTTON_HOVER
	hover.border_color = Color(0.30, 0.32, 0.38, 1.0)

	var pressed := normal.duplicate() as StyleBoxFlat
	pressed.bg_color = COLOR_BUTTON_PRESSED
	pressed.border_color = COLOR_ACCENT

	btn.add_theme_stylebox_override("normal", normal)
	btn.add_theme_stylebox_override("hover", hover)
	btn.add_theme_stylebox_override("pressed", pressed)
	btn.add_theme_color_override("font_color", COLOR_TEXT)
	btn.add_theme_color_override("font_hover_color", Color(1.0, 1.0, 1.0, 1.0))
	btn.add_theme_font_size_override("font_size", 16)

	return btn


# ── World list item factory ──────────────────────────────────────────────────


func _make_world_list_item(entry: Dictionary, index: int) -> Control:
	var item := PanelContainer.new()
	item.name = "WorldItem_%d" % index
	item.custom_minimum_size = Vector2(0, LIST_ITEM_HEIGHT)

	var style := StyleBoxFlat.new()
	style.bg_color = COLOR_LIST_ITEM
	style.set_corner_radius_all(3)
	style.set_content_margin_all(8)
	style.set_border_width_all(1)
	style.border_color = Color(0.12, 0.13, 0.16, 1.0)
	item.add_theme_stylebox_override("panel", style)

	# Store index for click handling.
	item.set_meta("world_index", index)

	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 12)
	item.add_child(hbox)

	var name_label := Label.new()
	name_label.text = str(entry.get("name", "Unknown"))
	name_label.add_theme_font_size_override("font_size", 15)
	name_label.add_theme_color_override("font_color", COLOR_TEXT)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(name_label)

	# Universe mode tag.
	var mode_tag := Label.new()
	var mode_str := str(entry.get("universe_mode", "solar_system"))
	mode_tag.text = "太阳系" if mode_str == "solar_system" else "随机"
	mode_tag.add_theme_font_size_override("font_size", 11)
	mode_tag.add_theme_color_override("font_color", COLOR_ACCENT)
	mode_tag.custom_minimum_size = Vector2(40, 0)
	mode_tag.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hbox.add_child(mode_tag)

	var date_label := Label.new()
	date_label.text = str(entry.get("date", ""))
	date_label.add_theme_font_size_override("font_size", 12)
	date_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	date_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	hbox.add_child(date_label)

	# Click handling via gui_input.
	item.gui_input.connect(_on_world_item_input.bind(index))

	return item


# ── State transitions ─────────────────────────────────────────────────────────


func _show_main_menu() -> void:
	_state = State.MAIN_MENU
	_main_menu_vbox.visible = true
	_world_list_panel.visible = false
	_new_world_panel.visible = false
	if _settings_ui:
		_settings_ui.dismiss()
	_selected_world_index = -1
	_set_status("")
	_recenter_panels()


func _show_world_list() -> void:
	_state = State.WORLD_LIST
	_main_menu_vbox.visible = false
	_world_list_panel.visible = true
	_new_world_panel.visible = false
	_selected_world_index = -1
	_set_status("")
	_refresh_world_list()
	_recenter_panels()


func _show_new_world_dialog() -> void:
	_state = State.NEW_WORLD_DIALOG
	_new_world_panel.visible = true
	_new_world_input.text = ""
	_game_mode_option.select(0)
	_permission_option.select(1)
	_universe_mode_option.select(0)
	_seed_input.text = ""
	_temp_gameplay_config = {}
	_temp_planet_overrides = {}
	_new_world_input.grab_focus()
	_on_universe_mode_changed(0)
	_recenter_panels()


# ── World list rendering ──────────────────────────────────────────────────────


func _refresh_world_list() -> void:
	_world_list = _scan_worlds()
	_world_list_filtered = _filter_and_sort_worlds()
	_selected_world_index = -1

	for child in _world_list_container.get_children():
		child.queue_free()

	if _world_list_filtered.is_empty():
		var empty_label := Label.new()
		empty_label.text = "暂无存档，请新建世界" if _world_list.is_empty() else "没有匹配的世界"
		empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		empty_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
		empty_label.add_theme_font_size_override("font_size", 14)
		_world_list_container.add_child(empty_label)
		return

	for i in _world_list_filtered.size():
		var item := _make_world_list_item(_world_list_filtered[i], i)
		_world_list_container.add_child(item)


func _filter_and_sort_worlds() -> Array[Dictionary]:
	if _world_list.is_empty():
		return []

	var result: Array[Dictionary] = []

	# Filter by search text.
	if _search_text.is_empty():
		result = _world_list.duplicate()
	else:
		var q := _search_text.strip_edges().to_lower()
		for w in _world_list:
			var name := str(w.get("name", "")).to_lower()
			if q in name:
				result.append(w)

	# Sort.
	match _sort_mode:
		0: # Date newest first
			result.sort_custom(func(a, b): return str(a.get("date", "")) > str(b.get("date", "")))
		1: # Date oldest first
			result.sort_custom(func(a, b): return str(a.get("date", "")) < str(b.get("date", "")))
		2: # Name A→Z
			result.sort_custom(func(a, b): return str(a.get("name", "")).to_lower() < str(b.get("name", "")).to_lower())
		3: # Name Z→A
			result.sort_custom(func(a, b): return str(a.get("name", "")).to_lower() > str(b.get("name", "")).to_lower())

	return result


func _update_world_list_selection() -> void:
	for child in _world_list_container.get_children():
		if not child is PanelContainer:
			continue
		var idx: int = child.get_meta("world_index", -1)
		var style: StyleBoxFlat = (child as Control).get_theme_stylebox("panel")
		if idx == _selected_world_index:
			style.bg_color = COLOR_LIST_ITEM_SELECTED
			style.border_color = COLOR_ACCENT
		else:
			style.bg_color = COLOR_LIST_ITEM
			style.border_color = Color(0.12, 0.13, 0.16, 1.0)


# ── Centering ─────────────────────────────────────────────────────────────────


func _recenter_panels() -> void:
	if _main_menu_vbox == null or _world_list_panel == null or _new_world_panel == null:
		return
	var vp_size := get_viewport_rect().size
	var center := vp_size / 2.0

	# Main menu vbox.
	_main_menu_vbox.position = center - Vector2(BUTTON_WIDTH / 2.0, 40)

	# World list panel.
	_world_list_panel.position = center - Vector2(PANEL_WIDTH / 2.0, PANEL_HEIGHT / 2.0)

	# New world dialog.
	var new_world_size: Vector2 = Vector2(460, 440)
	if _new_world_panel.get_child_count() > 0:
		var new_world_content := _new_world_panel.get_child(0) as Control
		if new_world_content != null:
			new_world_size = new_world_content.custom_minimum_size
	_new_world_panel.position = center - new_world_size / 2.0


# ── Status label helper ───────────────────────────────────────────────────────


func _set_status(text: String) -> void:
	_status_label.text = text


# ── Universe mode helpers ─────────────────────────────────────────────────────


func _get_selected_universe_mode() -> String:
	var idx := _universe_mode_option.get_selected_id()
	match idx:
		0: return "solar_system"
		1: return "random"
		_: return "solar_system"


func _get_selected_game_mode() -> int:
	return _game_mode_option.get_selected_id()


func _get_selected_permission_level() -> int:
	return _permission_option.get_selected_id()


func _parse_seed_value() -> int:
	var text := _seed_input.text.strip_edges()
	if text == "":
		return 0
	# Accept numeric seeds.
	if text.is_valid_int():
		return text.to_int()
	# Non-numeric: hash the string.
	return text.hash()


# ── Identity helpers ──────────────────────────────────────────────────────────


func _sync_identity_to_session() -> void:
	var identity_manager := get_node_or_null(^"/root/IdentityManager")
	if identity_manager == null:
		return
	var identity: Dictionary = identity_manager.get_identity()
	GameSession.identity = identity
	GameSession.player_uuid = identity_manager.get_player_uuid()
	GameSession.player_save_key = GameSession.player_uuid
	GameSession.build_channel = identity_manager.get_build_channel()
	GameSession.can_host_lan = identity_manager.can_host_lan()
	GameSession.lan_transport_modes = identity_manager.get_lan_transport_modes()
	GameSession.preferred_lan_transport = identity_manager.get_preferred_lan_transport()
	GameSession.dedicated_transport_mode = identity_manager.get_dedicated_transport_mode()


# ── Button callbacks ──────────────────────────────────────────────────────────


func _on_single_player() -> void:
	_show_world_list()


func _on_multiplayer() -> void:
	_sync_identity_to_session()
	if GameSession.can_host_lan:
		var transport_text := "LAN"
		if GameSession.lan_transport_modes.has("steam_p2p"):
			transport_text = "LAN + Steam P2P"
		_set_status("多人游戏 — 可加入服务器，也可开启局域网（%s，界面即将推出）" %
			transport_text)
	else:
		_set_status("多人游戏 — 可加入服务器；开启局域网需要 Steam 账号")


func _on_settings() -> void:
	_state = State.SETTINGS
	_main_menu_vbox.visible = false
	_settings_ui.open()


func _on_quit() -> void:
	get_tree().quit()


func _on_refresh_worlds() -> void:
	_refresh_world_list()


func _on_new_world() -> void:
	_show_new_world_dialog()


func _on_load_world() -> void:
	if _selected_world_index < 0 or _selected_world_index >= _world_list_filtered.size():
		_set_status("请先选择一个世界")
		return
	var entry: Dictionary = _world_list_filtered[_selected_world_index]
	_load_world(entry)
	_start_game()


func _on_delete_world() -> void:
	if _selected_world_index < 0 or _selected_world_index >= _world_list_filtered.size():
		_set_status("请先选择一个世界")
		return
	var entry: Dictionary = _world_list_filtered[_selected_world_index]
	var world_name := str(entry.get("name", ""))
	var folder := str(entry.get("folder", ""))
	var dialog := ConfirmationDialog.new()
	dialog.title = "删除世界"
	dialog.dialog_text = "确定要删除世界 \"%s\" 吗？\n此操作不可撤销！" % world_name
	dialog.ok_button_text = "删除"
	dialog.cancel_button_text = "取消"
	dialog.confirmed.connect(_confirm_delete_world.bind(folder))
	add_child(dialog)
	dialog.popup_centered(Vector2i(420, 140))
	dialog.canceled.connect(dialog.queue_free)
	dialog.confirmed.connect(dialog.queue_free)


func _on_rename_world() -> void:
	if _selected_world_index < 0 or _selected_world_index >= _world_list_filtered.size():
		_set_status("请先选择一个世界")
		return
	var entry: Dictionary = _world_list_filtered[_selected_world_index]
	_rename_world_folder = str(entry.get("folder", ""))
	var current_name := str(entry.get("name", ""))

	_rename_dialog = Window.new()
	_rename_dialog.title = "重命名世界"
	_rename_dialog.transient = true
	_rename_dialog.exclusive = true
	_rename_dialog.initial_position = Window.WINDOW_INITIAL_POSITION_CENTER_MAIN_WINDOW_SCREEN
	_rename_dialog.close_requested.connect(_on_rename_cancel)
	add_child(_rename_dialog)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 12)
	vbox.anchor_right = 1.0
	vbox.anchor_bottom = 1.0
	vbox.add_theme_constant_override("margin", 12)
	_rename_dialog.add_child(vbox)

	var label := Label.new()
	label.text = "输入新的世界名称："
	label.add_theme_color_override("font_color", COLOR_TEXT)
	vbox.add_child(label)

	_rename_input = LineEdit.new()
	_rename_input.text = current_name
	_rename_input.custom_minimum_size = Vector2(300, 32)
	_rename_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(_rename_input)

	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var confirm_btn := _make_button("确认", 100, 36)
	confirm_btn.pressed.connect(_confirm_rename_world)
	btn_row.add_child(confirm_btn)

	var cancel_btn := _make_button("取消", 100, 36)
	cancel_btn.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	cancel_btn.pressed.connect(_on_rename_cancel)
	btn_row.add_child(cancel_btn)

	vbox.add_child(btn_row)

	_rename_dialog.popup_centered(Vector2i(340, 150))
	_rename_input.grab_focus()
	_rename_input.select_all()


func _on_rename_cancel() -> void:
	if _rename_dialog:
		_rename_dialog.queue_free()
		_rename_dialog = null
	_rename_world_folder = ""


func _confirm_rename_world() -> void:
	if _rename_world_folder == "":
		return
	var new_name := _rename_input.text.strip_edges()
	if new_name == "":
		_set_status("世界名称不能为空")
		return
	var meta_path := SAVE_ROOT + _rename_world_folder + "/" + META_FILE
	if not FileAccess.file_exists(meta_path):
		_set_status("世界文件不存在")
		_on_rename_cancel()
		return
	var f := FileAccess.open(meta_path, FileAccess.READ)
	if f == null:
		_on_rename_cancel()
		return
	var json_text := f.get_as_text()
	f.close()
	var json := JSON.new()
	if json.parse(json_text) != OK:
		_on_rename_cancel()
		return
	var data: Dictionary = json.data
	data["name"] = new_name
	var out := FileAccess.open(meta_path, FileAccess.WRITE)
	if out == null:
		_set_status("重命名失败！")
		_on_rename_cancel()
		return
	out.store_string(JSON.stringify(data, "\t"))
	out.close()
	_set_status("世界已重命名")
	_on_rename_cancel()
	_refresh_world_list()


func _on_search_changed(text: String) -> void:
	_search_text = text
	_refresh_world_list()


func _on_sort_changed(index: int) -> void:
	_sort_mode = index
	_refresh_world_list()


func _confirm_delete_world(folder: String) -> void:
	var full_path := SAVE_ROOT + folder + "/"
	if not DirAccess.dir_exists_absolute(full_path):
		_set_status("世界文件夹不存在")
		_refresh_world_list()
		return
	var err := _remove_dir_recursive(full_path)
	if err != OK:
		_set_status("删除世界失败！")
		return
	_set_status("世界已删除")
	_selected_world_index = -1
	_refresh_world_list()


func _remove_dir_recursive(path: String) -> Error:
	var dir := DirAccess.open(path)
	if dir == null:
		return FAILED
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if file_name == "." or file_name == "..":
			file_name = dir.get_next()
			continue
		var full_path := path + "/" + file_name
		if dir.current_is_dir():
			_remove_dir_recursive(full_path)
		else:
			dir.remove(file_name)
		file_name = dir.get_next()
	dir.list_dir_end()
	return DirAccess.remove_absolute(path)


func _on_create_world() -> void:
	var world_name := _new_world_input.text.strip_edges()
	if world_name == "":
		_set_status("世界名称不能为空")
		return
	var mode := _get_selected_universe_mode()
	var seed_value := _parse_seed_value()
	if _create_world(world_name, mode, seed_value):
		_start_game()


func _on_cancel_new_world() -> void:
	_show_world_list()


func _on_universe_mode_changed(index: int) -> void:
	_planet_settings_btn.visible = (index == 1)
	if index == 0:
		_temp_planet_overrides = {}


func _on_randomize_seed() -> void:
	_seed_input.text = str(randi() % 1000000)


# ── World list item click ─────────────────────────────────────────────────────


func _on_world_item_input(event: InputEvent, index: int) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			_selected_world_index = index
			_update_world_list_selection()
			if mb.double_click:
				_on_load_world()
