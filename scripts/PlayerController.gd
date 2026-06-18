# PlayerController — 3D first-person player with server-authoritative interaction.
# Delegates interaction logic to PlayerInteraction, UI wiring to PlayerUIConnector,
# and gravity/alignment math to GDPlayerHelper (C++).
class_name PlayerController
extends CharacterBody3D

@warning_ignore("unused_signal")
signal connector_used(connector_id: int, from_dimension: StringName, to_dimension: StringName)
@warning_ignore("unused_signal")
signal mechanism_activated(mechanism_id: StringName, dimension: StringName)
signal hotbar_changed(index: int)
signal inventory_changed

const MOUSE_SENSITIVITY := 0.0025
const GRAVITY_STRENGTH := 22.0
const JUMP_VELOCITY := 7.0
const CLIMB_SPEED := 3.0
const OVERWORLD: StringName = &"overworld"
const MAIN_MENU_SCENE_PATH := "res://MainMenu.tscn"

# Creative fly mode — toggled via /fly console command.
var fly_mode := false:
	set(value):
		if fly_mode == value:
			return
		fly_mode = value
		if fly_mode:
			_is_climbing = false

# Fly movement speed (adjustable via /speed console command).
var fly_speed := 20.0

@export var move_speed := 5.2
@export var sprint_multiplier := 1.45
@export var inventory_width := 9
@export var inventory_height := 4
@export var connector_cooldown := 0.25
@export var use_planet_gravity := true
@export var planet_gravity_radius := 2048.0
@export var universe_manager_path: NodePath = ^"../UniverseManager"
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
@export var console_ui_path: NodePath = ^"../UI/ConsoleUI"
@export var connector_prompt_path: NodePath = ^"../UI/ConnectorPrompt"
@export var connector_prompt_label_path: NodePath = ^"../UI/ConnectorPrompt/Label"
@export var probe_panel_path: NodePath = ^"../UI/ProbePanel"
@export var quest_ui_path: NodePath = ^"../UI/QuestBookUI"
@export var quest_system_path: NodePath = ^"../GDQuestSystem"
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
@onready var console_ui: ConsoleUI = get_node_or_null(console_ui_path) as ConsoleUI
@onready var connector_prompt: CanvasItem = get_node_or_null(connector_prompt_path) as CanvasItem
@onready var connector_prompt_label: Label = get_node_or_null(connector_prompt_label_path) as Label
@onready var probe_panel: ProbePanel = get_node_or_null(probe_panel_path) as ProbePanel
@onready var quest_ui: QuestBookUI = get_node_or_null(quest_ui_path) as QuestBookUI
@onready var quest_system: Node = get_node_or_null(quest_system_path)
@onready var head: Node3D = get_node_or_null(head_path) as Node3D
@onready var camera: Camera3D = get_node_or_null(camera_path) as Camera3D
@onready var selection_box: Node3D = get_node_or_null(selection_path) as Node3D

# Exit menu (pause menu) — created programmatically and added to the UI layer.
var exit_menu: ExitMenu = null

# Sub-modules for separated concerns.
var _interaction: PlayerInteraction
var _ui_connector: PlayerUIConnector

# UniverseManager reference for multi-planet gravity.
var _universe_manager: UniverseManager = null

var _input_locked := false
var _mouse_captured := true
var _pitch := deg_to_rad(-18.0)
var _last_cell := Vector3i.ZERO
var _last_debug_time := -100.0
var _target := {}
var _is_climbing := false
var _gravity_direction := Vector3.DOWN
var _planet_center := Vector3.ZERO

# Per-planet gravity multiplier (1.0 = Earth-like, 0.38 = Mars-like).
# Updated each frame by _update_gravity_direction().
var _gravity_multiplier := 1.0

# --- Atmosphere hazard system ---

# Default damage rates used when no active planet is available.
# These match PlanetDescriptor defaults and serve as fallback values.
const _DEFAULT_TOXIC_DAMAGE_PER_SEC := 5.0
const _DEFAULT_CORROSIVE_DAMAGE_PER_SEC := 8.0
const _DEFAULT_VACUUM_DAMAGE_PER_SEC := 3.0

# Cached reference to the active planet descriptor.
# Updated each frame by _update_gravity_direction().
var _active_planet: PlanetDescriptor = null

# Current atmosphere type at the player's position.
# Updated each frame alongside gravity.
var _atmosphere_type: int = PlanetDescriptor.AtmosphereType.BREATHABLE

# Accumulated damage timer for atmosphere hazard ticks.
var _atmo_damage_timer := 0.0


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
	_connect_universe_manager()
	_setup_inventory()
	_ui_connector.connect_ui()
	_connect_console()
	_connect_quest_system()
	_setup_exit_menu()
	_update_camera_rotation()
	_select_hotbar(selected_hotbar)
	_last_cell = get_current_cell()
	_update_gravity_direction()


func _physics_process(delta: float) -> void:
	_interaction.process_cooldown(delta)
	_update_gravity_direction()
	_update_atmosphere_hazard(delta)
	_update_target()
	_handle_movement(delta)
	_interaction.try_auto_cell_events()
	_ui_connector.update_connector_prompt()
	_update_status_label()


# --- Input handling ---

func _unhandled_input(event: InputEvent) -> void:
	if console_ui and console_ui.is_open():
		return

	# ESC toggles the exit menu — handled even when input is locked (menu open).
	if event is InputEventKey:
		var key_event: InputEventKey = event
		if key_event.pressed and not key_event.echo and key_event.keycode == KEY_ESCAPE:
			if exit_menu and exit_menu.is_open():
				_close_exit_menu()
				return
			if not _input_locked:
				_open_exit_menu()
				return

	if event is InputEventMouseMotion and _mouse_captured and not _input_locked:
		var motion: InputEventMouseMotion = event
		rotate_y(-motion.relative.x * MOUSE_SENSITIVITY)
		_pitch = clampf(_pitch - motion.relative.y * MOUSE_SENSITIVITY, deg_to_rad(-82.0), deg_to_rad(76.0))
		_update_camera_rotation()
		return

	if event is InputEventKey:
		var key_event: InputEventKey = event
		if key_event.pressed and not key_event.echo and not _input_locked:
			_handle_key(key_event)
			return

	if event is InputEventMouseButton:
		var mouse_event: InputEventMouseButton = event
		if mouse_event.pressed and not _input_locked:
			if mouse_event.button_index == MOUSE_BUTTON_LEFT:
				# Try creature attack first; fall back to block mining.
				if not _interaction.try_attack_creature():
					_interaction.try_mine_target(_target)
			elif mouse_event.button_index == MOUSE_BUTTON_RIGHT:
				_interaction.try_place_or_interact(_target)
			elif mouse_event.button_index == MOUSE_BUTTON_WHEEL_UP:
				_select_hotbar((selected_hotbar - 1 + 9) % 9)
			elif mouse_event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
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
	elif key == KEY_I:
		if _ui_connector.close_furnace_if_open():
			return
		if wiki_ui and wiki_ui.visible:
			wiki_ui.toggle()
			_set_input_locked(false)
			return
		_ui_connector.toggle_inventory()
	elif key == KEY_E or key == KEY_SPACE:
		_interaction.try_place_or_interact(_target)
	elif key == KEY_F3:
		if probe_panel:
			probe_panel.toggle_mode()
	elif key == KEY_J:
		_ui_connector.toggle_quest_book()


# --- Movement and physics ---

func _handle_movement(delta: float) -> void:
	# Effective gravity strength adjusted by per-planet multiplier.
	var effective_gravity := GRAVITY_STRENGTH * _gravity_multiplier
	# Jump velocity scales with sqrt of multiplier so jump height
	# is inversely proportional to gravity (physically consistent).
	var effective_jump := JUMP_VELOCITY * sqrt(_gravity_multiplier)

	if _input_locked:
		velocity.x = move_toward(velocity.x, 0.0, move_speed)
		velocity.z = move_toward(velocity.z, 0.0, move_speed)
		velocity += _gravity_direction * effective_gravity * delta
		move_and_slide()
		return

	# Creative fly mode — 6DoF camera-relative movement.
	if fly_mode:
		_handle_fly_movement(delta)
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

	# Zero-G in space: no gravity, free camera-relative movement.
	var is_zero_g := _gravity_direction == Vector3.ZERO

	if is_zero_g:
		_handle_zero_g_movement(delta, input_vector)
		return

	var up := -_gravity_direction
	var xform_basis := global_transform.basis
	var forward := -xform_basis.z
	forward = (forward - up * forward.dot(up)).normalized()
	var right := xform_basis.x
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
			velocity -= _gravity_direction * effective_jump
	else:
		velocity += _gravity_direction * effective_gravity * delta

	move_and_slide()
	_align_body_to_gravity(delta)


# Creative fly mode: 6DoF movement relative to camera.
func _handle_fly_movement(delta: float) -> void:
	var xform_basis := global_transform.basis
	var forward := -xform_basis.z
	var right := xform_basis.x
	var up := xform_basis.y

	var direction := Vector3.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		direction += forward
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		direction -= forward
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		direction += right
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		direction -= right
	if Input.is_key_pressed(KEY_SPACE):
		direction += up
	if Input.is_key_pressed(KEY_SHIFT):
		direction -= up

	if direction != Vector3.ZERO:
		direction = direction.normalized()

	var speed := fly_speed * (sprint_multiplier if Input.is_key_pressed(KEY_CTRL) else 1.0)
	velocity = velocity.move_toward(direction * speed, speed * 12.0 * delta)

	move_and_slide()


# Zero-G movement (space, no fly mode): camera-relative with Space/Shift for up/down.
func _handle_zero_g_movement(delta: float, input_vector: Vector2) -> void:
	var xform_basis := global_transform.basis
	var forward := -xform_basis.z
	var right := xform_basis.x
	var up := xform_basis.y

	var direction := right * input_vector.x + forward * -input_vector.y
	if Input.is_key_pressed(KEY_SPACE):
		direction += up
	if Input.is_key_pressed(KEY_SHIFT):
		direction -= up

	if direction != Vector3.ZERO:
		direction = direction.normalized()

	var speed := move_speed * (sprint_multiplier if Input.is_key_pressed(KEY_CTRL) else 1.0)
	velocity = velocity.move_toward(direction * speed, speed * 10.0 * delta)

	move_and_slide()


func _check_climbing() -> void:
	if world == null:
		_is_climbing = false
		return

	var cell := get_current_cell()
	var dimension := get_current_dimension()

	var candidates := [cell, cell + Vector3i.UP]
	for candidate: Vector3i in candidates:
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
		_gravity_multiplier = 1.0
		return

	# Prefer multi-planet gravity from UniverseManager.
	if _universe_manager != null:
		_gravity_direction = _universe_manager.compute_gravity_direction(global_position)
		_gravity_multiplier = _universe_manager.compute_gravity_multiplier(global_position)
		# Cache the active planet descriptor for atmosphere hazard lookups.
		_active_planet = _universe_manager.active_planet
		if _active_planet != null:
			_atmosphere_type = _active_planet.atmosphere_type
		else:
			_atmosphere_type = PlanetDescriptor.AtmosphereType.NONE
		if fly_mode:
			_gravity_direction = Vector3.ZERO
		return

	# Fallback: single-planet gravity (backward compatible).
	var world_data_node := world.get_world_data()
	if world_data_node == null:
		_gravity_direction = Vector3.DOWN
		_gravity_multiplier = 1.0
		return

	_planet_center = _get_planet_center_from_config(world_data_node.worldgen_config)
	_gravity_direction = GDPlayerHelper.compute_gravity_direction(
		global_position, _planet_center, planet_gravity_radius, use_planet_gravity)

	# Single-planet fallback: derive multiplier from active planet descriptor.
	_gravity_multiplier = 1.0
	if _universe_manager != null and _universe_manager.active_planet != null:
		_gravity_multiplier = _universe_manager.active_planet.gravity_multiplier

	# In fly mode, override gravity to zero so player has full 6DoF control.
	if fly_mode:
		_gravity_direction = Vector3.ZERO


func _get_planet_center_from_config(_config: Resource) -> Vector3:
	# TODO: Expose planet_center through GDWorldGenConfig API.
	return Vector3(0.0, -512.0, 0.0)


func _align_body_to_gravity(delta: float) -> void:
	# Skip alignment in zero-G (space or fly mode).
	if _gravity_direction == Vector3.ZERO:
		return

	if not use_planet_gravity:
		return

	var target_up := -_gravity_direction
	var new_basis := GDPlayerHelper.align_body_to_gravity(
		global_transform.basis, target_up, 8.0, delta)
	global_transform.basis = new_basis


# --- Atmosphere hazard system ---

# Apply environmental damage based on the current planet's atmosphere type.
# BREATHABLE: no damage.
# THIN/NONE: slow suffocation damage without oxygen supply.
# TOXIC: continuous poison damage without suit.
# CORROSIVE: heavy damage + equipment degradation without suit.
# TODO: check player equipment for oxygen mask / hazard suit to negate damage.
func _update_atmosphere_hazard(delta: float) -> void:
	# No hazard in creative fly mode.
	if fly_mode:
		return

	# No hazard in space (zero-G, no active planet).
	if _gravity_direction == Vector3.ZERO:
		return

	var damage_rate := 0.0

	match _atmosphere_type:
		PlanetDescriptor.AtmosphereType.NONE, \
		PlanetDescriptor.AtmosphereType.THIN:
			damage_rate = _active_planet.vacuum_damage_per_sec if _active_planet != null else _DEFAULT_VACUUM_DAMAGE_PER_SEC
		PlanetDescriptor.AtmosphereType.TOXIC:
			damage_rate = _active_planet.toxic_damage_per_sec if _active_planet != null else _DEFAULT_TOXIC_DAMAGE_PER_SEC
		PlanetDescriptor.AtmosphereType.CORROSIVE:
			damage_rate = _active_planet.corrosive_damage_per_sec if _active_planet != null else _DEFAULT_CORROSIVE_DAMAGE_PER_SEC
		PlanetDescriptor.AtmosphereType.BREATHABLE, _:
			damage_rate = 0.0

	if damage_rate <= 0.0:
		_atmo_damage_timer = 0.0
		return

	_atmo_damage_timer += delta
	# TODO: Apply damage to player health system when implemented.
	# For now, log a warning every 2 seconds so developers can verify.
	if _atmo_damage_timer >= 2.0:
		_atmo_damage_timer -= 2.0
		var atmo_name := "unknown"
		match _atmosphere_type:
			PlanetDescriptor.AtmosphereType.NONE:
				atmo_name = "vacuum"
			PlanetDescriptor.AtmosphereType.THIN:
				atmo_name = "thin"
			PlanetDescriptor.AtmosphereType.TOXIC:
				atmo_name = "toxic"
			PlanetDescriptor.AtmosphereType.CORROSIVE:
				atmo_name = "corrosive"
		push_warning("Player in %s atmosphere: %.1f dmg/sec" % [atmo_name, damage_rate])


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


@warning_ignore("unsafe_call_argument")
func _update_target() -> void:
	_target.clear()
	if camera == null or world == null:
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
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
		if probe_panel:
			probe_panel.clear_target()
		return

	var normal: Vector3 = hit.get("normal", Vector3.UP)
	var hit_position: Vector3 = hit.get("position", Vector3.ZERO)
	var block_point := hit_position - normal * 0.01
	var cell := world.world_position_to_cell(block_point)
	var info := world.get_cell_info(cell)
	var data: Dictionary = info.get("data", {})
	if data.is_empty():
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
		return

	var material := int(data.get("material", 0))
	if material == 0:
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
		return

	_target = info
	_target["normal"] = normal
	_target["place_cell"] = world.world_position_to_cell(hit_position + normal * 0.55)
	_target["position"] = hit_position
	_set_selection_visible(true)
	if selection_box:
		selection_box.global_position = world.cell_to_world_position(cell)
	if probe_panel:
		var mat_def: Dictionary = world.get_world_data().get_terrain_material_def(material) if world.get_world_data() else {}
		var tool_def: ToolDef = _get_equipped_tool_def()
		probe_panel.update_target(mat_def, tool_def)


func _set_selection_visible(vis: bool) -> void:
	if selection_box:
		selection_box.visible = vis


# --- Probe panel helpers ---

# Get the ToolDef for the currently equipped main-hand item, or null.
func _get_equipped_tool_def() -> ToolDef:
	var held_id := get_equipped_item_id()
	if held_id == 0:
		return null
	return ItemDatabase.get_tool_stats(held_id)




# --- Hotbar ---

@warning_ignore("unsafe_call_argument")
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
	if _universe_manager != null and _universe_manager.active_planet != null:
		return _universe_manager.active_planet.dimension_id
	if world != null:
		return world.active_dimension
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


# --- Console integration ---

func _connect_universe_manager() -> void:
	_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager


func _connect_console() -> void:
	if console_ui:
		console_ui.set_player(self)
		if not console_ui.console_opened.is_connected(_on_console_opened):
			console_ui.console_opened.connect(_on_console_opened)
		if not console_ui.console_closed.is_connected(_on_console_closed):
			console_ui.console_closed.connect(_on_console_closed)


func _on_console_opened() -> void:
	_set_input_locked(true)


func _on_console_closed() -> void:
	_set_input_locked(false)
	_mouse_captured = true
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


# --- Exit menu (pause menu) ---

func _setup_exit_menu() -> void:
	exit_menu = ExitMenu.new()
	exit_menu.name = "ExitMenu"
	# Add to the UI CanvasLayer so it renders alongside other UIs.
	var ui_layer := get_node_or_null(^"../UI")
	if ui_layer != null:
		ui_layer.add_child(exit_menu)
	else:
		add_child(exit_menu)
	exit_menu.resume_requested.connect(_on_exit_menu_resume)
	exit_menu.return_to_main_menu_requested.connect(_on_exit_menu_return_to_main)
	exit_menu.quit_requested.connect(_on_exit_menu_quit)


func _open_exit_menu() -> void:
	if exit_menu == null:
		return
	# Close any open gameplay UIs first so the exit menu is the top overlay.
	_ui_connector.close_furnace_if_open()
	if wiki_ui and wiki_ui.visible:
		wiki_ui.toggle()
	if inventory_ui and inventory_ui.visible:
		_ui_connector.toggle_inventory()
	if crafting_ui and crafting_ui.visible:
		_ui_connector.toggle_crafting()
	if quest_ui and quest_ui.is_open():
		_ui_connector.toggle_quest_book()
	exit_menu.open()
	_set_input_locked(true)


func _close_exit_menu() -> void:
	if exit_menu == null:
		return
	exit_menu.close()
	_set_input_locked(false)
	_mouse_captured = true
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _on_exit_menu_resume() -> void:
	_close_exit_menu()


func _on_exit_menu_return_to_main() -> void:
	get_tree().change_scene_to_file(MAIN_MENU_SCENE_PATH)


func _on_exit_menu_quit() -> void:
	get_tree().quit()


# --- Status label ---

func _update_status_label() -> void:
	var label := get_node_or_null(^"../UI/LayerStatusLabel") as Label
	if label == null:
		return

	# Atmosphere type short name for status display.
	var atmo_short := ""
	match _atmosphere_type:
		PlanetDescriptor.AtmosphereType.NONE:
			atmo_short = "vacuum"
		PlanetDescriptor.AtmosphereType.THIN:
			atmo_short = "thin"
		PlanetDescriptor.AtmosphereType.BREATHABLE:
			atmo_short = "breathable"
		PlanetDescriptor.AtmosphereType.TOXIC:
			atmo_short = "toxic"
		PlanetDescriptor.AtmosphereType.CORROSIVE:
			atmo_short = "corrosive"

	if fly_mode:
		label.text = "Fly (creative)"
	elif _gravity_direction == Vector3.ZERO:
		label.text = "Space (zero-G)"
	elif _universe_manager != null and _universe_manager.active_planet != null:
		var planet := _universe_manager.active_planet
		var grav_text := "g=%.2f" % _gravity_multiplier
		label.text = "%s (%s, %s)" % [planet.display_name, grav_text, atmo_short]
	else:
		var grav_text := "g=%.2f" % _gravity_multiplier
		label.text = "3D Surface (%s, %s)" % [grav_text, atmo_short]


# --- Quest system ---

@warning_ignore("unsafe_method_access")
func _connect_quest_system() -> void:
	if quest_system == null:
		return

	# Wire inventory query for HAS_ITEM conditions.
	var inv_callable := Callable(self, "_quest_inventory_query")
	quest_system.on_inventory_changed(inv_callable)

	# Wire inventory changed signal to re-evaluate quest conditions.
	if not inventory_changed.is_connected(_on_quest_inventory_changed):
		inventory_changed.connect(_on_quest_inventory_changed)

	# Wire crafting signal.
	if crafting_ui and not crafting_ui.crafted.is_connected(_on_quest_item_crafted):
		crafting_ui.crafted.connect(_on_quest_item_crafted)

	# Wire mining signal via PlayerInteraction.
	if _interaction and not _interaction.block_mined.is_connected(_on_quest_block_mined):
		_interaction.block_mined.connect(_on_quest_block_mined)

	# Wire machine placed signal via PlayerInteraction.
	if _interaction and not _interaction.machine_placed.is_connected(_on_quest_machine_placed):
		_interaction.machine_placed.connect(_on_quest_machine_placed)


func _quest_inventory_query(item_key: String) -> int:
	if inventory == null:
		return 0
	var item_id := ItemDatabase.get_item_id_by_key(item_key)
	if item_id < 0:
		return 0
	return inventory.count_item(item_id)


@warning_ignore("unsafe_method_access")
func _on_quest_inventory_changed() -> void:
	if quest_system == null:
		return
	var inv_callable := Callable(self, "_quest_inventory_query")
	quest_system.on_inventory_changed(inv_callable)


@warning_ignore("unsafe_method_access")
func _on_quest_item_crafted(item_id: int, count: int) -> void:
	if quest_system == null:
		return
	var key := ItemDatabase.get_item_key_by_id(item_id)
	if key.is_empty():
		return
	quest_system.on_item_crafted(key, count)


@warning_ignore("unsafe_method_access")
func _on_quest_block_mined(block_key: String) -> void:
	if quest_system == null:
		return
	quest_system.on_block_mined(block_key, 1)


@warning_ignore("unsafe_method_access")
func _on_quest_machine_placed(machine_type: String) -> void:
	if quest_system == null:
		return
	quest_system.on_machine_placed(machine_type, 1)
