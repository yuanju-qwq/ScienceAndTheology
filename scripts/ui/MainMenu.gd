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
const PANEL_HEIGHT := 440

# ── State ─────────────────────────────────────────────────────────────────────

enum State { MAIN_MENU, WORLD_LIST, NEW_WORLD_DIALOG }

var _state: State = State.MAIN_MENU
var _world_list: Array[Dictionary] = []
var _selected_world_index: int = -1

# ── Node references (built in _build_ui) ──────────────────────────────────────

var _main_menu_vbox: VBoxContainer
var _world_list_panel: Control
var _world_list_container: VBoxContainer
var _new_world_panel: Control
var _new_world_input: LineEdit
var _status_label: Label

# ── Lifecycle ─────────────────────────────────────────────────────────────────


func _ready() -> void:
	_ensure_save_root()
	_set_anchors_preset(Control.PRESET_FULL_RECT)
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
		return {"name": folder_name, "folder": folder_name, "date": ""}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {"name": folder_name, "folder": folder_name, "date": ""}
	var json_text := f.get_as_text()
	f.close()
	var json := JSON.new()
	if json.parse(json_text) != OK:
		return {"name": folder_name, "folder": folder_name, "date": ""}
	var data: Dictionary = json.data
	return {
		"name": str(data.get("name", folder_name)),
		"folder": folder_name,
		"date": str(data.get("created_at", "")),
	}


func _compare_worlds_by_date(a: Dictionary, b: Dictionary) -> bool:
	return str(a.get("date", "")) > str(b.get("date", ""))


func _create_world(world_name: String) -> bool:
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
	return true


func _load_world(entry: Dictionary) -> void:
	GameSession.world_name = str(entry.get("name", ""))
	GameSession.save_path = SAVE_ROOT + str(entry.get("folder", "")) + "/"


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
	title.text = "科学与神学"
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
	subtitle.text = "Science & Theology"
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


# ── Main menu buttons ─────────────────────────────────────────────────────────


func _build_main_menu() -> void:
	_main_menu_vbox = VBoxContainer.new()
	_main_menu_vbox.name = "MainMenuVBox"
	_main_menu_vbox.add_theme_constant_override("separation", BUTTON_GAP)
	add_child(_main_menu_vbox)

	var labels := ["单人游戏", "多人游戏", "设置", "退出游戏"]
	var callbacks: Array[Callable] = [
		_on_single_player,
		_on_multiplayer,
		_on_settings,
		_on_quit,
	]

	for i in labels.size():
		var btn := _make_button(labels[i], BUTTON_WIDTH, BUTTON_HEIGHT)
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
	title_label.text = "选择世界"
	title_label.add_theme_font_size_override("font_size", 22)
	title_label.add_theme_color_override("font_color", COLOR_TEXT)
	title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title_row.add_child(title_label)

	var refresh_btn := _make_button("刷新", 60, 28)
	refresh_btn.add_theme_font_size_override("font_size", 12)
	refresh_btn.pressed.connect(_on_refresh_worlds)
	title_row.add_child(refresh_btn)

	vbox.add_child(title_row)

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

	var btn_new := _make_button("新建世界", 120, BUTTON_HEIGHT)
	btn_new.pressed.connect(_on_new_world)
	btn_row.add_child(btn_new)

	var btn_load := _make_button("加载世界", 120, BUTTON_HEIGHT)
	btn_load.pressed.connect(_on_load_world)
	btn_row.add_child(btn_load)

	var btn_back := _make_button("返回", 120, BUTTON_HEIGHT)
	btn_back.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	btn_back.pressed.connect(_show_main_menu)
	btn_row.add_child(btn_back)

	vbox.add_child(btn_row)


# ── New world dialog ──────────────────────────────────────────────────────────


func _build_new_world_dialog() -> void:
	_new_world_panel = Control.new()
	_new_world_panel.name = "NewWorldPanel"
	_new_world_panel.visible = false
	add_child(_new_world_panel)

	var dialog_bg := PanelContainer.new()
	dialog_bg.name = "DialogBg"
	dialog_bg.custom_minimum_size = Vector2(360, 160)
	var dialog_style := StyleBoxFlat.new()
	dialog_style.bg_color = COLOR_PANEL
	dialog_style.border_color = Color(0.15, 0.16, 0.20, 1.0)
	dialog_style.set_border_width_all(1)
	dialog_style.set_content_margin_all(16)
	dialog_style.set_corner_radius_all(4)
	dialog_bg.add_theme_stylebox_override("panel", dialog_style)
	_new_world_panel.add_child(dialog_bg)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	dialog_bg.add_child(vbox)

	var dialog_title := Label.new()
	dialog_title.text = "新建世界"
	dialog_title.add_theme_font_size_override("font_size", 20)
	dialog_title.add_theme_color_override("font_color", COLOR_TEXT)
	vbox.add_child(dialog_title)

	_new_world_input = LineEdit.new()
	_new_world_input.name = "WorldNameInput"
	_new_world_input.placeholder_text = "输入世界名称"
	_new_world_input.custom_minimum_size = Vector2(0, 32)
	vbox.add_child(_new_world_input)

	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 8)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var btn_create := _make_button("创建", 120, BUTTON_HEIGHT)
	btn_create.pressed.connect(_on_create_world)
	btn_row.add_child(btn_create)

	var btn_cancel := _make_button("取消", 120, BUTTON_HEIGHT)
	btn_cancel.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	btn_cancel.pressed.connect(_on_cancel_new_world)
	btn_row.add_child(btn_cancel)

	vbox.add_child(btn_row)


# ── Button factory ────────────────────────────────────────────────────────────


func _make_button(text: String, width: int, height: int) -> Button:
	var btn := Button.new()
	btn.text = text
	btn.custom_minimum_size = Vector2(width, height)

	var normal := StyleBoxFlat.new()
	normal.bg_color = COLOR_BUTTON
	normal.set_corner_radius_all(4)
	normal.set_content_margin_all(8)
	normal.set_border_width_all(1)
	normal.border_color = Color(0.18, 0.19, 0.22, 1.0)

	var hover := normal.duplicate()
	hover.bg_color = COLOR_BUTTON_HOVER
	hover.border_color = Color(0.30, 0.32, 0.38, 1.0)

	var pressed := normal.duplicate()
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
	_new_world_input.grab_focus()
	_recenter_panels()


# ── World list rendering ──────────────────────────────────────────────────────


func _refresh_world_list() -> void:
	_world_list = _scan_worlds()
	_selected_world_index = -1

	for child in _world_list_container.get_children():
		child.queue_free()

	if _world_list.is_empty():
		var empty_label := Label.new()
		empty_label.text = "暂无存档，请新建世界"
		empty_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		empty_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
		empty_label.add_theme_font_size_override("font_size", 14)
		_world_list_container.add_child(empty_label)
		return

	for i in _world_list.size():
		var item := _make_world_list_item(_world_list[i], i)
		_world_list_container.add_child(item)


func _update_world_list_selection() -> void:
	for child in _world_list_container.get_children():
		if not child is PanelContainer:
			continue
		var idx: int = child.get_meta("world_index", -1)
		var style: StyleBoxFlat = child.get_theme_stylebox("panel")
		if idx == _selected_world_index:
			style.bg_color = COLOR_LIST_ITEM_SELECTED
			style.border_color = COLOR_ACCENT
		else:
			style.bg_color = COLOR_LIST_ITEM
			style.border_color = Color(0.12, 0.13, 0.16, 1.0)


# ── Centering ─────────────────────────────────────────────────────────────────


func _recenter_panels() -> void:
	var vp_size := get_viewport_rect().size
	var center := vp_size / 2.0

	# Main menu vbox.
	_main_menu_vbox.position = center - Vector2(BUTTON_WIDTH / 2.0, 40)

	# World list panel.
	_world_list_panel.position = center - Vector2(PANEL_WIDTH / 2.0, PANEL_HEIGHT / 2.0)

	# New world dialog.
	_new_world_panel.position = center - Vector2(180, 80)


# ── Status label helper ───────────────────────────────────────────────────────


func _set_status(text: String) -> void:
	_status_label.text = text


# ── Button callbacks ──────────────────────────────────────────────────────────


func _on_single_player() -> void:
	_show_world_list()


func _on_multiplayer() -> void:
	_set_status("多人游戏 — 即将推出")


func _on_settings() -> void:
	_set_status("设置 — 即将推出")


func _on_quit() -> void:
	get_tree().quit()


func _on_refresh_worlds() -> void:
	_refresh_world_list()


func _on_new_world() -> void:
	_show_new_world_dialog()


func _on_load_world() -> void:
	if _selected_world_index < 0 or _selected_world_index >= _world_list.size():
		_set_status("请先选择一个世界")
		return
	var entry: Dictionary = _world_list[_selected_world_index]
	_load_world(entry)
	_start_game()


func _on_create_world() -> void:
	var name := _new_world_input.text.strip_edges()
	if name == "":
		_set_status("世界名称不能为空")
		return
	if _create_world(name):
		_start_game()


func _on_cancel_new_world() -> void:
	_show_world_list()


# ── World list item click ─────────────────────────────────────────────────────


func _on_world_item_input(event: InputEvent, index: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		_selected_world_index = index
		_update_world_list_selection()
