# ExitMenu — In-game pause/exit menu shown when player presses ESC.
# Offers: Resume, Return to Main Menu, Quit Game.
# Built programmatically to match MainMenu's visual style.
class_name ExitMenu
extends Control

signal resume_requested
signal return_to_main_menu_requested
signal quit_requested
signal settings_requested

# ── Theme colors (match MainMenu) ────────────────────────────────────────────

const COLOR_BG := Color(0.02, 0.025, 0.04, 0.75)
const COLOR_PANEL := Color(0.06, 0.065, 0.08, 0.97)
const COLOR_BUTTON := Color(0.10, 0.11, 0.14, 1.0)
const COLOR_BUTTON_HOVER := Color(0.18, 0.20, 0.26, 1.0)
const COLOR_BUTTON_PRESSED := Color(0.25, 0.28, 0.35, 1.0)
const COLOR_ACCENT := Color(0.35, 0.55, 0.75, 1.0)
const COLOR_TEXT := Color(0.85, 0.87, 0.90, 1.0)
const COLOR_TEXT_DIM := Color(0.50, 0.52, 0.56, 1.0)

# ── Layout metrics ───────────────────────────────────────────────────────────

const BUTTON_WIDTH := 260
const BUTTON_HEIGHT := 44
const BUTTON_GAP := 10
const TITLE_FONT_SIZE := 24

var _vbox: VBoxContainer


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	# Use IGNORE by default so the menu never blocks input when closed.
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_ui()
	visible = false


func _build_ui() -> void:
	# Dim full-screen background.
	var bg := ColorRect.new()
	bg.name = "DimBackground"
	bg.color = COLOR_BG
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(bg)

	# Centered panel.
	var center := CenterContainer.new()
	center.name = "Center"
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var panel_bg := PanelContainer.new()
	panel_bg.name = "Panel"
	panel_bg.custom_minimum_size = Vector2(BUTTON_WIDTH + 24, 0)
	var style := StyleBoxFlat.new()
	style.bg_color = COLOR_PANEL
	style.border_color = Color(0.15, 0.16, 0.20, 1.0)
	style.set_border_width_all(1)
	style.set_content_margin_all(12)
	style.set_corner_radius_all(4)
	panel_bg.add_theme_stylebox_override("panel", style)
	center.add_child(panel_bg)

	_vbox = VBoxContainer.new()
	_vbox.name = "VBox"
	_vbox.add_theme_constant_override("separation", BUTTON_GAP)
	panel_bg.add_child(_vbox)

	# Title.
	var title := Label.new()
	title.name = "TitleLabel"
	title.text = tr("exit_menu.title")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", TITLE_FONT_SIZE)
	title.add_theme_color_override("font_color", COLOR_TEXT)
	_vbox.add_child(title)

	# Buttons.
	_add_button("exit_menu.resume", _on_resume)
	_add_button("exit_menu.settings", _on_settings)
	_add_button("exit_menu.main_menu", _on_return_to_main)
	_add_button("exit_menu.quit", _on_quit)


func _add_button(text: String, callback: Callable) -> void:
	var btn := Button.new()
	btn.text = tr(text)
	btn.custom_minimum_size = Vector2(BUTTON_WIDTH, BUTTON_HEIGHT)

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
	btn.pressed.connect(callback)
	_vbox.add_child(btn)


# ── Public API ───────────────────────────────────────────────────────────────


func open() -> void:
	visible = true
	mouse_filter = Control.MOUSE_FILTER_STOP


func close() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func is_open() -> bool:
	return visible


# ── Button callbacks ─────────────────────────────────────────────────────────


func _on_resume() -> void:
	resume_requested.emit()


func _on_settings() -> void:
	settings_requested.emit()


func _on_return_to_main() -> void:
	return_to_main_menu_requested.emit()


func _on_quit() -> void:
	quit_requested.emit()
