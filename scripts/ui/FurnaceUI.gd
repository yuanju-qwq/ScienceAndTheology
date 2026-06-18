class_name FurnaceUI extends Control

signal closed

var player: PlayerController
var _furnace_manager: FurnaceManager = null
var _furnace_data: FurnaceData = null
var _furnace_key: String = ""
var _furnace_dimension: StringName = &""
var _furnace_cell: Vector3i = Vector3i.ZERO
var _is_open := false

@onready var _panel := $Panel
@onready var _title := $Panel/Title
@onready var _input_slot := $Panel/InputSlot
@onready var _input_icon := $Panel/InputSlot/Icon
@onready var _input_label := $Panel/InputSlot/Label
@onready var _fuel_slot := $Panel/FuelSlot
@onready var _fuel_icon := $Panel/FuelSlot/Icon
@onready var _fuel_label := $Panel/FuelSlot/Label
@onready var _output_slot := $Panel/OutputSlot
@onready var _output_icon := $Panel/OutputSlot/Icon
@onready var _output_label := $Panel/OutputSlot/Label
@onready var _progress_bar := $Panel/ProgressBar
@onready var _burn_bar := $Panel/BurnBar
@onready var _close_btn := $Panel/CloseBtn


func _ready() -> void:
	visible = false
	_close_btn.pressed.connect(_on_close)


func _process(_delta: float) -> void:
	if not _is_open or _furnace_data == null:
		return
	_refresh_display()


func open(furnace_data, dimension: StringName, cell: Vector3i, furnace_manager = null) -> void:
	_furnace_data = furnace_data
	_furnace_key = "%s,%d,%d,%d" % [dimension, cell.x, cell.y, cell.z]
	_furnace_dimension = dimension
	_furnace_cell = cell
	_furnace_manager = furnace_manager
	_is_open = true
	visible = true
	_refresh_display()


func close() -> void:
	_is_open = false
	visible = false
	_furnace_data = null
	_furnace_key = ""
	_furnace_dimension = &""
	_furnace_cell = Vector3i.ZERO
	closed.emit()


func _on_close() -> void:
	close()


func set_player(p: PlayerController) -> void:
	player = p


func _refresh_display() -> void:
	if _furnace_data == null:
		return

	# Input slot
	if _furnace_data.input_item_id > 0 and _furnace_data.input_count > 0:
		var name := tr(GDCraftingManager.get_item_title_key(_furnace_data.input_item_id))
		_input_label.text = "%s x%d" % [name, _furnace_data.input_count]
		_input_icon.visible = true
	else:
		_input_label.text = ""
		_input_icon.visible = false

	# Fuel slot
	if _furnace_data.fuel_item_id > 0:
		var name := tr(GDCraftingManager.get_item_title_key(_furnace_data.fuel_item_id))
		_fuel_label.text = name
		_fuel_icon.visible = true
	else:
		_fuel_label.text = ""
		_fuel_icon.visible = false

	# Output slot
	if _furnace_data.output_item_id > 0 and _furnace_data.output_count > 0:
		var name := tr(GDCraftingManager.get_item_title_key(_furnace_data.output_item_id))
		_output_label.text = "%s x%d" % [name, _furnace_data.output_count]
		_output_icon.visible = true
	else:
		_output_label.text = ""
		_output_icon.visible = false

	# Progress bar
	_progress_bar.value = _furnace_data.get_progress_ratio() * 100.0

	# Burn bar
	_burn_bar.value = _furnace_data.get_fuel_ratio() * 100.0


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

	# Output slot click - take items
	if _output_slot.get_global_rect().has_point(get_global_mouse_position()):
		command_server.submit_command({
			"type": GameCommandServer.COMMAND_FURNACE_TAKE_OUTPUT,
			"dimension": _furnace_dimension,
			"cell": _furnace_cell,
		})
		return

	# Input slot click - insert items
	if _input_slot.get_global_rect().has_point(get_global_mouse_position()):
		var held_id := _get_held_item_id()
		if held_id > 0 and _is_valid_input(held_id):
			command_server.submit_command({
				"type": GameCommandServer.COMMAND_FURNACE_INSERT_INPUT,
				"dimension": _furnace_dimension,
				"cell": _furnace_cell,
				"item_id": held_id,
			})
		return

	# Fuel slot click - insert fuel
	if _fuel_slot.get_global_rect().has_point(get_global_mouse_position()):
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
