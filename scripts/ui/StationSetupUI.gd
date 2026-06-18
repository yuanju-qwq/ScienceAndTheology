# StationSetupUI — popup panel for configuring a new space station.
# Opened when the player right-clicks a "Station Blueprint" item.
# On confirmation, calls UniverseManager.create_station() and
# generates a MapConnector linking the player to the station entrance.
class_name StationSetupUI
extends Control

signal station_confirmed(params: Dictionary)
signal cancelled

var _is_open := false

# UI elements.
var _panel: PanelContainer
var _vbox: VBoxContainer
var _title_label: Label
var _name_edit: LineEdit
var _type_option: OptionButton
var _orbit_spin: SpinBox
var _gravity_spin: SpinBox
var _confirm_btn: Button
var _cancel_btn: Button


func _ready() -> void:
	visible = false
	_build_ui()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE and _is_open:
		close()
		get_viewport().set_input_as_handled()


# --- Public API ---

func is_open() -> bool:
	return _is_open


func open() -> void:
	if _is_open:
		return
	_is_open = true
	visible = true
	_name_edit.grab_focus()


func close() -> void:
	if not _is_open:
		return
	_is_open = false
	visible = false
	cancelled.emit()


# --- UI construction ---

func _build_ui() -> void:
	# Full-screen semi-transparent backdrop.
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP

	var backdrop := ColorRect.new()
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.0, 0.0, 0.0, 0.6)
	add_child(backdrop)

	# Centered panel.
	_panel = PanelContainer.new()
	_panel.set_anchors_preset(Control.PRESET_CENTER)
	_panel.custom_minimum_size = Vector2(360, 0)
	_panel.position = -Vector2(180, 0)
	add_child(_panel)

	# Content layout.
	_vbox = VBoxContainer.new()
	_vbox.add_theme_constant_override("separation", 8)
	_panel.add_child(_vbox)

	# Title.
	_title_label = Label.new()
	_title_label.text = "Create Space Station"
	_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_vbox.add_child(_title_label)

	# Separator.
	_vbox.add_child(HSeparator.new())

	# Station name.
	_vbox.add_child(_make_label("Station Name:"))
	_name_edit = LineEdit.new()
	_name_edit.placeholder_text = "My Station"
	_name_edit.text = "Outpost"
	_vbox.add_child(_name_edit)

	# Station type.
	_vbox.add_child(_make_label("Station Type:"))
	_type_option = OptionButton.new()
	_type_option.add_item("Outpost (1x1x1 chunks)", StationDescriptor.StationType.OUTPOST)
	_type_option.add_item("Habitat (3x1x3 chunks)", StationDescriptor.StationType.HABITAT)
	_type_option.add_item("Factory (5x1x5 chunks)", StationDescriptor.StationType.FACTORY)
	_vbox.add_child(_type_option)

	# Orbit height.
	_vbox.add_child(_make_label("Orbit Height (above surface):"))
	_orbit_spin = SpinBox.new()
	_orbit_spin.min_value = 500.0
	_orbit_spin.max_value = 50000.0
	_orbit_spin.step = 500.0
	_orbit_spin.value = 2000.0
	_orbit_spin.suffix = " units"
	_vbox.add_child(_orbit_spin)

	# Gravity multiplier.
	_vbox.add_child(_make_label("Gravity (1.0 = normal, 0.0 = zero-G):"))
	_gravity_spin = SpinBox.new()
	_gravity_spin.min_value = 0.0
	_gravity_spin.max_value = 2.0
	_gravity_spin.step = 0.1
	_gravity_spin.value = 1.0
	_vbox.add_child(_gravity_spin)

	# Separator.
	_vbox.add_child(HSeparator.new())

	# Buttons.
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 8)
	hbox.alignment = BoxContainer.ALIGNMENT_CENTER

	_confirm_btn = Button.new()
	_confirm_btn.text = "Create Station"
	_confirm_btn.pressed.connect(_on_confirm)
	hbox.add_child(_confirm_btn)

	_cancel_btn = Button.new()
	_cancel_btn.text = "Cancel"
	_cancel_btn.pressed.connect(_on_cancel)
	hbox.add_child(_cancel_btn)

	_vbox.add_child(hbox)


func _make_label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


# --- Button handlers ---

func _on_confirm() -> void:
	var params := {
		"display_name": _name_edit.text.strip_edges(),
		"station_type": _type_option.get_selected_id(),
		"orbit_height": _orbit_spin.value,
		"gravity_multiplier": _gravity_spin.value,
	}
	_is_open = false
	visible = false
	station_confirmed.emit(params)


func _on_cancel() -> void:
	close()
