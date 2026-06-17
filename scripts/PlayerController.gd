# PlayerController — 3D first-person player with server-authoritative interaction.
# Delegates interaction logic to PlayerInteraction, UI wiring to PlayerUIConnector,
# and gravity/alignment math to GDPlayerHelper (C++).
class_name PlayerController
extends CharacterBody3D

signal connector_used(connector_id: int, from_dimension: StringName, to_dimension: StringName)
signal mechanism_activated(mechanism_id: StringName, dimension: StringName)
signal hotbar_changed(index: int)
signal inventory_changed

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

# Sub-modules for separated concerns.
var _interaction: PlayerInteraction
var _ui_connector: PlayerUIConnector

var _input_locked := false
var _mouse_captured := true
var _pitch := deg_to_rad(-18.0)
var _last_cell := Vector3i.ZERO
var _last_debug_time := -100.0
var _target := {}
var _is_climbing := false
var _gravity_direction := Vector3.DOWN
var _planet_center := Vector3.ZERO


func _ready() -> void:
	_interaction = PlayerInteraction.new()
	_interaction.name = "PlayerInteraction"
	add_child(_interaction)
	_interaction.setup(self)

	_ui_connector = PlayerUIConnector.new()
	_ui_connector.name = "PlayerUIConnector"
	add_child(_ui_connector)
	_ui_connector.setup(self)

	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_setup_inventory()
	_ui_connector.connect_ui()
	_update_camera_rotation()
	_select_hotbar(selected_hotbar)
	_last_cell = get_current_cell()
	_update_gravity_direction()


func _physics_process(delta: float) -> void:
	_interaction.process_cooldown(delta)
	_update_gravity_direction()
	_update_target()
	_handle_movement(delta)
	_interaction.try_auto_cell_events()
	_ui_connector.update_connector_prompt()


# --- Input handling ---

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
			_interaction.try_mine_target(_target)
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			_interaction.try_place_or_interact(_target)
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
		_ui_connector.toggle_crafting()
	elif key == KEY_B:
		if _ui_connector.close_furnace_if_open():
			return
		_ui_connector.toggle_wiki()
	elif key == KEY_I or key == KEY_ESCAPE:
		if key == KEY_ESCAPE and _mouse_captured:
			_mouse_captured = false
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			return
		if _ui_connector.close_furnace_if_open():
			return
		if wiki_ui and wiki_ui.visible:
			wiki_ui.toggle()
			_set_input_locked(false)
			return
		_ui_connector.toggle_inventory()
	elif key == KEY_E or key == KEY_SPACE:
		_interaction.try_place_or_interact(_target)


# --- Movement and physics ---

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


func _check_climbing() -> void:
	if world == null:
		_is_climbing = false
		return

	var cell := get_current_cell()
	var dimension := get_current_dimension()

	var candidates := [cell, cell + Vector3i.UP]
	for candidate in candidates:
		var info := world.get_cell_info(candidate, dimension)
		var data: Dictionary = info.get("data", {})
		if data.get("is_climbable", false):
			_is_climbing = true
			return

	_is_climbing = false


# --- Gravity system (delegates math to C++ GDPlayerHelper) ---

func _update_gravity_direction() -> void:
	if not use_planet_gravity or world == null:
		_gravity_direction = Vector3.DOWN
		return

	var world_data_node := world.get_world_data()
	if world_data_node == null:
		_gravity_direction = Vector3.DOWN
		return

	_planet_center = _get_planet_center_from_config(world_data_node.worldgen_config)
	_gravity_direction = GDPlayerHelper.compute_gravity_direction(
		global_position, _planet_center, planet_gravity_radius, use_planet_gravity)


func _get_planet_center_from_config(_config: Resource) -> Vector3:
	# TODO: Expose planet_center through GDWorldGenConfig API.
	return Vector3(0.0, -512.0, 0.0)


func _align_body_to_gravity(delta: float) -> void:
	if not use_planet_gravity:
		return

	var target_up := -_gravity_direction
	var new_basis := GDPlayerHelper.align_body_to_gravity(
		global_transform.basis, target_up, 8.0, delta)
	global_transform.basis = new_basis


# --- Inventory and equipment ---

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


func _on_server_inventory_synced() -> void:
	inventory_changed.emit()


# --- Camera and targeting ---

func _update_camera_rotation() -> void:
	if head:
		head.rotation.x = _pitch


func _update_target() -> void:
	_target.clear()
	if camera == null or world == null:
		_set_selection_visible(false)
		return

	var from := camera.global_position
	var to := from + (-camera.global_transform.basis.z * 6.0)
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


func _set_selection_visible(vis: bool) -> void:
	if selection_box:
		selection_box.visible = vis


# --- Hotbar ---

func _select_hotbar(index: int) -> void:
	selected_hotbar = clampi(index, 0, 8)
	var slot: Dictionary = inventory.get_slot(selected_hotbar) if inventory else {}
	var item_id := int(slot.get("item_id", 0))
	equipment.equip(GDPlayerEquipment.SLOT_MAIN_HAND, item_id)
	hotbar_changed.emit(selected_hotbar)
	inventory_changed.emit()
	_ui_connector.update_hotbar_display()


# --- Public queries ---

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


func _debug_interaction(message: String) -> void:
	if not debug_interactions:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - _last_debug_time < debug_interval:
		return
	_last_debug_time = now
	print("PlayerController3D: ", message)
