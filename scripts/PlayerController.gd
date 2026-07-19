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
signal build_mode_changed(mode: int)
signal game_mode_changed(mode: int)

const MOUSE_SENSITIVITY := 0.0025
const GRAVITY_STRENGTH := 22.0
const LoadingOverlayScript := preload("res://scripts/ui/LoadingOverlay.gd")
const PlayerAvatarModelScript := preload("res://scripts/player/PlayerAvatarModel.gd")
const JUMP_VELOCITY := 7.0
const CLIMB_SPEED := 3.0
const OVERWORLD: StringName = &"overworld"
const MAIN_MENU_SCENE_PATH := "res://MainMenu.tscn"

enum GameMode {
	SURVIVAL = 0,
	CREATIVE = 1,
	OBSERVER = 2,
}

var game_mode: int = GameMode.SURVIVAL:
	set(value):
		value = clampi(value, GameMode.SURVIVAL, GameMode.OBSERVER)
		if game_mode == value:
			return
		game_mode = value
		if game_mode == GameMode.OBSERVER:
			collision_layer = 0
			collision_mask = 0
			_is_climbing = false
		else:
			collision_layer = _saved_collision_layer
			collision_mask = _saved_collision_mask
		if game_mode == GameMode.SURVIVAL:
			_is_climbing = false
		game_mode_changed.emit(game_mode)

var _saved_collision_layer := 0
var _saved_collision_mask := 0

# Fly movement speed (adjustable via /speed console command).
var fly_speed := 20.0

# Observer mode movement speed.
var observer_speed := 30.0

# Flight toggle in SURVIVAL mode (CREATIVE and OBSERVER always have flight).
var _flight_enabled := false

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
@export var charcoal_pit_manager_path: NodePath = ^"../CharcoalPitManager"
@export var pit_kiln_manager_path: NodePath = ^"../PitKilnManager"
@export var bloomery_manager_path: NodePath = ^"../BloomeryManager"
@export var anvil_manager_path: NodePath = ^"../AnvilManager"
@export var hotbar_ui_path: NodePath = ^"../UI/HotbarUI"
@export var inventory_ui_path: NodePath = ^"../UI/InventoryUI"
@export var crafting_ui_path: NodePath = ^"../UI/CraftingUI"
@export var console_ui_path: NodePath = ^"../UI/ConsoleUI"
@export var crosshair_path: NodePath = ^"../UI/Crosshair"
@export var connector_prompt_path: NodePath = ^"../UI/ConnectorPrompt"
@export var connector_prompt_label_path: NodePath = ^"../UI/ConnectorPrompt/Label"
@export var probe_panel_path: NodePath = ^"../UI/ProbePanel"
@export var quest_ui_path: NodePath = ^"../UI/QuestBookUI"
@export var knapping_ui_path: NodePath = ^"../UI/KnappingUI"
@export var quest_system_path: NodePath = ^"../GDQuestSystem"
@export var head_path: NodePath = ^"Head"
@export var camera_path: NodePath = ^"Head/Camera3D"
@export var selection_path: NodePath = ^"../SelectionBox"
@export var debug_interactions := false
@export var debug_interval := 0.7
@export var give_debug_starting_items := true
@export var show_world_player_model := true
@export var hide_world_avatar_head_in_first_person := true

var inventory: GDPlayerInventory
var equipment: GDPlayerEquipment
var selected_hotbar := 0

@onready var world: ChunkRendererBridge = get_node_or_null(world_path) as ChunkRendererBridge
@onready var command_server: GameCommandServer = (
		get_node_or_null(command_server_path) as GameCommandServer)
@onready var connector_manager: ConnectorManager = (
	get_node_or_null(connector_manager_path) as ConnectorManager)
@onready var mechanism_manager: MechanismManager = (
	get_node_or_null(mechanism_manager_path) as MechanismManager)
@onready var charcoal_pit_manager: CharcoalPitManager = (
	get_node_or_null(charcoal_pit_manager_path) as CharcoalPitManager)
@onready var pit_kiln_manager: PitKilnManager = (
	get_node_or_null(pit_kiln_manager_path) as PitKilnManager)
@onready var bloomery_manager: BloomeryManager = (
	get_node_or_null(bloomery_manager_path) as BloomeryManager)
@onready var anvil_manager: AnvilManager = (
	get_node_or_null(anvil_manager_path) as AnvilManager)
@onready var hotbar_ui: HotbarUI = get_node_or_null(hotbar_ui_path) as HotbarUI
@onready var inventory_ui: InventoryUI = get_node_or_null(inventory_ui_path) as InventoryUI
@onready var crafting_ui: CraftingUI = get_node_or_null(crafting_ui_path) as CraftingUI
@onready var console_ui: ConsoleUI = get_node_or_null(console_ui_path) as ConsoleUI
@onready var connector_prompt: CanvasItem = get_node_or_null(connector_prompt_path) as CanvasItem
@onready var connector_prompt_label: Label = get_node_or_null(connector_prompt_label_path) as Label
@onready var probe_panel: ProbePanel = get_node_or_null(probe_panel_path) as ProbePanel
@onready var quest_ui: QuestBookUI = get_node_or_null(quest_ui_path) as QuestBookUI
@onready var knapping_ui: KnappingUI = get_node_or_null(knapping_ui_path) as KnappingUI
@onready var quest_system: Node = get_node_or_null(quest_system_path)
@onready var head: Node3D = get_node_or_null(head_path) as Node3D
@onready var camera: Camera3D = get_node_or_null(camera_path) as Camera3D
@onready var hand: HandController
@onready var selection_box: Node3D = get_node_or_null(selection_path) as Node3D

# Exit menu (pause menu) — created programmatically and added to the UI layer.
var exit_menu: ExitMenu = null
var _settings_ui: SettingsUI = null
var creative_inventory_ui: CreativeInventoryUI = null
var nei_panel: NeiPanel = null
var nei_sidebar: NEISidebar = null
var nei_utility_bar: NEIUtilityBar = null
var avatar_model = null

# Sub-modules for separated concerns.
var interaction: PlayerInteraction
var _ui_connector: PlayerUIConnector

# UniverseManager reference for multi-planet gravity.
var universe_manager: UniverseManager = null

var input_locked := false
var _mouse_captured := true
var _pitch := deg_to_rad(-18.0)
var last_cell := Vector3i.ZERO
var last_debug_time := -100.0
var _spawn_debug_time := 0.0
var _target_debug_time := 0.0
# Spawn freeze: keep the player immobile until the chunk it stands on has
# its collision body built. Otherwise the player falls through the surface
# while chunk *data* is ready but the *collider* is still queued.
var _spawn_freeze := true
var _spawn_freeze_logged := false
var _loading_overlay: Control
var target := {}
var _is_climbing := false
var _is_quitting := false
var gravity_direction := Vector3.DOWN
var planet_center := Vector3.ZERO

# Block construction uses the fixed global voxel lattice.
# Planet-local gravity is for movement/support rules, not placement adjacency.
var build_mode := GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES

# Per-planet gravity multiplier (1.0 = Earth-like, 0.38 = Mars-like).
# Updated each frame by _update_gravity_direction().
var _gravity_multiplier := 1.0

# --- Atmosphere hazard system ---

# Cached reference to the active planet descriptor.
# Updated each frame by _update_gravity_direction().
var _active_planet: PlanetDescriptor = null

# Current atmosphere type at the player's position.
# Updated each frame alongside gravity.
var _atmosphere_type: int = PlanetDescriptor.AtmosphereType.BREATHABLE

# Vitals simulation (health + satiation tick + atmosphere hazard) sunk to C++.
# Owns health_current and all per-frame timers; GD only pushes context via
# set_* setters before calling tick(delta).
var _vitals: GDPlayerVitals = null

# Source Law data — drives health_max, health_regen, rejection damage, etc.
# Kept as a direct reference for save/load; also passed to _vitals.
var _source_law: GDPlayerSourceLawData = null

# Satiation (hunger) system — backed by C++ GDSatiationData.
# Kept as a direct reference for save/load; also passed to _vitals.
var _satiation: GDSatiationData = null
var _status_bars: Node = null
var _player_save_data: Dictionary = {}
var _player_save_loaded := false
# Loaded health from save, applied to _vitals after setup_vitals() binds refs.
var _pending_health: float = -1.0


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST and not _is_quitting:
		_is_quitting = true
		_do_save()
		get_tree().quit.call_deferred()


# --- Player save identity ---

func _load_player_save_data() -> void:
	var save_dir := _get_current_save_dir()
	if save_dir == "":
		return
	var identity := _get_current_identity()
	if identity.is_empty():
		return
	var service := _get_player_save_service()
	if service == null:
		return

	_player_save_data = service.load_player(save_dir, identity)
	_player_save_loaded = not _player_save_data.is_empty()
	if _player_save_loaded:
		print("[PlayerController] loaded player save uuid=%s" %
			str(identity.get("player_uuid", "")))


func _apply_player_save_basics() -> void:
	if not _player_save_loaded:
		return
	game_mode = int(_player_save_data.get("game_mode", game_mode))
	# Health is owned by _vitals; defer the actual set until _setup_vitals
	# runs. Stash the loaded value so _ready can apply it after
	# the source_law + satiation refs are bound to _vitals.
	_pending_health = float(_player_save_data.get("health", 100.0))
	selected_hotbar = clampi(int(_player_save_data.get("selected_hotbar", selected_hotbar)), 0, 8)
	if universe_manager != null:
		universe_manager.player_game_mode = game_mode
		universe_manager.player_health = _pending_health


func _get_current_identity() -> Dictionary:
	var identity_manager := get_node_or_null(^"/root/IdentityManager")
	if identity_manager == null:
		return {}
	return identity_manager.get_identity()


func _get_player_save_service() -> Node:
	return get_node_or_null(^"/root/PlayerSaveService")


func _get_current_save_dir() -> String:
	var game_session := get_node_or_null(^"/root/GameSession")
	if game_session != null:
		var session_path := str(game_session.get("save_path"))
		if session_path != "":
			return session_path
	if universe_manager != null:
		return universe_manager.get_save_dir()
	return ""


func _apply_player_save_transform() -> void:
	if not _player_save_loaded:
		return
	var pos_data: Dictionary = _player_save_data.get("position", {})
	if pos_data.is_empty():
		return

	var saved_dimension := StringName(str(pos_data.get("dimension", "")))
	if saved_dimension != &"" and universe_manager != null:
		var current_dimension := (
			universe_manager.active_planet.dimension_id
			if universe_manager.active_planet != null else &"")
		if current_dimension != saved_dimension:
			var planet := universe_manager.get_planet_by_dimension(saved_dimension)
			if planet != null:
				universe_manager.travel_to_planet(planet)
			else:
				push_warning("[PlayerController] saved player dimension not found: %s"
					% String(saved_dimension))

	var position_data: Array = pos_data.get("global_position", [])
	if position_data.size() >= 3:
		global_position = _array_to_vector3(position_data, global_position)
		velocity = Vector3.ZERO
		_spawn_freeze = true
		_spawn_freeze_logged = false
	rotation.y = float(pos_data.get("rotation_y", rotation.y))
	_pitch = float(pos_data.get("pitch", _pitch))


func _save_player_data() -> bool:
	var save_dir := _get_current_save_dir()
	if save_dir == "":
		return false
	var identity := _get_current_identity()
	if identity.is_empty():
		return false
	var service := _get_player_save_service()
	if service == null:
		return false
	return service.save_player(save_dir, identity, _build_player_save_data(identity))


func _build_player_save_data(identity: Dictionary) -> Dictionary:
	var service := _get_player_save_service()
	return {
		"identity": identity.duplicate(true),
		"game_mode": game_mode,
		"health": _vitals.get_health_current() if _vitals != null else 100.0,
		"vitals": _vitals.to_dict() if _vitals != null else {},
		"selected_hotbar": selected_hotbar,
		"position": {
			"dimension": String(get_current_dimension()),
			"global_position": _vector3_to_array(global_position),
			"rotation_y": rotation.y,
			"pitch": _pitch,
		},
		"inventory": (
			service.export_inventory(inventory) if service != null else {}),
		"equipment": (
			service.export_equipment(equipment) if service != null else {}),
		"source_law": (
			_source_law.to_dict() if _source_law != null else {}),
		"satiation": (
			_satiation.to_dict() if _satiation != null else {}),
	}


func _vector3_to_array(value: Vector3) -> Array:
	return [value.x, value.y, value.z]


func _array_to_vector3(value: Array, fallback: Vector3) -> Vector3:
	if value.size() < 3:
		return fallback
	return Vector3(float(value[0]), float(value[1]), float(value[2]))


func _ready() -> void:
	_saved_collision_layer = collision_layer
	_saved_collision_mask = collision_mask

	interaction = PlayerInteraction.new()
	interaction.name = "PlayerInteraction"
	add_child(interaction)
	interaction.setup(self)

	_ui_connector = PlayerUIConnector.new()
	_ui_connector.name = "PlayerUIConnector"
	add_child(_ui_connector)
	_ui_connector.setup(self)

	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_connect_universe_manager()
	if universe_manager != null:
		game_mode = universe_manager.player_game_mode
		# Defer health assignment to _vitals (created in _setup_vitals below).
		_pending_health = universe_manager.player_health
	_load_player_save_data()
	_apply_player_save_basics()
	_setup_inventory()
	_setup_player_avatar()
	_setup_creative_inventory()
	_setup_nei_panel()
	_ui_connector.connect_ui()
	_connect_console()
	_connect_crosshair()
	_setup_vitals()
	# Apply any pending health from save/universe to _vitals and clamp to max.
	if _pending_health >= 0.0:
		_vitals.set_health_current(mini(_pending_health, _vitals.get_health_max()))
		_pending_health = -1.0
	else:
		_vitals.set_health_current(mini(_vitals.get_health_current(), _vitals.get_health_max()))
	_connect_quest_system()
	_setup_exit_menu()
	_apply_player_save_transform()
	_update_camera_rotation()
	if camera:
		hand = HandController.new()
		camera.add_child(hand)
	_select_hotbar(selected_hotbar)
	last_cell = get_current_cell()
	_update_gravity_direction()
	_create_loading_overlay()


func _create_loading_overlay() -> void:
	if world == null:
		return
	# Build the list of initial chunks (y=0 layer, start_chunk_radius ring)
	# to track loading progress on the overlay.
	var radius := world.start_chunk_radius
	var initial_chunks: Array[Vector3i] = []
	for cz in range(-radius, radius + 1):
		for cx in range(-radius, radius + 1):
			initial_chunks.append(Vector3i(cx, 0, cz))

	_loading_overlay = LoadingOverlayScript.new()
	_loading_overlay.setup(world, initial_chunks)
	var ui_layer := get_node_or_null(^"../UI")
	if ui_layer != null:
		ui_layer.add_child(_loading_overlay)
	else:
		add_child(_loading_overlay)
	print("[Player] loading overlay created, tracking %d initial chunks" % initial_chunks.size())


func _physics_process(delta: float) -> void:
	interaction.process_cooldown(delta)
	_update_gravity_direction()
	# CharacterBody3D uses this vector to classify floor contacts. Keep it in
	# sync with spherical gravity when crossing separate chunk colliders.
	if gravity_direction != Vector3.ZERO:
		up_direction = -gravity_direction
	# Push per-frame context to vitals (C++) and tick the full vitals sim:
	# atmosphere hazard + source law rejection + satiation/source_law 20 TPS
	# tick + starvation damage + health regen + clamp.
	_vitals.set_game_mode(game_mode)
	_vitals.set_flight_enabled(_flight_enabled)
	_vitals.set_gravity_is_zero(gravity_direction == Vector3.ZERO)
	var vacuum_dps := (
		_active_planet.vacuum_damage_per_sec
		if _active_planet != null
		else GDPlayerVitals.DEFAULT_VACUUM_DAMAGE_PER_SEC)
	var toxic_dps := (
		_active_planet.toxic_damage_per_sec
		if _active_planet != null
		else GDPlayerVitals.DEFAULT_TOXIC_DAMAGE_PER_SEC)
	var corrosive_dps := (
		_active_planet.corrosive_damage_per_sec
		if _active_planet != null
		else GDPlayerVitals.DEFAULT_CORROSIVE_DAMAGE_PER_SEC)
	_vitals.set_atmosphere(_atmosphere_type, vacuum_dps, toxic_dps, corrosive_dps)
	_vitals.tick(delta)
	_update_target()
	if _spawn_freeze:
		_update_spawn_freeze()
		if _spawn_freeze:
			# Still frozen: zero out any velocity accumulated by gravity/movement
			# and skip move_and_slide so the body stays exactly at spawn point.
			velocity = Vector3.ZERO
			_maybe_debug_spawn_fall(delta)
			return
	_handle_movement(delta)
	interaction.try_auto_cell_events()
	_ui_connector.update_connector_prompt()
	_update_status_label()
	_maybe_debug_spawn_fall(delta)


# --- Input handling ---

func _input(event: InputEvent) -> void:
	# Handle mouse look in _input() (called BEFORE the GUI system) so that
	# visible Control nodes don't swallow mouse motion events before they
	# can reach _unhandled_input().
	if event is InputEventMouseMotion and _mouse_captured and not input_locked:
		var motion: InputEventMouseMotion = event
		rotate_y(-motion.relative.x * MOUSE_SENSITIVITY)
		_pitch = clampf(
			_pitch - motion.relative.y * MOUSE_SENSITIVITY,
			deg_to_rad(-82.0), deg_to_rad(76.0))
		_update_camera_rotation()
		get_viewport().set_input_as_handled()
		return
	# Click-to-capture: if mouse is free (e.g. after window focus loss),
	# re-capture on left click instead of mining.
	if event is InputEventMouseButton and event.pressed and not _mouse_captured \
			and not input_locked:
		var mb: InputEventMouseButton = event
		if mb.button_index == MOUSE_BUTTON_LEFT:
			_mouse_captured = true
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			get_viewport().set_input_as_handled()
			return


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
			if input_locked:
				# A gameplay UI is open — close it instead of opening the exit menu.
				_close_gameplay_ui()
				return
			_open_exit_menu()
			return

	if event is InputEventKey and event.pressed and not event.echo:
		var key_event: InputEventKey = event
		# Allow UI toggle keys even when input_locked so they can close UIs.
		if input_locked and not KeyBindings.is_input_lock_allowed_event(key_event):
			return
		_handle_key(key_event)
		return

	if event is InputEventMouseButton and event.pressed and not input_locked:
		var mouse_event: InputEventMouseButton = event
		if mouse_event.button_index == MOUSE_BUTTON_LEFT:
			if not interaction.try_attack_creature():
				if interaction.try_mine_target(target):
					if hand:
						hand.swing()
			else:
				if hand:
					hand.swing()
		elif mouse_event.button_index == MOUSE_BUTTON_RIGHT:
			interaction.try_place_or_interact(target)
		elif mouse_event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_select_hotbar((selected_hotbar - 1 + 9) % 9)
		elif mouse_event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_select_hotbar((selected_hotbar + 1) % 9)


func _handle_key(event: InputEventKey) -> void:
	var key := event.keycode
	if key >= KEY_1 and key <= KEY_9:
		_select_hotbar(key - KEY_1)
		return

	if KeyBindings.is_action_event(event, &"toggle_mouse"):
		_mouse_captured = not _mouse_captured
		Input.mouse_mode = (
			Input.MOUSE_MODE_CAPTURED if _mouse_captured
			else Input.MOUSE_MODE_VISIBLE)
		return

	if KeyBindings.is_action_event(event, &"toggle_crafting"):
		_ui_connector.toggle_crafting()
	elif KeyBindings.is_action_event(event, &"toggle_inventory"):
		if game_mode == GameMode.CREATIVE:
			_ui_connector.toggle_creative_inventory()
		else:
			_ui_connector.toggle_inventory()
	elif KeyBindings.is_action_event(event, &"toggle_debug"):
		if probe_panel:
			probe_panel.toggle_mode()
	elif KeyBindings.is_action_event(event, &"toggle_build_mode"):
		_toggle_build_mode()
	elif KeyBindings.is_action_event(event, &"toggle_quest_book"):
		_ui_connector.toggle_quest_book()
	elif KeyBindings.is_action_event(event, &"toggle_nei_panel"):
		_ui_connector.toggle_nei()
	elif KeyBindings.is_action_event(event, &"toggle_nei_mode"):
		_toggle_nei_mode()


# Cycle NEI mode: RECIPE -> UTILITY -> RECIPE.
# Mirrors NEI's O key that toggles between recipe/utility/hidden modes.
func _toggle_nei_mode() -> void:
	var nei_settings := get_node_or_null(^"/root/NEISettings")
	if nei_settings == null:
		return
	nei_settings.cycle_mode()
	print("[NEI] mode cycled to: %d" % int(nei_settings.get("mode")))


func _toggle_build_mode() -> void:
	# Planet-local placement was removed because it made block adjacency diverge
	# from the fixed voxel lattice. Keep B as a harmless compatibility hotkey.
	build_mode = GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES
	build_mode_changed.emit(build_mode)
	print("[Player] build mode fixed to %s" % _build_mode_name())


func _effective_build_mode() -> int:
	return GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES


func _build_mode_name() -> String:
	return "global-xyz"


func _build_planet_center() -> Vector3:
	if universe_manager != null and universe_manager.active_planet != null:
		return universe_manager.active_planet.local_center
	return planet_center


# --- Movement and physics ---

func _handle_movement(delta: float) -> void:
	# Effective gravity strength adjusted by per-planet multiplier.
	var effective_gravity := GRAVITY_STRENGTH * _gravity_multiplier
	# Jump velocity scales with sqrt of multiplier so jump height
	# is inversely proportional to gravity (physically consistent).
	var effective_jump := JUMP_VELOCITY * sqrt(_gravity_multiplier)

	if input_locked:
		velocity.x = move_toward(velocity.x, 0.0, move_speed)
		velocity.z = move_toward(velocity.z, 0.0, move_speed)
		velocity += gravity_direction * effective_gravity * delta
		move_and_slide()
		return

	# OBSERVER mode — noclip flight, direct position manipulation.
	if game_mode == GameMode.OBSERVER:
		_handle_observer_movement(delta)
		return

	# CREATIVE mode — 6DoF camera-relative flight with collision.
	if game_mode == GameMode.CREATIVE:
		_handle_fly_movement(delta)
		return

	# SURVIVAL mode with flight enabled.
	if _flight_enabled:
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
	var is_zero_g := gravity_direction == Vector3.ZERO

	if is_zero_g:
		_handle_zero_g_movement(delta, input_vector)
		return

	var up := -gravity_direction
	var xform_basis := global_transform.basis
	var forward := -xform_basis.z
	forward = (forward - up * forward.dot(up)).normalized()
	var right := xform_basis.x
	right = (right - up * right.dot(up)).normalized()

	var direction := (right * input_vector.x + forward * -input_vector.y).normalized()
	var speed := move_speed * (sprint_multiplier if Input.is_key_pressed(KEY_SHIFT) else 1.0)

	var vertical_vel := gravity_direction * velocity.dot(gravity_direction)
	var horizontal_vel := velocity - vertical_vel
	horizontal_vel = horizontal_vel.move_toward(direction * speed, speed * 10.0 * delta)
	velocity = horizontal_vel + vertical_vel

	if _is_climbing:
		velocity.x *= 0.5
		velocity.z *= 0.5
		var climb_vel := gravity_direction * velocity.dot(gravity_direction)
		velocity -= climb_vel
		if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_SPACE):
			velocity -= gravity_direction * CLIMB_SPEED
		elif Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_SHIFT):
			velocity += gravity_direction * CLIMB_SPEED
	elif is_on_floor():
		if Input.is_key_pressed(KEY_SPACE):
			velocity -= gravity_direction * effective_jump
	else:
		velocity += gravity_direction * effective_gravity * delta

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


func _handle_observer_movement(delta: float) -> void:
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

	var speed := observer_speed * (sprint_multiplier if Input.is_key_pressed(KEY_CTRL) else 1.0)
	global_position += direction * speed * delta
	velocity = Vector3.ZERO


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
	for candidate in candidates:
		var info := world.get_cell_info(candidate, dimension)
		var data: Dictionary = info.get("data", {})
		if data.get("is_climbable", false):
			_is_climbing = true
			return

	_is_climbing = false


# Low-frequency debug log to diagnose spawn falling issues.
# Prints player position, gravity, velocity, and chunk readiness once per second.
func _maybe_debug_spawn_fall(delta: float) -> void:
	if not debug_interactions:
		return
	_spawn_debug_time += delta
	if _spawn_debug_time < 1.0:
		return
	_spawn_debug_time = 0.0
	var chunk_ready := false
	var chunk_visible := false
	var chunk_str := "no_world"
	if world != null and world.world_data != null:
		var cx := int(floor(global_position.x / 32.0))
		var cy := int(floor(global_position.y / 32.0))
		var cz := int(floor(global_position.z / 32.0))
		var dim := world.active_dimension
		chunk_ready = world.world_data.has_chunk(dim, cx, cy, cz)
		chunk_visible = world.is_chunk_visible(Vector3i(cx, cy, cz))
		chunk_str = (
			"dim=%s chunk=(%d,%d,%d) ready=%s visible=%s"
			% [dim, cx, cy, cz, chunk_ready, chunk_visible])
	var ap_name := "null"
	if _active_planet != null:
		ap_name = _active_planet.display_name
	print("[Player] pos=%s vel=%s grav_dir=%s grav_mult=%.2f active=%s freeze=%s | %s"
		% [global_position, velocity, gravity_direction, _gravity_multiplier,
			ap_name, _spawn_freeze, chunk_str])


# Spawn freeze check: release the player only when the chunk it stands in
# has its collision body built. This avoids the race where chunk *data* is
# ready but the *collider* is still queued in _process_visible_queue
# (max 2 per frame). Initial chunks are generated only at y=0 layer, so the
# player's spawn chunk (cy=0 when spawn_y < 32) is the one that must be ready.
func _update_spawn_freeze() -> void:
	if world == null or world.world_data == null:
		return
	var cx := int(floor(global_position.x / 32.0))
	var cy := int(floor(global_position.y / 32.0))
	var cz := int(floor(global_position.z / 32.0))
	var here := Vector3i(cx, cy, cz)
	if world.is_chunk_visible(here):
		_spawn_freeze = false
		if not _spawn_freeze_logged:
			_spawn_freeze_logged = true
			print("[Player] spawn freeze released at %s (chunk %s visible)"
				% [global_position, here])
			if _loading_overlay != null:
				_loading_overlay.fade_out_and_free()


# --- Gravity system (delegates math to C++ GDPlayerHelper) ---

func _update_gravity_direction() -> void:
	if not use_planet_gravity or world == null:
		gravity_direction = Vector3.DOWN
		_gravity_multiplier = 1.0
		return

	# Prefer multi-planet gravity from UniverseManager.
	if universe_manager != null:
		gravity_direction = universe_manager.compute_gravity_direction(global_position)
		_gravity_multiplier = universe_manager.compute_gravity_multiplier(global_position)
		# Cache the active planet descriptor for atmosphere hazard lookups.
		_active_planet = universe_manager.active_planet
		if _active_planet != null:
			_atmosphere_type = _active_planet.atmosphere_type
		else:
			_atmosphere_type = PlanetDescriptor.AtmosphereType.NONE
		if game_mode != GameMode.SURVIVAL or _flight_enabled:
			gravity_direction = Vector3.ZERO
		return

	# Fallback: single-planet gravity (backward compatible).
	var world_data_node := world.get_world_data()
	if world_data_node == null:
		gravity_direction = Vector3.DOWN
		_gravity_multiplier = 1.0
		return

	planet_center = _get_planet_center_from_config(world_data_node.worldgen_config)
	gravity_direction = GDPlayerHelper.compute_gravity_direction(
		global_position, planet_center, planet_gravity_radius, use_planet_gravity)

	# Single-planet fallback: derive multiplier from active planet descriptor.
	_gravity_multiplier = 1.0
	if universe_manager != null and universe_manager.active_planet != null:
		_gravity_multiplier = universe_manager.active_planet.gravity_multiplier

	# In non-survival modes or when flight is on, override gravity to zero.
	if game_mode != GameMode.SURVIVAL or _flight_enabled:
		gravity_direction = Vector3.ZERO


func _get_planet_center_from_config(_config: Resource) -> Vector3:
	# TODO: Expose planet_center through GDWorldGenConfig API.
	return Vector3(0.0, -512.0, 0.0)


func _align_body_to_gravity(delta: float) -> void:
	# Skip alignment in zero-G (space or fly mode).
	if gravity_direction == Vector3.ZERO:
		return

	if not use_planet_gravity:
		return

	var target_up := -gravity_direction
	var new_basis := GDPlayerHelper.align_body_to_gravity(
		global_transform.basis, target_up, 8.0, delta)
	global_transform.basis = new_basis


# --- Atmosphere hazard + player vitals (sunk to C++ GDPlayerVitals) ---
# Per-frame atmosphere damage, source law rejection, satiation/source_law
# 20 TPS tick, starvation damage, health regen, and clamp all run in C++.
# GD only pushes per-frame context (game mode, gravity, atmosphere) before
# calling _vitals.tick(delta) in _physics_process.

# --- Player vitals setup ---

func _setup_vitals() -> void:
	_source_law = GDPlayerSourceLawData.new()
	_satiation = GDSatiationData.new()

	# New per-player saves live in saves/<world>/players/. Fall back to
	# legacy universe_meta fields so old worlds migrate naturally on next save.
	if _player_save_loaded:
		var source_law_data: Dictionary = _player_save_data.get("source_law", {})
		var satiation_data: Dictionary = _player_save_data.get("satiation", {})
		if not source_law_data.is_empty():
			_source_law.from_dict(source_law_data)
		if not satiation_data.is_empty():
			_satiation.from_dict(satiation_data)
	elif universe_manager != null:
		if not universe_manager.player_source_law_dict.is_empty():
			_source_law.from_dict(universe_manager.player_source_law_dict)
		if not universe_manager.player_satiation_dict.is_empty():
			_satiation.from_dict(universe_manager.player_satiation_dict)

	# Bind source_law + satiation to the C++ vitals simulator.
	_vitals = GDPlayerVitals.new()
	_vitals.setup(_source_law, _satiation)
	# Restore vitals-owned timers from save if present.
	if _player_save_loaded:
		var vitals_data: Dictionary = _player_save_data.get("vitals", {})
		if not vitals_data.is_empty():
			_vitals.from_dict(vitals_data)

	var bars := preload("res://scripts/ui/PlayerStatusBars.gd").new()
	bars.name = "PlayerStatusBars"
	bars.setup(self)
	_status_bars = bars
	var ui_layer := get_node_or_null(^"../UI")
	if ui_layer != null:
		ui_layer.add_child(bars)
	else:
		add_child(bars)


func get_source_law_data() -> GDPlayerSourceLawData:
	return _source_law


# --- Inventory and equipment ---

func _setup_inventory() -> void:
	inventory = GDPlayerInventory.new()
	inventory.init(inventory_width, inventory_height)
	equipment = GDPlayerEquipment.new()

	var loaded_inventory := false
	if _player_save_loaded:
		var service := _get_player_save_service()
		if service != null:
			loaded_inventory = service.apply_inventory(
				inventory, _player_save_data.get("inventory", {}))
			service.apply_equipment(equipment, _player_save_data.get("equipment", {}))

	if give_debug_starting_items and not loaded_inventory:
		inventory.set_slot(0, ItemDatabase.ITEM_IRON_PICKAXE, 1)
		inventory.set_slot(1, ItemDatabase.ITEM_IRON_SHOVEL, 1)
		inventory.set_slot(2, ItemDatabase.ITEM_WORKBENCH, 8)
		inventory.set_slot(3, ItemDatabase.ITEM_FURNACE, 8)
		inventory.set_slot(4, ItemDatabase.ITEM_LADDER, 8)
		equipment.equip(GDPlayerEquipment.SLOT_MAIN_HAND, ItemDatabase.ITEM_IRON_PICKAXE)

	if command_server != null:
		command_server.register_player(GameCommandServer.LOCAL_PLAYER_HANDLE, inventory, equipment)
		if not command_server.inventory_synced.is_connected(_on_server_inventory_synced):
			command_server.inventory_synced.connect(_on_server_inventory_synced)


func _on_server_inventory_synced() -> void:
	sync_avatar_model()
	inventory_changed.emit()


func _setup_player_avatar() -> void:
	if not show_world_player_model:
		return
	avatar_model = PlayerAvatarModelScript.new()
	avatar_model.name = "PlayerAvatarModel"
	add_child(avatar_model)
	avatar_model.setup_for_world(hide_world_avatar_head_in_first_person)
	sync_avatar_model()


func sync_avatar_model() -> void:
	if avatar_model == null:
		return
	avatar_model.sync_equipment(equipment)


# --- Camera and targeting ---

func _update_camera_rotation() -> void:
	if head:
		head.rotation.x = _pitch


func _update_target() -> void:
	target.clear()
	if camera == null or world == null:
		_debug_target_miss("missing camera/world")
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
		_debug_target_miss("ray missed from=%s to=%s" % [str(from), str(to)])
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
		return

	var normal: Vector3 = hit.get("normal", Vector3.UP)
	var hit_position: Vector3 = hit.get("position", Vector3.ZERO)
	var block_point := hit_position - normal * 0.01
	var cell := world.world_position_to_cell(block_point)
	var dimension := get_current_dimension()
	var info := world.get_cell_info(cell, dimension)
	var data: Dictionary = info.get("data", {})
	if data.is_empty():
		_debug_target_miss("empty cell_info dim=%s cell=%s collider=%s" % [
			String(dimension), str(cell), str(hit.get("collider", null))])
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
		return

	var material := int(data.get("material", 0))
	if material == 0:
		_debug_target_miss("air target dim=%s cell=%s collider=%s" % [
			String(dimension), str(cell), str(hit.get("collider", null))])
		_set_selection_visible(false)
		if probe_panel:
			probe_panel.clear_target()
		return

	target = info
	target["normal"] = normal
	var build_direction := GDPlanetBuildFrame.snap_global_axis(normal)
	target["build_anchor_cell"] = cell
	target["build_direction"] = build_direction
	# Do not cache a derived place_cell in the target. The target only describes
	# the hit block and hit face; PlayerInteraction resolves the actual placement
	# immediately before submitting the authoritative command.
	target["build_mode"] = GDPlanetBuildFrame.BUILD_MODE_GLOBAL_AXES
	target["build_semantic"] = -1
	target["position"] = hit_position
	_set_selection_visible(true)
	if selection_box:
		selection_box.global_position = world.cell_to_world_position(cell)
	if probe_panel:
		var world_data := world.get_world_data()
		var mat_def: Dictionary = (
			world_data.get_terrain_material_def(material)
			if world_data else {})
		if mat_def.is_empty() and material > 0:
			mat_def = {"id": material, "title_key": "Block #%d" % material, "hardness": -1.0}
			if debug_interactions:
				print("[Player] _update_target: material %d has no definition (world_data=%s)" % [material, str(world_data != null)])
		var tool_def: ToolDef = _get_equipped_tool_def()
		probe_panel.update_target(mat_def, tool_def)


func _debug_target_miss(message: String) -> void:
	if not debug_interactions:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if now - _target_debug_time < 1.0:
		return
	_target_debug_time = now
	print("[PlayerTarget] %s" % message)


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

func _select_hotbar(index: int) -> void:
	selected_hotbar = clampi(index, 0, 8)
	var slot: Dictionary = inventory.get_slot(selected_hotbar) if inventory else {}
	var item_id := int(slot.get("item_id", 0))
	equipment.equip(GDPlayerEquipment.SLOT_MAIN_HAND, item_id)
	if hand:
		hand.set_item(item_id)
	sync_avatar_model()
	hotbar_changed.emit(selected_hotbar)
	inventory_changed.emit()
	_ui_connector.update_hotbar_display()


# --- Public queries ---

func get_current_dimension() -> StringName:
	if universe_manager != null and universe_manager.active_planet != null:
		return universe_manager.active_planet.dimension_id
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


func set_game_mode(mode: int) -> void:
	game_mode = mode
	if universe_manager != null:
		universe_manager.player_game_mode = mode


func get_game_mode() -> int:
	return game_mode


func get_game_mode_name() -> String:
	match game_mode:
		GameMode.SURVIVAL:
			return "survival"
		GameMode.CREATIVE:
			return "creative"
		GameMode.OBSERVER:
			return "observer"
	return "unknown"


func set_input_locked(is_locked: bool) -> void:
	input_locked = is_locked
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
	if now - last_debug_time < debug_interval:
		return
	last_debug_time = now
	print("PlayerController3D: ", message)


# --- Console integration ---

func _connect_universe_manager() -> void:
	universe_manager = get_node_or_null(universe_manager_path) as UniverseManager


# --- Multi-planet travel ---

# 旅行到指定名称的星球。委托给 UniverseManager.travel_to_planet_by_name。
# 名称匹配不区分大小写，支持部分匹配（如 "mars" 匹配 "Mars"）。
# 返回 true 表示旅行成功。
func travel_to_planet_by_name(planet_name: String) -> bool:
	if universe_manager == null:
		push_warning("PlayerController: travel_to_planet_by_name — UniverseManager not connected")
		return false
	var ok := universe_manager.travel_to_planet_by_name(planet_name)
	if ok:
		# 旅行后立即刷新重力方向，避免一帧的旧重力。
		_update_gravity_direction()
		print("[PlayerController] traveled to '%s', pos=%s dim=%s" % [
			planet_name, global_position, String(get_current_dimension())])
	return ok


# 获取所有可旅行星球列表（委托给 UniverseManager）。
# 返回数组，每个元素是 { "name": String, "dimension": StringName, "planet": PlanetDescriptor }。
func get_travelable_planets() -> Array:
	if universe_manager == null:
		return []
	return universe_manager.get_travelable_planets()


# 获取玩家当前的宇宙坐标（double 精度，通过 FloatingOrigin）。
func get_player_universe_position() -> Vector3:
	if universe_manager == null:
		return Vector3.ZERO
	return universe_manager.get_player_universe_position()


# 获取玩家到指定星球的宇宙距离。
func get_distance_to_planet(planet: PlanetDescriptor) -> float:
	if universe_manager == null or planet == null:
		return INF
	return universe_manager.get_distance_to_planet(planet)


func _connect_console() -> void:
	if console_ui:
		console_ui.set_player(self)
		if universe_manager != null:
			console_ui.set_permission_level(universe_manager.get_permission_level())
		if not console_ui.console_opened.is_connected(_on_console_opened):
			console_ui.console_opened.connect(_on_console_opened)
		if not console_ui.console_closed.is_connected(_on_console_closed):
			console_ui.console_closed.connect(_on_console_closed)


func _connect_crosshair() -> void:
	var crosshair: Control = get_node_or_null(crosshair_path) as Control
	if crosshair != null:
		game_mode_changed.connect(crosshair.set_game_mode)
		crosshair.set_game_mode(game_mode)


func _on_console_opened() -> void:
	set_input_locked(true)


func _on_console_closed() -> void:
	set_input_locked(false)
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
	exit_menu.settings_requested.connect(_on_exit_menu_settings)


func _setup_creative_inventory() -> void:
	creative_inventory_ui = CreativeInventoryUI.new()
	creative_inventory_ui.name = "CreativeInventoryUI"
	var ui_layer := get_node_or_null(^"../UI")
	if ui_layer != null:
		ui_layer.add_child(creative_inventory_ui)
	else:
		add_child(creative_inventory_ui)


func _setup_nei_panel() -> void:
	nei_panel = NeiPanel.new()
	nei_panel.name = "NeiPanel"
	nei_panel.set_player(self)
	var ui_layer := get_node_or_null(^"../UI")
	if ui_layer != null:
		ui_layer.add_child(nei_panel)
	else:
		add_child(nei_panel)
	_setup_nei_sidebar(ui_layer)
	_setup_nei_utility_bar(ui_layer)


# NEI sidebar — persistent item browser shown alongside the inventory.
func _setup_nei_sidebar(ui_layer: Node) -> void:
	nei_sidebar = NEISidebar.new()
	nei_sidebar.name = "NEISidebar"
	nei_sidebar.set_player(self)
	nei_sidebar.item_activated.connect(_on_nei_sidebar_item_activated)
	if ui_layer != null:
		ui_layer.add_child(nei_sidebar)
	else:
		add_child(nei_sidebar)


# NEI utility bar — cheat/utility buttons shown in UTILITY mode.
func _setup_nei_utility_bar(ui_layer: Node) -> void:
	nei_utility_bar = NEIUtilityBar.new()
	nei_utility_bar.name = "NEIUtilityBar"
	nei_utility_bar.set_player(self)
	if ui_layer != null:
		ui_layer.add_child(nei_utility_bar)
	else:
		add_child(nei_utility_bar)


# Open the NEI panel for a specific item from the sidebar.
func _on_nei_sidebar_item_activated(item_id: int, mode: String) -> void:
	if nei_panel == null:
		return
	if not nei_panel.visible:
		nei_panel.open()
	nei_panel.open_for_item(item_id, mode)


func _open_exit_menu() -> void:
	if exit_menu == null:
		return
	# Close any open gameplay UIs first so the exit menu is the top overlay.
	_close_gameplay_ui()
	exit_menu.open()
	set_input_locked(true)


func _close_gameplay_ui() -> void:
	if inventory_ui and inventory_ui.visible:
		inventory_ui._is_open = false
		inventory_ui.visible = false
	if creative_inventory_ui and creative_inventory_ui._is_open:
		creative_inventory_ui._is_open = false
		creative_inventory_ui.visible = false
	if crafting_ui and crafting_ui.visible:
		crafting_ui._is_open = false
		crafting_ui.visible = false
	if quest_ui and quest_ui.is_open():
		quest_ui.toggle()
	if nei_panel and nei_panel.visible:
		nei_panel._is_open = false
		nei_panel.visible = false
	if nei_sidebar and nei_sidebar.visible:
		nei_sidebar.hide_sidebar()
	set_input_locked(false)


func _close_exit_menu() -> void:
	if exit_menu == null:
		return
	exit_menu.close()
	set_input_locked(false)
	_mouse_captured = true
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _on_exit_menu_resume() -> void:
	_close_exit_menu()


func _do_save() -> void:
	var player_ok := _save_player_data()
	if player_ok:
		print("[PlayerController] player saved before exit")
	else:
		push_warning("[PlayerController] failed to save player before exit")
	if universe_manager != null:
		universe_manager.player_game_mode = game_mode
		universe_manager.player_health = _vitals.get_health_current() if _vitals != null else 100.0
		if _source_law != null:
			universe_manager.player_source_law_dict = _source_law.to_dict()
		if _satiation != null:
			universe_manager.player_satiation_dict = _satiation.to_dict()
		var ok := universe_manager.save_universe()
		if ok:
			print("[PlayerController] world saved before exit")
		else:
			push_warning("[PlayerController] failed to save world before exit")


func _on_exit_menu_return_to_main() -> void:
	if not _is_quitting:
		_is_quitting = true
		_do_save()
	get_tree().change_scene_to_file(MAIN_MENU_SCENE_PATH)


func _on_exit_menu_quit() -> void:
	if not _is_quitting:
		_is_quitting = true
		_do_save()
	get_tree().quit()


func _on_exit_menu_settings() -> void:
	if exit_menu:
		exit_menu.close()
	if _settings_ui == null:
		_settings_ui = SettingsUI.new()
		_settings_ui.name = "SettingsUI"
		var ui_layer := get_node_or_null(^"../UI")
		if ui_layer != null:
			ui_layer.add_child(_settings_ui)
		else:
			add_child(_settings_ui)
		_settings_ui.closed.connect(_on_settings_closed)
	_settings_ui.open()


func _on_settings_closed() -> void:
	# 关闭设置后回到暂停菜单。
	if exit_menu:
		exit_menu.open()


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

	var build_text := " | build=%s" % _build_mode_name()
	if game_mode == GameMode.OBSERVER:
		label.text = "Observer (noclip)" + build_text
		return
	if game_mode == GameMode.CREATIVE:
		label.text = "Creative (fly)" + build_text
		return
	# SURVIVAL mode.
	if _flight_enabled:
		label.text = "Survival (fly)" + build_text
		return
	if gravity_direction == Vector3.ZERO:
		label.text = "Space (zero-G)" + build_text
	elif universe_manager != null and universe_manager.active_planet != null:
		var planet := universe_manager.active_planet
		var grav_text := "g=%.2f" % _gravity_multiplier
		var upos := get_player_universe_position()
		label.text = "%s (%s, %s) @U(%.0f,%.0f,%.0f)%s" % [
			planet.display_name, grav_text, atmo_short, upos.x, upos.y, upos.z,
			build_text]
	else:
		var grav_text := "g=%.2f" % _gravity_multiplier
		label.text = "3D Surface (%s, %s)%s" % [grav_text, atmo_short, build_text]


# --- Quest system ---

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
	if interaction and not interaction.block_mined.is_connected(_on_quest_block_mined):
		interaction.block_mined.connect(_on_quest_block_mined)

	# Wire machine placed signal via PlayerInteraction.
	if interaction and not interaction.machine_placed.is_connected(_on_quest_machine_placed):
		interaction.machine_placed.connect(_on_quest_machine_placed)

	# Wire crop planted signal via PlayerInteraction (tracked as block_mined event).
	if interaction and not interaction.crop_planted.is_connected(_on_quest_crop_planted):
		interaction.crop_planted.connect(_on_quest_crop_planted)

	# Wire crop fertilized signal via PlayerInteraction (tracked as block_mined event).
	if interaction and not interaction.crop_fertilized.is_connected(_on_quest_crop_fertilized):
		interaction.crop_fertilized.connect(_on_quest_crop_fertilized)


func _quest_inventory_query(item_key: String) -> int:
	if inventory == null:
		return 0
	var item_id := ItemDatabase.get_item_id_by_key(item_key)
	if item_id < 0:
		return 0
	return inventory.count_item(item_id)


func _on_quest_inventory_changed() -> void:
	if quest_system == null:
		return
	var inv_callable := Callable(self, "_quest_inventory_query")
	quest_system.on_inventory_changed(inv_callable)


func _on_quest_item_crafted(item_id: int, count: int) -> void:
	if quest_system == null:
		return
	var key := ItemDatabase.get_item_key_by_id(item_id)
	if key.is_empty():
		return
	quest_system.on_item_crafted(key, count)


func _on_quest_block_mined(block_key: String) -> void:
	if quest_system == null:
		return
	quest_system.on_block_mined(block_key, 1)


func _on_quest_machine_placed(machine_type: String) -> void:
	if quest_system == null:
		return
	quest_system.on_machine_placed(machine_type, 1)


# Crop planted event — tracked via on_block_mined with synthetic key "crop_planted".
func _on_quest_crop_planted(_species_key: String) -> void:
	if quest_system == null:
		return
	quest_system.on_block_mined("crop_planted", 1)


# Crop fertilized event — tracked via on_block_mined with synthetic key "crop_fertilized".
func _on_quest_crop_fertilized(_species_key: String) -> void:
	if quest_system == null:
		return
	quest_system.on_block_mined("crop_fertilized", 1)
