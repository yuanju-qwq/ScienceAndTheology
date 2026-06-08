class_name FurnaceUI extends Control

signal closed

var player: Node
var _furnace_manager = null
var _furnace_data = null
var _furnace_key: String = ""
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


func open(furnace_data, layer: StringName, cell: Vector2i, furnace_manager = null) -> void:
	_furnace_data = furnace_data
	_furnace_key = "%s,%d,%d" % [layer, cell.x, cell.y]
	_furnace_manager = furnace_manager
	_is_open = true
	visible = true
	_refresh_display()


func close() -> void:
	_is_open = false
	visible = false
	_furnace_data = null
	_furnace_key = ""
	closed.emit()


func _on_close() -> void:
	close()


func set_player(p: Node) -> void:
	player = p


func _refresh_display() -> void:
	if _furnace_data == null:
		return

	var db := ItemDatabase

	# Input slot
	if _furnace_data.input_item_id > 0 and _furnace_data.input_count > 0:
		var name := GDCraftingManager.get_item_display_name(_furnace_data.input_item_id)
		_input_label.text = "%s x%d" % [name, _furnace_data.input_count]
		_input_icon.visible = true
	else:
		_input_label.text = ""
		_input_icon.visible = false

	# Fuel slot
	if _furnace_data.fuel_item_id > 0:
		var name := GDCraftingManager.get_item_display_name(_furnace_data.fuel_item_id)
		_fuel_label.text = name
		_fuel_icon.visible = true
	else:
		_fuel_label.text = ""
		_fuel_icon.visible = false

	# Output slot
	if _furnace_data.output_item_id > 0 and _furnace_data.output_count > 0:
		var name := GDCraftingManager.get_item_display_name(_furnace_data.output_item_id)
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

	var db := ItemDatabase

	# Output slot click - take items
	if _output_slot.get_global_rect().has_point(get_global_mouse_position()):
		if _furnace_data.output_item_id > 0 and _furnace_data.output_count > 0:
			var taken := player.inventory.add_item(_furnace_data.output_item_id, _furnace_data.output_count)
			if taken > 0:
				_furnace_data.output_count -= taken
				if _furnace_data.output_count <= 0:
					_furnace_data.output_item_id = 0
				player.inventory_changed.emit()
			else:
				_furnace_data.output_count = 0
				_furnace_data.output_item_id = 0
				player.inventory_changed.emit()
		return

	# Input slot click - insert items
	if _input_slot.get_global_rect().has_point(get_global_mouse_position()):
		var held_id := _get_held_item_id()
		if held_id > 0 and _is_valid_input(held_id):
			if _furnace_data.input_item_id == 0 or _furnace_data.input_item_id == held_id:
				var idx := player.inventory.find_item(held_id)
				if idx >= 0:
					player.inventory.remove_from_slot(idx, 1)
					_furnace_data.input_item_id = held_id
					_furnace_data.input_count += 1
					player.inventory_changed.emit()
		return

	# Fuel slot click - insert fuel
	if _fuel_slot.get_global_rect().has_point(get_global_mouse_position()):
		var held_id := _get_held_item_id()
		if held_id > 0 and _is_fuel(held_id):
			if _furnace_data.fuel_item_id == 0 or _furnace_data.fuel_item_id == held_id:
				var idx := player.inventory.find_item(held_id)
				if idx >= 0:
					player.inventory.remove_from_slot(idx, 1)
					_furnace_data.fuel_item_id = held_id
					player.inventory_changed.emit()
		return


func _get_held_item_id() -> int:
	if player and player.has_method("get_equipped_item_id"):
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
