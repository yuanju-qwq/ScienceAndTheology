class_name PlayerController
extends CharacterBody2D

signal connector_used(connector_id: int, from_layer: StringName, to_layer: StringName)
signal mechanism_activated(mechanism_id: StringName, layer_id: StringName)
signal hotbar_changed(index: int)
signal inventory_changed

@export var move_speed := 96.0
@export var connector_cooldown := 0.25
@export var inventory_width := 9
@export var inventory_height := 4
@export var layer_controller_path: NodePath = ^".."
@export var connector_manager_path: NodePath = ^"../ConnectorManager"
@export var mechanism_manager_path: NodePath = ^"../MechanismManager"
@export var connector_prompt_path: NodePath = ^"../UI/ConnectorPrompt"
@export var connector_prompt_label_path: NodePath = ^"../UI/ConnectorPrompt/Label"
@export var transition_overlay_path: NodePath = ^"../UI/TransitionOverlay"
@export var transition_label_path: NodePath = ^"../UI/TransitionOverlay/Label"
@export var exploration_tracker_path: NodePath = ^"../ExplorationTracker"
@export var hotbar_ui_path: NodePath = ^"../UI/HotbarUI"
@export var inventory_ui_path: NodePath = ^"../UI/InventoryUI"
@export var crosshair_path: NodePath = ^"../UI/Crosshair"
@export var mining_path: NodePath = ^"../PlayerMining"
@export var crafting_ui_path: NodePath = ^"../UI/CraftingUI"
@export var workbench_manager_path: NodePath = ^"../WorkbenchManager"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var ladder_manager_path: NodePath = ^"../LadderManager"
@export var furnace_ui_path: NodePath = ^"../UI/FurnaceUI"
@export var wiki_ui_path: NodePath = ^"../UI/WikiUI"

@export var transition_fade_in_duration := 0.18
@export var transition_hold_duration := 0.08
@export var transition_fade_out_duration := 0.22

var inventory: GDPlayerInventory
var equipment: GDPlayerEquipment
var selected_hotbar: int = 0

@onready var layer_controller = get_node_or_null(layer_controller_path)
@onready var connector_manager = get_node_or_null(connector_manager_path)
@onready var mechanism_manager = get_node_or_null(mechanism_manager_path)
@onready var connector_prompt: CanvasItem = get_node_or_null(connector_prompt_path) as CanvasItem
@onready var connector_prompt_label: Label = get_node_or_null(connector_prompt_label_path) as Label
@onready var transition_overlay: CanvasItem = get_node_or_null(transition_overlay_path) as CanvasItem
@onready var transition_label: Label = get_node_or_null(transition_label_path) as Label
@onready var exploration_tracker = get_node_or_null(exploration_tracker_path)
@onready var hotbar_ui = get_node_or_null(hotbar_ui_path)
@onready var inventory_ui = get_node_or_null(inventory_ui_path)
@onready var mining = get_node_or_null(mining_path)
@onready var crosshair = get_node_or_null(crosshair_path)
@onready var crafting_ui = get_node_or_null(crafting_ui_path)
@onready var workbench_manager = get_node_or_null(workbench_manager_path)
@onready var furnace_manager = get_node_or_null(furnace_manager_path)
@onready var ladder_manager = get_node_or_null(ladder_manager_path)
@onready var furnace_ui = get_node_or_null(furnace_ui_path)
@onready var wiki_ui = get_node_or_null(wiki_ui_path)

var _cooldown_remaining := 0.0
var _last_layer: StringName = &""
var _last_cell := Vector2i.ZERO
var _input_locked := false


func _ready() -> void:
	inventory = GDPlayerInventory.new()
	inventory.init(inventory_width, inventory_height)
	equipment = GDPlayerEquipment.new()

	_prepare_transition_overlay()
	_last_layer = _get_current_layer()
	_last_cell = _get_current_cell()
	_mark_current_cell_visited()
	_update_connector_prompt()

	if hotbar_ui and hotbar_ui.has_method(&"set_player"):
		hotbar_ui.set_player(self)
	if inventory_ui and inventory_ui.has_method(&"set_player"):
		inventory_ui.set_player(self)
	if mining and mining.has_method(&"set_player"):
		mining.set_player(self)
		if mining.block_mined.is_connected(_on_block_mined):
			mining.block_mined.disconnect(_on_block_mined)
		mining.block_mined.connect(_on_block_mined)
	if crafting_ui and crafting_ui.has_method(&"set_player"):
		crafting_ui.set_player(self)
	if furnace_ui and furnace_ui.has_method(&"set_player"):
		furnace_ui.set_player(self)
		if furnace_ui.closed.is_connected(_on_furnace_ui_closed):
			furnace_ui.closed.disconnect(_on_furnace_ui_closed)
		furnace_ui.closed.connect(_on_furnace_ui_closed)

	_update_hotbar_display()


func _physics_process(delta: float) -> void:
	if _cooldown_remaining > 0.0:
		_cooldown_remaining = maxf(_cooldown_remaining - delta, 0.0)

	if _input_locked:
		velocity = Vector2.ZERO
		move_and_slide()
		_update_connector_prompt()
		return

	_handle_movement()
	_try_auto_connector()
	_update_connector_prompt()
	_update_hotbar_selection()


func _handle_movement() -> void:
	var input_vector := Vector2.ZERO

	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		input_vector.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		input_vector.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		input_vector.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		input_vector.y += 1.0

	velocity = input_vector.normalized() * move_speed
	move_and_slide()


func _unhandled_input(event: InputEvent) -> void:
	if _input_locked:
		return

	if _is_interact_event(event):
		if _try_use_connector(false) or _try_activate_mechanism(false) or _try_open_furnace(false):
			get_viewport().set_input_as_handled()

	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			selected_hotbar = (selected_hotbar - 1 + 9) % 9
			hotbar_changed.emit(selected_hotbar)
			_update_hotbar_display()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			selected_hotbar = (selected_hotbar + 1) % 9
			hotbar_changed.emit(selected_hotbar)
			_update_hotbar_display()
		elif event.button_index == MOUSE_BUTTON_RIGHT and event.pressed:
			if _try_place_workbench():
				get_viewport().set_input_as_handled()
			elif _try_place_furnace():
				get_viewport().set_input_as_handled()
			elif _try_place_ladder():
				get_viewport().set_input_as_handled()


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		var key := event.keycode
		if key >= KEY_1 and key <= KEY_9:
			selected_hotbar = key - KEY_1
			hotbar_changed.emit(selected_hotbar)
			_update_hotbar_display()
		elif key == KEY_C:
			_toggle_crafting()
		elif key == KEY_B:
			if _close_furnace_if_open():
				return
			_toggle_wiki()
		elif key == KEY_I or key == KEY_ESCAPE:
			if _close_furnace_if_open():
				return
			if wiki_ui and wiki_ui.visible:
				wiki_ui.toggle()
				return
			_toggle_inventory()

func _close_furnace_if_open() -> bool:
	if furnace_ui and furnace_ui.visible:
		furnace_ui.close()
		_set_input_locked(false)
		return true
	return false


func _on_furnace_ui_closed() -> void:
	_set_input_locked(false)


func _toggle_inventory() -> void:
	if crafting_ui and crafting_ui.visible:
		_toggle_crafting()
	if inventory_ui and inventory_ui.has_method(&"toggle"):
		inventory_ui.toggle()

func _toggle_wiki() -> void:
	if crafting_ui and crafting_ui.visible:
		_toggle_crafting()
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	if wiki_ui and wiki_ui.has_method(&"toggle"):
		wiki_ui.toggle()


func _try_auto_connector() -> void:
	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var entered_new_cell := layer_id != _last_layer or cell_position != _last_cell

	if not entered_new_cell:
		return

	_last_layer = layer_id
	_last_cell = cell_position
	_mark_current_cell_visited()

	if _cooldown_remaining <= 0.0:
		if _try_use_connector(true):
			return
		_try_activate_mechanism(true)


func _try_use_connector(auto_only: bool) -> bool:
	if _input_locked or _cooldown_remaining > 0.0 or connector_manager == null:
		return false

	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var connector = connector_manager.get_connector_at(layer_id, cell_position)
	if connector == null:
		return false

	if auto_only and not connector.activates_on_enter():
		return false
	if not auto_only and not connector.requires_interaction():
		return false

	_start_connector_transition(connector, layer_id, cell_position)
	return true


func _try_activate_mechanism(auto_only: bool) -> bool:
	if _input_locked or mechanism_manager == null:
		return false

	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var mechanism = mechanism_manager.get_mechanism_at(layer_id, cell_position)
	if mechanism == null:
		return false

	if auto_only and not mechanism.activates_on_enter():
		return false
	if not auto_only and not mechanism.requires_interaction():
		return false

	if not mechanism_manager.activate_mechanism(mechanism.mechanism_id):
		return false

	_cooldown_remaining = connector_cooldown
	mechanism_activated.emit(mechanism.mechanism_id, layer_id)
	_update_connector_prompt()
	return true


func _start_connector_transition(connector, layer_id: StringName, cell_position: Vector2i) -> void:
	_set_input_locked(true)
	_run_connector_transition(connector, layer_id, cell_position)


func _run_connector_transition(connector, layer_id: StringName, cell_position: Vector2i) -> void:
	var target_layer: StringName = connector.get_target_layer_for(layer_id, cell_position)
	if target_layer == &"":
		_set_input_locked(false)
		return

	var target_cell: Vector2i = connector.get_target_cell_for(layer_id, cell_position)
	await _play_transition_fade_in(connector)

	if layer_controller != null:
		layer_controller.change_layer(target_layer)

	var target_tile_layer := _get_current_tile_layer()
	if target_tile_layer != null:
		global_position = target_tile_layer.to_global(target_tile_layer.map_to_local(target_cell))

	_last_layer = target_layer
	_last_cell = target_cell
	_mark_current_cell_visited()
	_cooldown_remaining = connector_cooldown
	connector_used.emit(connector.connector_id, layer_id, target_layer)

	if transition_hold_duration > 0.0:
		await get_tree().create_timer(transition_hold_duration).timeout

	await _play_transition_fade_out()
	_set_input_locked(false)


func _on_block_mined(cell: Vector2i, layer: StringName, item_id: int, count: int) -> void:
	var overflow := inventory.add_item(item_id, count)
	if overflow > 0:
		pass
	inventory_changed.emit()


func _try_place_workbench() -> bool:
	if inventory == null or equipment == null or crosshair == null or workbench_manager == null:
		return false
	if not crosshair.has_target:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_WORKBENCH:
		return false

	var tile_layer := _get_current_tile_layer()
	if tile_layer == null:
		return false
	var cell := crosshair.target_cell
	var layer := crosshair.target_layer
	var world_pos := tile_layer.to_global(tile_layer.map_to_local(cell))
	if global_position.distance_to(world_pos) > 4.0:
		return false

	var drop_idx := inventory.find_item(ItemDatabase.ITEM_WORKBENCH)
	if drop_idx < 0:
		return false

	var ok := inventory.remove_from_slot(drop_idx, 1)
	if not ok:
		return false

	var placed := workbench_manager.place_workbench(layer, cell)
	if placed:
		inventory_changed.emit()
	return placed


func _try_place_furnace() -> bool:
	if inventory == null or equipment == null or crosshair == null or furnace_manager == null:
		return false
	if not crosshair.has_target:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_FURNACE:
		return false

	var tile_layer := _get_current_tile_layer()
	if tile_layer == null:
		return false
	var cell := crosshair.target_cell
	var layer := crosshair.target_layer
	var world_pos := tile_layer.to_global(tile_layer.map_to_local(cell))
	if global_position.distance_to(world_pos) > 4.0:
		return false

	var drop_idx := inventory.find_item(ItemDatabase.ITEM_FURNACE)
	if drop_idx < 0:
		return false

	var ok := inventory.remove_from_slot(drop_idx, 1)
	if not ok:
		return false

	var placed := furnace_manager.place_furnace(layer, cell)
	if placed:
		inventory_changed.emit()
	return placed


func _try_place_ladder() -> bool:
	if inventory == null or equipment == null or crosshair == null or ladder_manager == null:
		return false
	if not crosshair.has_target:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	if held_id != ItemDatabase.ITEM_LADDER:
		return false

	var tile_layer := _get_current_tile_layer()
	if tile_layer == null:
		return false
	var cell := crosshair.target_cell
	var layer := crosshair.target_layer
	var world_pos := tile_layer.to_global(tile_layer.map_to_local(cell))
	if global_position.distance_to(world_pos) > 4.0:
		return false

	var drop_idx := inventory.find_item(ItemDatabase.ITEM_LADDER)
	if drop_idx < 0:
		return false

	var ok := inventory.remove_from_slot(drop_idx, 1)
	if not ok:
		return false

	var placed := ladder_manager.place_ladder(layer, cell)
	if placed:
		inventory_changed.emit()
	return placed


func _try_open_furnace(auto_only: bool) -> bool:
	if _cooldown_remaining > 0.0 or furnace_manager == null or furnace_ui == null:
		return false

	if auto_only:
		return false

	var layer_id := _get_current_layer()
	var cell_position := _get_current_cell()
	var neighbors := [Vector2i(0, 0), Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]

	for offset in neighbors:
		var check_cell := cell_position + offset
		if not furnace_manager.has_furnace(layer_id, check_cell):
			continue
		var data = furnace_manager.get_furnace(layer_id, check_cell)
		furnace_ui.open(data, layer_id, check_cell, furnace_manager)
		_set_input_locked(true)
		_cooldown_remaining = connector_cooldown
		return true

	return false


func get_current_layer() -> StringName:
	return _get_current_layer()


func get_current_cell() -> Vector2i:
	return _get_current_cell()


func get_equipped_item_id() -> int:
	if equipment == null:
		return 0
	return equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)

func get_selected_hotbar() -> int:
	return selected_hotbar

func _toggle_crafting() -> void:
	if crafting_ui == null:
		return
	if not crafting_ui.has_method(&"toggle"):
		return

	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()

	if crafting_ui.has_method(&"set_station"):
		var station := _get_nearby_station()
		crafting_ui.set_station(station)
	crafting_ui.toggle()
	_set_input_locked(crafting_ui.visible)


func _get_nearby_station() -> String:
	if workbench_manager == null:
		return ""
	if not workbench_manager.has_method(&"has_workbench"):
		return ""
	var cell := _get_current_cell()
	var layer := _get_current_layer()
	var neighbors := [Vector2i(0, 0), Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]
	for offset in neighbors:
		if workbench_manager.has_workbench(layer, cell + offset):
			return "workbench"
	return ""


func _get_current_layer() -> StringName:
	if layer_controller == null:
		return &"surface"
	return layer_controller.current_layer


func _get_current_cell() -> Vector2i:
	var tile_layer := _get_current_tile_layer()
	if tile_layer == null:
		return Vector2i.ZERO
	return tile_layer.local_to_map(tile_layer.to_local(global_position))


func _get_current_tile_layer() -> TileMapLayer:
	if layer_controller == null or not layer_controller.has_method("get_current_tile_layer"):
		return null
	return layer_controller.get_current_tile_layer()


func _update_connector_prompt() -> void:
	if connector_prompt == null or connector_prompt_label == null:
		return

	var connector = null
	if connector_manager != null:
		connector = connector_manager.get_connector_at(_get_current_layer(), _get_current_cell())

	var can_interact: bool = connector != null \
			and connector.requires_interaction() \
			and _cooldown_remaining <= 0.0 \
			and not _input_locked

	if can_interact:
		connector_prompt.visible = true
		connector_prompt_label.text = "E  %s" % _format_connector_type(connector.connector_type)
		return

	var mechanism = null
	if mechanism_manager != null:
		mechanism = mechanism_manager.get_mechanism_at(_get_current_layer(), _get_current_cell())

	var can_activate_mechanism: bool = mechanism != null \
			and mechanism.requires_interaction() \
			and not _input_locked
	connector_prompt.visible = can_activate_mechanism

	if can_activate_mechanism:
		connector_prompt_label.text = "E  %s" % _format_mechanism_action(mechanism)


func _format_connector_type(connector_type: StringName) -> String:
	match connector_type:
		&"cave_entrance":
			return "Cave Entrance"
		&"rift":
			return "Rift"
		&"ruin_gate":
			return "Ruin Gate"
		&"ladder":
			return "Ladder"
		_:
			return String(connector_type).replace("_", " ").capitalize()


func _format_mechanism_action(mechanism) -> String:
	if mechanism.action_label != "":
		return mechanism.action_label
	return mechanism.display_name


func _prepare_transition_overlay() -> void:
	if transition_overlay == null:
		return
	transition_overlay.visible = false
	transition_overlay.modulate.a = 0.0


func _play_transition_fade_in(connector) -> void:
	if transition_overlay == null:
		return
	transition_overlay.visible = true
	transition_overlay.modulate.a = 0.0
	if transition_label != null:
		transition_label.text = _get_transition_text(connector)
	var tween := create_tween()
	tween.tween_property(transition_overlay, "modulate:a", 1.0, transition_fade_in_duration)
	await tween.finished


func _play_transition_fade_out() -> void:
	if transition_overlay == null:
		return
	var tween := create_tween()
	tween.tween_property(transition_overlay, "modulate:a", 0.0, transition_fade_out_duration)
	await tween.finished
	transition_overlay.visible = false


func _get_transition_text(connector) -> String:
	match connector.connector_type:
		&"cave_entrance":
			return "Entering"
		&"rift":
			return "Falling"
		&"ruin_gate":
			return "Passing"
		&"ladder":
			return "Climbing"
		_:
			return "Moving"


func _mark_current_cell_visited() -> void:
	if exploration_tracker == null or not exploration_tracker.has_method("mark_visited"):
		return
	exploration_tracker.mark_visited(_get_current_layer(), _get_current_cell())


func _set_input_locked(is_locked: bool) -> void:
	_input_locked = is_locked
	if layer_controller != null and layer_controller.has_method("set_input_locked"):
		layer_controller.set_input_locked(is_locked)
	if _input_locked:
		velocity = Vector2.ZERO
	_update_connector_prompt()


func _is_interact_event(event: InputEvent) -> bool:
	return (
		event is InputEventKey
		and event.pressed
		and not event.echo
		and (event.physical_keycode == KEY_E or event.physical_keycode == KEY_SPACE)
	)


func _update_hotbar_selection() -> void:
	pass


func _update_hotbar_display() -> void:
	if hotbar_ui and hotbar_ui.has_method(&"refresh"):
		hotbar_ui.refresh()
