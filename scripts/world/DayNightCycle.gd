# DayNightCycle — drives the visual day/night cycle in the scene tree.
# Reads the DayNightState from GDTickSystem each frame and applies it
# to the sun/moon DirectionalLight3D nodes and the WorldEnvironment.
class_name DayNightCycle
extends Node

signal time_of_day_changed(time_of_day: float)

# Path to the GDTickSystem node.
@export var tick_system_path: NodePath = ^"../GDTickSystem"

# Path to the sun DirectionalLight3D.
@export var sun_path: NodePath = ^"../Sun"

# Path to the moon DirectionalLight3D (created automatically if missing).
@export var moon_path: NodePath = ^"../Moon"

# Path to the WorldEnvironment.
@export var environment_path: NodePath = ^"../WorldEnvironment"

# Maximum sun light energy (at noon).
@export var max_sun_energy: float = 2.2

# Maximum moon light energy (at full moon night).
@export var max_moon_energy: float = 0.15

# Whether to update the environment ambient light.
@export var update_ambient: bool = true

# Whether to update the environment fog color.
@export var update_fog: bool = true

# Internal references.
var _tick_system: GDTickSystem = null
var _sun: DirectionalLight3D = null
var _moon: DirectionalLight3D = null
var _world_env: WorldEnvironment = null
var _env: Environment = null

# Cached state to detect changes.
var _prev_time_of_day: float = -1.0


func _ready() -> void:
	_resolve_nodes()
	_ensure_moon_node()


func _process(_delta: float) -> void:
	if not _tick_system:
		return

	var state: Dictionary = _tick_system.get_day_night_state()
	var tod: float = state.get("time_of_day", 0.5)

	_update_sun(state)
	_update_moon(state)

	if update_ambient and _env:
		_update_ambient(state)

	if update_fog and _env:
		_update_fog(state)

	if absf(tod - _prev_time_of_day) > 0.001:
		_prev_time_of_day = tod
		time_of_day_changed.emit(tod)


# --- Node resolution ---

func _resolve_nodes() -> void:
	if tick_system_path:
		_tick_system = get_node_or_null(tick_system_path) as GDTickSystem
	if sun_path:
		_sun = get_node_or_null(sun_path) as DirectionalLight3D
	if moon_path:
		_moon = get_node_or_null(moon_path) as DirectionalLight3D
	if environment_path:
		_world_env = get_node_or_null(environment_path) as WorldEnvironment
		if _world_env:
			_env = _world_env.environment


func _ensure_moon_node() -> void:
	if _moon:
		return

	# Create a moon DirectionalLight3D if it does not exist.
	var parent: Node = get_parent()
	if not parent:
		return

	_moon = DirectionalLight3D.new()
	_moon.name = "Moon"
	_moon.light_color = Color(0.6, 0.65, 0.8)
	_moon.light_energy = 0.0
	_moon.shadow_enabled = false
	_moon.directional_shadow_max_distance = 100.0
	parent.add_child(_moon)
	_moon.owner = parent


# --- Sun update ---

func _update_sun(state: Dictionary) -> void:
	if not _sun:
		return

	var elevation: float = state.get("sun_elevation", 1.5708)
	var azimuth: float = state.get("sun_azimuth", 0.0)
	var energy: float = state.get("sun_light_energy", 2.2)
	var cr: float = state.get("sun_color_r", 1.0)
	var cg: float = state.get("sun_color_g", 1.0)
	var cb: float = state.get("sun_color_b", 1.0)

	# Convert elevation/azimuth to a direction vector.
	# Elevation: angle above horizon (0 = horizon, pi/2 = zenith).
	# Azimuth: horizontal angle (0 = north, pi/2 = east).
	var dir := Vector3(
		-sin(azimuth) * cos(elevation),
		sin(elevation),
		-cos(azimuth) * cos(elevation)
	)

	# Godot DirectionalLight3D uses -Z as default direction.
	# We set rotation to point the light in the computed direction.
	_sun.look_at(_sun.global_position + dir, _up_for_direction(dir))

	_sun.light_energy = energy
	_sun.light_color = Color(cr, cg, cb)


# --- Moon update ---

func _update_moon(state: Dictionary) -> void:
	if not _moon:
		return

	var elevation: float = state.get("moon_elevation", -1.5708)
	var azimuth: float = state.get("moon_azimuth", 3.14159)
	var energy: float = state.get("moon_light_energy", 0.0)
	var cr: float = state.get("moon_color_r", 0.6)
	var cg: float = state.get("moon_color_g", 0.65)
	var cb: float = state.get("moon_color_b", 0.8)

	var dir := Vector3(
		-sin(azimuth) * cos(elevation),
		sin(elevation),
		-cos(azimuth) * cos(elevation)
	)

	_moon.look_at(_moon.global_position + dir, _up_for_direction(dir))

	_moon.light_energy = energy
	_moon.light_color = Color(cr, cg, cb)


static func _up_for_direction(direction: Vector3) -> Vector3:
	if absf(direction.dot(Vector3.UP)) > 0.999:
		return Vector3.FORWARD
	return Vector3.UP


# --- Ambient light update ---

func _update_ambient(state: Dictionary) -> void:
	if not _env:
		return

	var energy: float = state.get("ambient_energy", 0.5)
	var cr: float = state.get("ambient_color_r", 0.5)
	var cg: float = state.get("ambient_color_g", 0.5)
	var cb: float = state.get("ambient_color_b", 0.6)

	_env.ambient_light_energy = energy
	_env.ambient_light_color = Color(cr, cg, cb)


# --- Fog update ---

func _update_fog(state: Dictionary) -> void:
	if not _env:
		return

	var is_day: bool = state.get("is_daytime", true)
	var cr: float = state.get("ambient_color_r", 0.5)
	var cg: float = state.get("ambient_color_g", 0.5)
	var cb: float = state.get("ambient_color_b", 0.6)

	# Blend fog color with ambient light color.
	# At night, fog is darker and bluer.
	if is_day:
		_env.fog_light_color = Color(cr * 0.9, cg * 0.9, cb * 0.95)
	else:
		_env.fog_light_color = Color(cr * 0.3, cg * 0.3, cb * 0.5)
