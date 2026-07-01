class_name MachinePanel extends Control

# Generic, data-driven machine GUI panel. Reads a PanelLayout from the
# C++ MachineDefinition registry and dynamically builds slot/progress/bar
# widgets. Runtime state is refreshed from a FurnaceData reference, mapped
# by element role/type — so any furnace-family machine renders with this
# single scene and no per-machine .tscn.

signal closed

var player: PlayerController
var _furnace_data: GDFurnaceData = null
var _furnace_manager: FurnaceManager = null
var _type_key: String = ""
var _furnace_key: String = ""
var _furnace_dimension: StringName = &""
var _furnace_cell: Vector3i = Vector3i.ZERO
var _is_open := false

# element_id -> {node, element_type, role, slot_index}
var _widgets: Dictionary = {}

@onready var _panel := $Panel
@onready var _title := $Panel/Title
@onready var _content := $Panel/Content
@onready var _close_btn := $Panel/CloseBtn


func _ready() -> void:
	visible = false
	_close_btn.pressed.connect(_on_close)


func _process(_delta: float) -> void:
	if not _is_open or _furnace_data == null:
		return
	_refresh_display()


# Open the panel for a furnace-family machine. type_key selects which
# registered PanelLayout to build; defaults to "furnace".
func open(furnace_data, dimension: StringName, cell: Vector3i, furnace_manager = null, type_key: String = "furnace") -> void:
	_furnace_data = furnace_data
	_furnace_key = "%s,%d,%d,%d" % [dimension, cell.x, cell.y, cell.z]
	_furnace_dimension = dimension
	_furnace_cell = cell
	_furnace_manager = furnace_manager
	_type_key = type_key
	_is_open = true
	_build_from_layout(type_key)
	visible = true
	_refresh_display()


func close() -> void:
	_is_open = false
	visible = false
	_furnace_data = null
	_furnace_key = ""
	_furnace_dimension = &""
	_furnace_cell = Vector3i.ZERO
	_clear_widgets()
	closed.emit()


func _on_close() -> void:
	close()


func set_player(p: PlayerController) -> void:
	player = p


# ============================================================
# Layout build — consume PanelLayout from the C++ registry
# ============================================================

func _build_from_layout(type_key: String) -> void:
	_clear_widgets()
	var def := GDMachineDefinitionRegistry.get_definition(type_key)
	if def.is_empty():
		push_warning("MachinePanel: no machine definition for '%s'" % type_key)
		return
	var layout: Dictionary = def.get("panel_layout", {})
	_title.text = String(def.get("display_name", type_key))

	var panel_w: float = float(layout.get("panel_width", 320.0))
	var panel_h: float = float(layout.get("panel_height", 220.0))
	# Center the panel and size it from layout data.
	_panel.offset_left = -panel_w * 0.5
	_panel.offset_top = -panel_h * 0.5
	_panel.offset_right = panel_w * 0.5
	_panel.offset_bottom = panel_h * 0.5
	_content.offset_right = panel_w
	_content.offset_bottom = panel_h

	var elements: Array = layout.get("elements", [])
	for e in elements:
		_build_element(e)


func _clear_widgets() -> void:
	for key in _widgets.keys():
		var entry: Dictionary = _widgets[key]
		var node: Node = entry.get("node", null)
		if node != null and is_instance_valid(node):
			node.queue_free()
	_widgets.clear()


func _build_element(element: Dictionary) -> void:
	var element_id: String = element.get("element_id", "")
	var element_type: String = element.get("element_type", "")
	var rect: Array = element.get("rect", [0.0, 0.0, 0.0, 0.0])
	if element_id.is_empty():
		return

	if element_type == "slot":
		var node := _make_slot(rect)
		_content.add_child(node)
		_widgets[element_id] = {
			"node": node,
			"element_type": element_type,
			"role": String(element.get("role", "")),
			"slot_index": int(element.get("slot_index", 0)),
			"icon": node.get_node("Icon"),
			"label": node.get_node("Label"),
		}
	elif element_type == "progress_bar":
		var node := _make_progress_bar(rect)
		_content.add_child(node)
		_widgets[element_id] = {"node": node, "element_type": element_type}
	elif element_type == "fuel_bar":
		var node := _make_fuel_bar(rect)
		_content.add_child(node)
		_widgets[element_id] = {"node": node, "element_type": element_type}


# Build a slot: background panel + item icon + count label.
func _make_slot(rect: Array) -> Panel:
	var slot := Panel.new()
	slot.offset_left = float(rect[0])
	slot.offset_top = float(rect[1])
	slot.offset_right = float(rect[2])
	slot.offset_bottom = float(rect[3])
	slot.color = Color(0.12, 0.12, 0.14, 0.9)

	var icon := TextureRect.new()
	icon.name = "Icon"
	icon.layout_mode = 0
	icon.offset_left = 4.0
	icon.offset_top = 4.0
	icon.offset_right = 52.0
	icon.offset_bottom = 52.0
	icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	slot.add_child(icon)

	var lbl := Label.new()
	lbl.name = "Label"
	lbl.layout_mode = 0
	lbl.offset_left = 2.0
	lbl.offset_top = 48.0
	lbl.offset_right = 54.0
	lbl.offset_bottom = 64.0
	lbl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	slot.add_child(lbl)
	return slot


func _make_progress_bar(rect: Array) -> ProgressBar:
	var bar := ProgressBar.new()
	bar.offset_left = float(rect[0])
	bar.offset_top = float(rect[1])
	bar.offset_right = float(rect[2])
	bar.offset_bottom = float(rect[3])
	bar.max_value = 100.0
	bar.value = 0.0
	bar.show_percentage = false
	return bar


# Fuel burn bar rendered as a vertical fill (bottom-to-top).
func _make_fuel_bar(rect: Array) -> TextureProgressBar:
	var bar := TextureProgressBar.new()
	bar.offset_left = float(rect[0])
	bar.offset_top = float(rect[1])
	bar.offset_right = float(rect[2])
	bar.offset_bottom = float(rect[3])
	bar.fill_mode = TextureProgressBar.FILL_BOTTOM_TO_TOP
	bar.value = 0.0
	return bar


# ============================================================
# Refresh — read furnace state and update widgets by role/type
# ============================================================

func _refresh_display() -> void:
	if _furnace_data == null:
		return

	for element_id in _widgets.keys():
		var entry: Dictionary = _widgets[element_id]
		var element_type: String = entry["element_type"]
		match element_type:
			"slot":
				_refresh_slot(entry)
			"progress_bar":
				(entry["node"] as ProgressBar).value = _furnace_data.get_progress_ratio() * 100.0
			"fuel_bar":
				(entry["node"] as TextureProgressBar).value = _furnace_data.get_fuel_ratio() * 100.0


func _refresh_slot(entry: Dictionary) -> void:
	var role: String = entry.get("role", "")
	var icon: TextureRect = entry.get("icon", null)
	var lbl: Label = entry.get("label", null)
	if icon == null or lbl == null:
		return

	var item_id: int = 0
	var count: int = 0
	match role:
		"input":
			item_id = _furnace_data.input_item_id
			count = _furnace_data.input_count
		"fuel":
			item_id = _furnace_data.fuel_item_id
		"output":
			item_id = _furnace_data.output_item_id
			count = _furnace_data.output_count

	if item_id > 0 and (count > 0 or role == "fuel"):
		var name := tr(GDCraftingManager.get_item_title_key(item_id))
		if count > 0:
			lbl.text = "%s x%d" % [name, count]
		else:
			lbl.text = name
		icon.visible = true
	else:
		lbl.text = ""
		icon.visible = false


# ============================================================
# Input — click handling mapped by slot role
# ============================================================

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_handle_click()


func _handle_click() -> void:
	if player == null or _furnace_data == null:
		return

	var command_server: GameCommandServer = player.get_command_server()
	if command_server == null:
		return

	var mouse := get_global_mouse_position()
	for element_id in _widgets.keys():
		var entry: Dictionary = _widgets[element_id]
		if entry.get("element_type", "") != "slot":
			continue
		var node: Panel = entry.get("node", null)
		if node == null or not node.get_global_rect().has_point(mouse):
			continue
		var role: String = entry.get("role", "")
		match role:
			"output":
				command_server.submit_command({
					"type": GameCommandServer.COMMAND_FURNACE_TAKE_OUTPUT,
					"dimension": _furnace_dimension,
					"cell": _furnace_cell,
				})
				return
			"input":
				var held_id := _get_held_item_id()
				if held_id > 0 and _is_valid_input(held_id):
					command_server.submit_command({
						"type": GameCommandServer.COMMAND_FURNACE_INSERT_INPUT,
						"dimension": _furnace_dimension,
						"cell": _furnace_cell,
						"item_id": held_id,
					})
				return
			"fuel":
				var held_id := _get_held_item_id()
				if held_id > 0 and _is_fuel(held_id):
					command_server.submit_command({
						"type": GameCommandServer.COMMAND_FURNACE_INSERT_FUEL,
						"dimension": _furnace_dimension,
						"cell": _furnace_cell,
						"item_id": held_id,
					})
				return


func _get_held_item_id() -> int:
	if player:
		return player.get_equipped_item_id()
	return 0


func _is_valid_input(item_id: int) -> bool:
	if _furnace_manager == null:
		return false
	return not _furnace_manager.get_recipe_for(item_id).is_empty()


func _is_fuel(item_id: int) -> bool:
	if _furnace_manager == null:
		return false
	return _furnace_manager.get_fuel_burn_time(item_id) > 0.0
