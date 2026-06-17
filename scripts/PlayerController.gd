# PlayerController — 3D first-person player with server-authoritative interaction.
# All connector and mechanism lookups use dimension + Vector3i (3D cell coordinates).
class_name PlayerController
extends CharacterBody3D

signal connector_used(connector_id: int, from_dimension: StringName, to_dimension: StringName)
signal mechanism_activated(mechanism_id: StringName, dimension: StringName)
signal hotbar_changed(index: int)
signal inventory_changed

const REACH := 6.0
const MOUSE_SENSITIVITY := 0.0025
const GRAVITY_STRENGTH := 22.0
const JUMP_VELOCITY := 7.0
const CLIMB_SPEED := 3.0
const OVERWORLD: StringName = &"overworld"

@export var move_speed := 5.2
@export var sprint_multiplier := 1.45
@export var inventory_width := 9
@export var inventory_height := 4
@export var connector_cooldown := 0.25
@export var use_planet_gravity := true
@export var planet_gravity_radius := 600.0
@export var world_path: NodePath = ^"../ChunkRendererBridge"
@export var command_server_path: NodePath = ^"../GameCommandServer"
@export var connector_manager_path: NodePath = ^"../ConnectorManager"
@export var mechanism_manager_path: NodePath = ^"../MechanismManager"
@export var workbench_manager_path: NodePath = ^"../WorkbenchManager"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var hotbar_ui_path: NodePath = ^"../UI/HotbarUI"
@export var inventory_ui_path: NodePath = ^"../UI/InventoryUI"
@export var crafting_ui_path: NodePath = ^"../UI/CraftingUI"
@export var furnace_ui_path: NodePath = ^"../UI/FurnaceUI"
@export var wiki_ui_path: NodePath = ^"../UI/WikiUI"
@export var connector_prompt_path: NodePath = ^"../UI/ConnectorPrompt"
@export var connector_prompt_label_path: NodePath = ^"../UI/ConnectorPrompt/Label"
@export var target_label_path: NodePath = ^"../UI/TargetLabel"
@export var head_path: NodePath = ^"Head"
@export var camera_path: NodePath = ^"Head/Camera3D"
@export var selection_path: NodePath = ^"../SelectionBox"
@export var debug_interactions := true
@export var debug_interval := 0.7
@export var give_debug_starting_items := true

var inventory: GDPlayerInventory
var equipment: GDPlayerEquipment
var selected_hotbar := 0

@onready var world: ChunkRendererBridge = get_node_or_null(world_path) as ChunkRendererBridge
@onready var command_server: GameCommandServer = get_node_or_null(command_server_path) as GameCommandServer
@onready var connector_manager: ConnectorManager = get_node_or_null(connector_manager_path) as ConnectorManager
@onready var mechanism_manager: MechanismManager = get_node_or_null(mechanism_manager_path) as MechanismManager
@onready var workbench_manager: WorkbenchManager = get_node_or_null(workbench_manager_path) as WorkbenchManager
@onready var furnace_manager: FurnaceManager = get_node_or_null(furnace_manager_path) as FurnaceManager
@onready var hotbar_ui: HotbarUI = get_node_or_null(hotbar_ui_path) as HotbarUI
@onready var inventory_ui: InventoryUI = get_node_or_null(inventory_ui_path) as InventoryUI
@onready var crafting_ui: CraftingUI = get_node_or_null(crafting_ui_path) as CraftingUI
@onready var furnace_ui: FurnaceUI = get_node_or_null(furnace_ui_path) as FurnaceUI
@onready var wiki_ui: WikiUI = get_node_or_null(wiki_ui_path) as WikiUI
@onready var connector_prompt: CanvasItem = get_node_or_null(connector_prompt_path) as CanvasItem
@onready var connector_prompt_label: Label = get_node_or_null(connector_prompt_label_path) as Label
@onready var target_label: Label = get_node_or_null(target_label_path) as Label
@onready var head: Node3D = get_node_or_null(head_path) as Node3D
@onready var camera: Camera3D = get_node_or_null(camera_path) as Camera3D
@onready var selection_box: Node3D = get_node_or_null(selection_path) as Node3D

var _input_locked := false
var _mouse_captured := true
var _pitch := deg_to_rad(-18.0)
var _cooldown_remaining := 0.0
var _last_cell := Vector3i.ZERO
var _last_debug_time := -100.0
var _target := {}
var _is_climbing := false
var _gravity_direction := Vector3.DOWN
var _planet_center := Vector3.ZERO


func _ready() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_setup_inventory()
	_connect_ui()
	_update_camera_rotation()
	_update_hotbar_display()
	_last_cell = get_current_cell()
	_update_gravity_direction()


func _physics_process(delta: float) -> void:
	if _cooldown_remaining > 0.0:
		_cooldown_remaining = maxf(_cooldown_remaining - delta, 0.0)

	_update_gravity_direction()
	_update_target()
	_handle_movement(delta)
	_try_auto_cell_events()
	_update_connector_prompt()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and _mouse_captured and not _input_locked:
		rotate_y(-event.relative.x * MOUSE_SENSITIVITY)
		_pitch = clampf(_pitch - event.relative.y * MOUSE_SENSITIVITY, deg_to_rad(-82.0), deg_to_rad(76.0))
		_update_camera_rotation()
		return

	if event is InputEventKey and event.pressed and not event.echo:
		_handle_key(event)
		return

	if event is InputEventMouseButton and event.pressed and not _input_locked:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_try_mine_target()
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			_try_place_or_interact()
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_select_hotbar((selected_hotbar - 1 + 9) % 9)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_select_hotbar((selected_hotbar + 1) % 9)


func _handle_key(event: InputEventKey) -> void:
	var key := event.keycode
	if key >= KEY_1 and key <= KEY_9:
		_select_hotbar(key - KEY_1)
		return

	if key == KEY_TAB:
		_mouse_captured = not _mouse_captured
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if _mouse_captured else Input.MOUSE_MODE_VISIBLE
		return

	if key == KEY_C:
		_toggle_crafting()
	elif key == KEY_B:
		if _close_furnace_if_open():
			return
		_toggle_wiki()
	elif key == KEY_I or key == KEY_ESCAPE:
		if key == KEY_ESCAPE and _mouse_captured:
			_mouse_captured = false
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			return
		if _close_furnace_if_open():
			return
		if wiki_ui and wiki_ui.visible:
			wiki_ui.toggle()
			_set_input_locked(false)
			return
		_toggle_inventory()
	elif key == KEY_E or key == KEY_SPACE:
		_try_place_or_interact()


func _handle_movement(delta: float) -> void:
	if _input_locked:
		velocity.x = move_toward(velocity.x, 0.0, move_speed)
		velocity.z = move_toward(velocity.z, 0.0, move_speed)
		velocity += _gravity_direction * GRAVITY_STRENGTH * delta
		move_and_slide()
		return

	_check_climbing()

	var input_vector := Vector2.ZERO
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		input_vector.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		input_vector.x += 1.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		input_vector.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		input_vector.y += 1.0

	var up := -_gravity_direction
	var basis := global_transform.basis
	var forward := -basis.z
	forward = (forward - up * forward.dot(up)).normalized()
	var right := basis.x
	right = (right - up * right.dot(up)).normalized()

	var direction := (right * input_vector.x + forward * -input_vector.y).normalized()
	var speed := move_speed * (sprint_multiplier if Input.is_key_pressed(KEY_SHIFT) else 1.0)

	# Project velocity onto the tangent plane for horizontal movement.
	var vertical_vel := _gravity_direction * velocity.dot(_gravity_direction)
	var horizontal_vel := velocity - vertical_vel
	horizontal_vel = horizontal_vel.move_toward(direction * speed, speed * 10.0 * delta)
	velocity = horizontal_vel + vertical_vel

	if _is_climbing:
		velocity.x *= 0.5
		velocity.z *= 0.5
		var climb_vel := _gravity_direction * velocity.dot(_gravity_direction)
		velocity -= climb_vel
		if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_SPACE):
			velocity -= _gravity_direction * CLIMB_SPEED
		elif Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_SHIFT):
			velocity += _gravity_direction * CLIMB_SPEED
	elif is_on_floor():
		if Input.is_key_pressed(KEY_SPACE):
			velocity -= _gravity_direction * JUMP_VELOCITY
	else:
		velocity += _gravity_direction * GRAVITY_STRENGTH * delta

	move_and_slide()
	_align_body_to_gravity(delta)


func _setup_inventory() -> void:
	inventory = GDPlayerInventory.new()
	inventory.init(inventory_width, inventory_height)
	equipment = GDPlayerEquipment.new()

	if give_debug_starting_items:
		inventory.set_slot(0, ItemDatabase.ITEM_IRON_PICKAXE, 1)
		inventory.set_slot(1, ItemDatabase.ITEM_IRON_SHOVEL, 1)
		inventory.set_slot(2, ItemDatabase.ITEM_WORKBENCH, 8)
		inventory.set_slot(3, ItemDatabase.ITEM_FURNACE, 8)
		inventory.set_slot(4, ItemDatabase.ITEM_LADDER, 8)
		equipment.equip(GDPlayerEquipment.SLOT_MAIN_HAND, ItemDatabase.ITEM_IRON_PICKAXE)

	if command_server != null:
		command_server.configure_player(inventory, equipment)
		if not command_server.inventory_synced.is_connected(_on_server_inventory_synced):
			command_server.inventory_synced.connect(_on_server_inventory_synced)


func _connect_ui() -> void:
	if hotbar_ui:
		hotbar_ui.set_player(self)
	if inventory_ui:
		inventory_ui.set_player(self)
	if crafting_ui:
		crafting_ui.set_player(self)
	if furnace_ui:
		furnace_ui.set_player(self)
		if not furnace_ui.closed.is_connected(_on_furnace_ui_closed):
			furnace_ui.closed.connect(_on_furnace_ui_closed)


func _update_camera_rotation() -> void:
	if head:
		head.rotation.x = _pitch


func _update_target() -> void:
	_target.clear()
	if camera == null or world == null:
		_set_selection_visible(false)
		return

	var from := camera.global_position
	var to := from + (-camera.global_transform.basis.z * REACH)
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.collide_with_areas = false
	query.collide_with_bodies = true
	query.exclude = [self.get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		_set_selection_visible(false)
		if target_label:
			target_label.text = ""
		return

	var normal: Vector3 = hit.get("normal", Vector3.UP)
	var hit_position: Vector3 = hit.get("position", Vector3.ZERO)
	var block_point := hit_position - normal * 0.01
	var cell := world.world_position_to_cell(block_point)
	var info := world.get_cell_info(cell)
	var data: Dictionary = info.get("data", {})
	if data.is_empty():
		_set_selection_visible(false)
		return

	var material := int(data.get("material", 0))
	if material == 0:
		_set_selection_visible(false)
		return

	_target = info
	_target["normal"] = normal
	_target["place_cell"] = world.world_position_to_cell(hit_position + normal * 0.55)
	_target["position"] = hit_position
	_set_selection_visible(true)
	if selection_box:
		selection_box.global_position = world.cell_to_world_position(cell)
	if target_label:
		var def: Dictionary = world.get_world_data().get_terrain_material_def(material) if world.get_world_data() else {}
		target_label.text = str(def.get("display_name", "Block"))


func _set_selection_visible(visible: bool) -> void:
	if selection_box:
		selection_box.visible = visible


func _try_mine_target() -> bool:
	if command_server == null or _target.is_empty():
		return false

	var data: Dictionary = _target.get("data", {})
	var material := int(data.get("material", 0))
	if material == 0:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_MINE_BLOCK,
		"dimension": _target.get("dimension", OVERWORLD),
		"chunk": _target.get("chunk", Vector3i.ZERO),
		"local": _target.get("local", Vector3i.ZERO),
		"cell": _target.get("cell", Vector3i.ZERO),
		"expected_material": material,
	})
	if not bool(result.get("ok", false)):
		_debug_interaction("mine rejected: %s" % str(result.get("reason", "unknown")))
		return false

	var chunk: Vector3i = _target.get("chunk", Vector3i.ZERO)
	var local: Vector3i = _target.get("local", Vector3i.ZERO)
	world.refresh_cell(_target.get("dimension", OVERWORLD), chunk, local)
	inventory_changed.emit()
	return true


func _try_place_or_interact() -> bool:
	if _try_open_furnace(false):
		return true
	if _try_use_connector(false):
		return true
	if _try_activate_mechanism(false):
		return true
	if _try_place_world_object():
		return true
	return false


func _try_place_world_object() -> bool:
	if _target.is_empty() or command_server == null or equipment == null:
		return false

	var held_id := equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
	var object_type := &""
	match held_id:
		ItemDatabase.ITEM_WORKBENCH:
			object_type = GameCommandServer.OBJECT_WORKBENCH
		ItemDatabase.ITEM_FURNACE:
			object_type = GameCommandServer.OBJECT_FURNACE
		ItemDatabase.ITEM_LADDER:
			object_type = GameCommandServer.OBJECT_LADDER
		_:
			return false

	var place_cell: Vector3i = _target.get("place_cell", get_current_cell())
	if global_position.distance_to(world.cell_to_world_position(place_cell)) > REACH:
		return false

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_PLACE_OBJECT,
		"object_type": object_type,
		"dimension": get_current_dimension(),
		"cell": place_cell,
		"item_id": held_id,
	})
	if not bool(result.get("ok", false)):
		_debug_interaction("place rejected: %s" % str(result.get("reason", "unknown")))
		return false

	inventory_changed.emit()
	return true


func _try_auto_cell_events() -> void:
	var cell := get_current_cell()
	if cell == _last_cell:
		return
	_last_cell = cell
	if _cooldown_remaining <= 0.0:
		if _try_use_connector(true):
			return
		_try_activate_mechanism(true)


func _try_use_connector(auto_only: bool) -> bool:
	if _input_locked or _cooldown_remaining > 0.0 or connector_manager == null:
		return false

	var dimension := get_current_dimension()
	var cell := get_current_cell()
	var connector = connector_manager.get_connector_at(dimension, cell)
	if connector == null:
		return false
	if auto_only and not connector.activates_on_enter():
		return false
	if not auto_only and not connector.requires_interaction():
		return false

	var target_dimension: StringName = connector.get_target_dimension_for(dimension, cell)
	var target_cell: Vector3i = connector.get_target_cell_for(dimension, cell)
	if target_dimension == &"":
		return false

	_cooldown_remaining = connector_cooldown
	connector_used.emit(connector.connector_id, dimension, target_dimension)
	return true


func _try_activate_mechanism(auto_only: bool) -> bool:
	if _input_locked or mechanism_manager == null:
		return false

	var dimension := get_current_dimension()
	var cell := get_current_cell()
	var mechanism = mechanism_manager.get_mechanism_at(dimension, cell)
	if mechanism == null:
		return false
	if auto_only and not mechanism.activates_on_enter():
		return false
	if not auto_only and not mechanism.requires_interaction():
		return false
	if not mechanism_manager.activate_mechanism(mechanism.mechanism_id):
		return false

	_cooldown_remaining = connector_cooldown
	mechanism_activated.emit(mechanism.mechanism_id, dimension)
	return true


func _try_open_furnace(auto_only: bool) -> bool:
	if auto_only or furnace_manager == null or furnace_ui == null:
		return false
	var cell := get_current_cell()
	var dimension := get_current_dimension()
	var candidates := [cell]
	if not _target.is_empty():
		candidates.append(_target.get("cell", cell))
		candidates.append(_target.get("place_cell", cell))
	candidates.append_array([
		cell + Vector3i.RIGHT,
		cell + Vector3i.LEFT,
		cell + Vector3i.UP,
		cell + Vector3i.DOWN,
		cell + Vector3i.FORWARD,
		cell + Vector3i.BACK,
	])

	for candidate in candidates:
		if not furnace_manager.has_furnace(dimension, candidate):
			continue
		var data = furnace_manager.get_furnace(dimension, candidate)
		furnace_ui.open(data, dimension, candidate, furnace_manager)
		_set_input_locked(true)
		_cooldown_remaining = connector_cooldown
		return true
	return false


func _toggle_inventory() -> void:
	if crafting_ui and crafting_ui.visible:
		_toggle_crafting()
	if inventory_ui:
		inventory_ui.toggle()


func _toggle_wiki() -> void:
	if crafting_ui and crafting_ui.visible:
		_toggle_crafting()
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	if wiki_ui:
		wiki_ui.toggle()
		_set_input_locked(wiki_ui.visible)


func _toggle_crafting() -> void:
	if crafting_ui == null:
		return
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	crafting_ui.set_station(_get_nearby_station())
	crafting_ui.toggle()
	_set_input_locked(crafting_ui.visible)


func _get_nearby_station() -> String:
	if workbench_manager == null:
		return ""
	var cell := get_current_cell()
	var dimension := get_current_dimension()
	for offset in [
		Vector3i.ZERO,
		Vector3i.RIGHT,
		Vector3i.LEFT,
		Vector3i.UP,
		Vector3i.DOWN,
		Vector3i.FORWARD,
		Vector3i.BACK,
	]:
		if workbench_manager.has_workbench(dimension, cell + offset):
			return "workbench"
	return ""


func _close_furnace_if_open() -> bool:
	if furnace_ui and furnace_ui.visible:
		furnace_ui.close()
		_set_input_locked(false)
		return true
	return false


func _on_furnace_ui_closed() -> void:
	_set_input_locked(false)


func _on_server_inventory_synced() -> void:
	inventory_changed.emit()


func _select_hotbar(index: int) -> void:
	selected_hotbar = clampi(index, 0, 8)
	var slot: Dictionary = inventory.get_slot(selected_hotbar) if inventory else {}
	var item_id := int(slot.get("item_id", 0))
	equipment.equip(GDPlayerEquipment.SLOT_MAIN_HAND, item_id)
	hotbar_changed.emit(selected_hotbar)
	inventory_changed.emit()
	_update_hotbar_display()


func _update_hotbar_display() -> void:
	if hotbar_ui:
		hotbar_ui.refresh()


func _update_connector_prompt() -> void:
	if connector_prompt == null or connector_prompt_label == null:
		return

	var dimension := get_current_dimension()
	var cell := get_current_cell()
	var text := ""
	if connector_manager != null:
		var connector = connector_manager.get_connector_at(dimension, cell)
		if connector != null and connector.requires_interaction():
			text = "E  %s" % String(connector.connector_type).replace("_", " ").capitalize()

	if text == "" and mechanism_manager != null:
		var mechanism = mechanism_manager.get_mechanism_at(dimension, cell)
		if mechanism != null and mechanism.requires_interaction():
			text = "E  %s" % (mechanism.action_label if mechanism.action_label != "" else mechanism.display_name)

	connector_prompt.visible = text != "" and not _input_locked
	connector_prompt_label.text = text


func get_current_dimension() -> StringName:
	return OVERWORLD


func get_current_cell() -> Vector3i:
	if world == null:
		return Vector3i.ZERO
	return world.world_position_to_cell(global_position)


func get_equipped_item_id() -> int:
	if equipment == null:
		return 0
	return equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)


func get_command_server() -> GameCommandServer:
	return command_server


func get_selected_hotbar() -> int:
	return selected_hotbar


func _set_input_locked(is_locked: bool) -> void:
	_input_locked = is_locked
	if is_locked:
		velocity = Vector3.ZERO
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_mouse_captured = false
	elif not _mouse_captured:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _check_climbing() -> void:
	if world == null:
		_is_climbing = false
		return

	var cell := get_current_cell()
	var dimension := get_current_dimension()

	# Check the player's current cell and the cell above for climbable terrain.
	var candidates := [cell, cell + Vector3i.UP]
	for candidate in candidates:
		var info := world.get_cell_info(candidate, dimension)
		var data: Dictionary = info.get("data", {})
		if data.get("is_climbable", false):
			_is_climbing = true
			return

	_is_climbing = false


# --- Planet gravity system ---

func _update_gravity_direction() -> void:
	if not use_planet_gravity or world == null:
		_gravity_direction = Vector3.DOWN
		return

	var world_data_node := world.get_world_data()
	if world_data_node == null:
		_gravity_direction = Vector3.DOWN
		return

	# Get planet center from the worldgen config.
	# The planet center is stored in the frozen config snapshot.
	var config: Resource = world_data_node.worldgen_config
	if config == null:
		_gravity_direction = Vector3.DOWN
		return

	# Read planet center from the config's snapshot.
	# For now, compute from the planet config registered in BuiltinTerrainContent.
	# The center is at (0, -512, 0) for the default planet.
	_planet_center = _get_planet_center_from_config(config)
	var to_center := _planet_center - global_position
	var dist := to_center.length()

	if dist < planet_gravity_radius and dist > 0.01:
		_gravity_direction = to_center.normalized()
	else:
		_gravity_direction = Vector3.DOWN


func _get_planet_center_from_config(_config: Resource) -> Vector3:
	# TODO: Expose planet_center through GDWorldGenConfig API.
	# For now, use the default center matching BuiltinTerrainContent.
	return Vector3(0.0, -512.0, 0.0)


func _align_body_to_gravity(delta: float) -> void:
	if not use_planet_gravity:
		return

	var target_up := -_gravity_direction
	var current_up := global_transform.basis.y

	# Skip if already well-aligned.
	var dot := current_up.dot(target_up)
	if dot > 0.999:
		return

	# Smooth rotation toward the target up direction.
	var rotation_speed := 8.0
	var max_angle := rotation_speed * delta

	var axis := current_up.cross(target_up)
	if axis.length_squared() < 0.0001:
		return
	axis = axis.normalized()
	var angle := acos(clampf(dot, -1.0, 1.0))
	var actual_angle := minf(angle, max_angle)

	rotate(axis, actual_angle)

	# Re-orthonormalize the basis to prevent drift.
	var new_basis := global_transform.basis.orthonormalized()
	global_transform.basis = new_basis


func _debug_interaction(message: String) -> void:
	if not debug_interactions:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - _last_debug_time < debug_interval:
		return
	_last_debug_time = now
	print("PlayerController3D: ", message)
