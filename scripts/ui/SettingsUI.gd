# SettingsUI — 设置界面，包含按键绑定修改功能。
# 风格与 MainMenu / ExitMenu 统一（深色主题，程序化构建）。
class_name SettingsUI
extends Control

signal closed

# ── Theme colors (match MainMenu / ExitMenu) ─────────────────────────────────

const COLOR_BG := Color(0.02, 0.025, 0.04, 0.75)
const COLOR_PANEL := Color(0.06, 0.065, 0.08, 0.97)
const COLOR_BUTTON := Color(0.10, 0.11, 0.14, 1.0)
const COLOR_BUTTON_HOVER := Color(0.18, 0.20, 0.26, 1.0)
const COLOR_BUTTON_PRESSED := Color(0.25, 0.28, 0.35, 1.0)
const COLOR_ACCENT := Color(0.35, 0.55, 0.75, 1.0)
const COLOR_TEXT := Color(0.85, 0.87, 0.90, 1.0)
const COLOR_TEXT_DIM := Color(0.50, 0.52, 0.56, 1.0)
const COLOR_ROW_BG := Color(0.08, 0.085, 0.10, 1.0)
const COLOR_LISTENING := Color(0.20, 0.35, 0.50, 1.0)

# ── Layout ───────────────────────────────────────────────────────────────────

const PANEL_WIDTH := 480
const PANEL_HEIGHT := 420
const ROW_HEIGHT := 40
const ROW_GAP := 4
const BUTTON_WIDTH := 120
const BUTTON_HEIGHT := 36

# ── State ────────────────────────────────────────────────────────────────────

var _vbox: VBoxContainer
var _rows: Array[Control] = []
var _listening_action: StringName = &""
var _listening_button: Button = null


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_ui()
	visible = false


func _input(event: InputEvent) -> void:
	if _listening_action == &"":
		return
	if not event is InputEventKey:
		return
	var key_event: InputEventKey = event
	# 忽略松开事件和重复事件。
	if not key_event.pressed or key_event.echo:
		return
	# ESC 取消绑定。
	var key: Key = key_event.keycode
	if key == KEY_ESCAPE:
		_stop_listening()
		get_viewport().set_input_as_handled()
		return
	# 应用新绑定。
	KeyBindings.rebind(_listening_action, key)
	_update_row_label(_listening_action)
	_stop_listening()
	get_viewport().set_input_as_handled()


# ── Public API ───────────────────────────────────────────────────────────────


func open() -> void:
	visible = true
	mouse_filter = Control.MOUSE_FILTER_STOP
	_refresh_all_rows()


# Hide the panel without notifying its owner. Use this when the owner is
# already applying a state transition, such as MainMenu._show_main_menu().
func dismiss() -> void:
	_stop_listening()
	visible = false
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func close() -> void:
	if not visible:
		return
	dismiss()
	closed.emit()


func is_open() -> bool:
	return visible


# ── UI construction ──────────────────────────────────────────────────────────


func _build_ui() -> void:
	# 全屏暗背景。
	var bg := ColorRect.new()
	bg.color = COLOR_BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(bg)

	# 居中容器。
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var panel_bg := PanelContainer.new()
	panel_bg.custom_minimum_size = Vector2(PANEL_WIDTH, PANEL_HEIGHT)
	var style := StyleBoxFlat.new()
	style.bg_color = COLOR_PANEL
	style.border_color = Color(0.15, 0.16, 0.20, 1.0)
	style.set_border_width_all(1)
	style.set_content_margin_all(16)
	style.set_corner_radius_all(4)
	panel_bg.add_theme_stylebox_override("panel", style)
	center.add_child(panel_bg)

	_vbox = VBoxContainer.new()
	_vbox.add_theme_constant_override("separation", 8)
	panel_bg.add_child(_vbox)

	# 标题。
	var title := Label.new()
	title.text = "按键绑定"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", COLOR_TEXT)
	_vbox.add_child(title)

	# 按键行列表。
	for def in KeyBindings.ACTION_DEFS:
		var action: StringName = def[0]
		var display_name: String = def[1]
		var row := _make_key_row(action, display_name)
		_rows.append(row)
		_vbox.add_child(row)

	# 底部按钮。
	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 10)
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER

	var btn_reset := _make_button("恢复默认", BUTTON_WIDTH, BUTTON_HEIGHT)
	btn_reset.pressed.connect(_on_reset)
	btn_row.add_child(btn_reset)

	var btn_back := _make_button("返回", BUTTON_WIDTH, BUTTON_HEIGHT)
	btn_back.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	btn_back.pressed.connect(close)
	btn_row.add_child(btn_back)

	_vbox.add_child(btn_row)


func _make_key_row(action: StringName, display_name: String) -> Control:
	var row := PanelContainer.new()
	row.custom_minimum_size = Vector2(0, ROW_HEIGHT)
	row.set_meta("action", action)
	var style := StyleBoxFlat.new()
	style.bg_color = COLOR_ROW_BG
	style.set_corner_radius_all(3)
	style.set_content_margin_all(8)
	row.add_theme_stylebox_override("panel", style)

	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 8)
	row.add_child(hbox)

	# 动作名称。
	var name_label := Label.new()
	name_label.text = display_name
	name_label.add_theme_font_size_override("font_size", 15)
	name_label.add_theme_color_override("font_color", COLOR_TEXT)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_label.set_meta("is_name_label", true)
	hbox.add_child(name_label)

	# 当前按键按钮（点击后进入监听模式）。
	var key_btn := Button.new()
	key_btn.custom_minimum_size = Vector2(120, 28)
	key_btn.set_meta("action", action)
	_style_key_button(key_btn, false)
	key_btn.pressed.connect(_on_key_button_pressed.bind(action, key_btn))
	hbox.add_child(key_btn)

	return row


func _style_key_button(btn: Button, listening: bool) -> void:
	var normal := StyleBoxFlat.new()
	normal.bg_color = COLOR_LISTENING if listening else COLOR_BUTTON
	normal.set_corner_radius_all(4)
	normal.set_content_margin_all(6)
	normal.set_border_width_all(1)
	normal.border_color = COLOR_ACCENT if listening else Color(0.18, 0.19, 0.22, 1.0)

	var hover := normal.duplicate() as StyleBoxFlat
	hover.bg_color = COLOR_BUTTON_HOVER if not listening else COLOR_LISTENING

	var pressed := normal.duplicate() as StyleBoxFlat
	pressed.bg_color = COLOR_BUTTON_PRESSED

	btn.add_theme_stylebox_override("normal", normal)
	btn.add_theme_stylebox_override("hover", hover)
	btn.add_theme_stylebox_override("pressed", pressed)
	btn.add_theme_color_override("font_color", COLOR_TEXT)
	btn.add_theme_font_size_override("font_size", 14)


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


# ── Row update ───────────────────────────────────────────────────────────────


func _refresh_all_rows() -> void:
	for row in _rows:
		var action: StringName = row.get_meta("action")
		_update_row_label(action)


func _update_row_label(action: StringName) -> void:
	for row in _rows:
		if row.get_meta("action") != action:
			continue
		var hbox: HBoxContainer = row.get_child(0)
		for child in hbox.get_children():
			if child is Button and child.has_meta("action"):
				var key: Key = KeyBindings.get_action_key(action)
				(child as Button).text = KeyBindings.key_name(key)
				break
		break


# ── Listening mode ───────────────────────────────────────────────────────────


func _on_key_button_pressed(action: StringName, btn: Button) -> void:
	# 如果已在监听另一个按键，先取消。
	_stop_listening()
	_listening_action = action
	_listening_button = btn
	btn.text = "按下新按键..."
	_style_key_button(btn, true)


func _stop_listening() -> void:
	if _listening_action == &"":
		return
	# 恢复按钮外观。
	if _listening_button != null and is_instance_valid(_listening_button):
		var key: Key = KeyBindings.get_action_key(_listening_action)
		_listening_button.text = KeyBindings.key_name(key)
		_style_key_button(_listening_button, false)
	_listening_action = &""
	_listening_button = null


# ── Button callbacks ─────────────────────────────────────────────────────────


func _on_reset() -> void:
	KeyBindings.reset_all()
	_refresh_all_rows()
