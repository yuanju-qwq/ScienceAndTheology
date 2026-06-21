# DayNightCycle — drives the visual day/night cycle in the scene tree.
# Reads the DayNightState from GDTickSystem each frame and applies it
# to the sun/moon DirectionalLight3D nodes and the WorldEnvironment.
class_name DayNightCycle
extends Node

signal time_of_day_changed(time_of_day: float)

const SURFACE_SKY_SHADER := preload("res://resource/shaders/surface_sky.gdshader")

# Path to the GDTickSystem node.
@export var tick_system_path: NodePath = ^"../GDTickSystem"

# Path to the sun DirectionalLight3D.
@export var sun_path: NodePath = ^"../Sun"

# Path to the moon DirectionalLight3D (created automatically if missing).
@export var moon_path: NodePath = ^"../Moon"

# Path to the WorldEnvironment.
@export var environment_path: NodePath = ^"../WorldEnvironment"

# Player orientation supplies the local horizon on a spherical planet.
@export var player_path: NodePath = ^"../Player"

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
var _player: CharacterBody3D = null
var _surface_sky_material: ShaderMaterial = null

# Cached state to detect changes.
var _prev_time_of_day: float = -1.0


func _ready() -> void:
	_resolve_nodes()
	_ensure_moon_node()
	_ensure_surface_sky()


func _process(_delta: float) -> void:
	if not _tick_system:
		return

	var state: Dictionary = _tick_system.get_day_night_state()
	var tod: float = state.get("time_of_day", 0.5)

	_update_sun(state)
	_update_moon(state)
	_update_surface_sky(state)

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
	if player_path:
		_player = get_node_or_null(player_path) as CharacterBody3D


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


func _ensure_surface_sky() -> void:
	if _env == null:
		return
	_surface_sky_material = ShaderMaterial.new()
	_surface_sky_material.shader = SURFACE_SKY_SHADER
	var sky := Sky.new()
	sky.sky_material = _surface_sky_material
	_env.sky = sky
	_env.background_mode = Environment.BG_SKY


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
	var local_up := _get_local_up()
	var dir := _direction_from_angles(elevation, azimuth, local_up)

	# The celestial direction points toward the visible sun; light rays travel
	# in the opposite direction toward the terrain.
	var ray_direction := -dir
	_sun.look_at(_sun.global_position + ray_direction,
			_up_for_direction(ray_direction, local_up))

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

	var local_up := _get_local_up()
	var dir := _direction_from_angles(elevation, azimuth, local_up)

	var ray_direction := -dir
	_moon.look_at(_moon.global_position + ray_direction,
			_up_for_direction(ray_direction, local_up))

	_moon.light_energy = energy
	_moon.light_color = Color(cr, cg, cb)


static func _direction_from_angles(elevation: float, azimuth: float,
		local_up: Vector3) -> Vector3:
	var reference := Vector3.FORWARD
	if absf(reference.dot(local_up)) > 0.95:
		reference = Vector3.RIGHT
	var east := reference.cross(local_up).normalized()
	var north := local_up.cross(east).normalized()
	return (east * sin(azimuth) * cos(elevation)
			+ local_up * sin(elevation)
			+ north * cos(azimuth) * cos(elevation)).normalized()


static func _up_for_direction(direction: Vector3, preferred_up: Vector3) -> Vector3:
	if absf(direction.dot(preferred_up)) > 0.999:
		return direction.cross(Vector3.RIGHT).normalized() \
				if absf(direction.dot(Vector3.RIGHT)) < 0.999 \
				else direction.cross(Vector3.FORWARD).normalized()
	return preferred_up


func _get_local_up() -> Vector3:
	if _player != null and _player.up_direction.length_squared() > 0.5:
		return _player.up_direction.normalized()
	return Vector3.UP


func _update_surface_sky(state: Dictionary) -> void:
	if _surface_sky_material == null:
		return
	var local_up := _get_local_up()
	var sun_elevation: float = state.get("sun_elevation", 1.5708)
	var sun_azimuth: float = state.get("sun_azimuth", 0.0)
	var moon_elevation: float = state.get("moon_elevation", -1.5708)
	var moon_azimuth: float = state.get("moon_azimuth", 3.14159)
	var ambient_energy: float = state.get("ambient_energy", 0.62)
	var sun_energy: float = state.get("sun_light_energy", 2.2)
	var moon_energy: float = state.get("moon_light_energy", 0.0)
	var day_factor := clampf((ambient_energy - 0.15) / 0.47, 0.0, 1.0)

	_surface_sky_material.set_shader_parameter("local_up", local_up)
	_surface_sky_material.set_shader_parameter("sun_direction",
			_direction_from_angles(sun_elevation, sun_azimuth, local_up))
	_surface_sky_material.set_shader_parameter("moon_direction",
			_direction_from_angles(moon_elevation, moon_azimuth, local_up))
	_surface_sky_material.set_shader_parameter("sun_color", Vector3(
			state.get("sun_color_r", 1.0),
			state.get("sun_color_g", 1.0),
			state.get("sun_color_b", 1.0)))
	_surface_sky_material.set_shader_parameter("moon_color", Vector3(
			state.get("moon_color_r", 0.6),
			state.get("moon_color_g", 0.65),
			state.get("moon_color_b", 0.8)))
	_surface_sky_material.set_shader_parameter("day_factor", day_factor)
	_surface_sky_material.set_shader_parameter("sun_visibility",
			clampf(sun_energy / maxf(max_sun_energy, 0.001), 0.0, 1.0))
	_surface_sky_material.set_shader_parameter("moon_visibility",
			clampf(moon_energy / maxf(max_moon_energy, 0.001), 0.0, 1.0))


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
