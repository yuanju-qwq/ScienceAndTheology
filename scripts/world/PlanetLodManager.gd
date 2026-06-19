# PlanetLodManager — manages planet LOD lifecycle and visibility.
# Delegates LOD level computation to GDPlanetLod (C++) and keeps
# Godot scene-tree operations (mesh creation, visibility, fade)
# in GDScript.
class_name PlanetLodManager
extends Node3D

signal lod_level_changed(new_level: int, old_level: int)

const PlanetSurfaceShader := preload("res://resource/shaders/planet_surface.gdshader")
const PlanetAtmosphereShader := preload("res://resource/shaders/planet_atmosphere.gdshader")
const PlanetCloudsShader := preload("res://resource/shaders/planet_clouds.gdshader")
const PlanetBillboardShader := preload("res://resource/shaders/planet_billboard.gdshader")
const SpaceSkyShader := preload("res://resource/shaders/space_sky.gdshader")

# LOD level constants matching the C++ GDPlanetLod definitions.
const LOD_REAL_CHUNKS := 0
const LOD_SIMPLIFIED_MESH := 1
const LOD_PROXY_SPHERE := 2
const LOD_LOW_POLY := 3
const LOD_BILLBOARD := 4

@export var planet_center := Vector3(0.0, -512.0, 0.0):
	set(value):
		planet_center = value
		_invalidate_lod_state()

@export var planet_radius := 512.0:
	set(value):
		planet_radius = maxf(value, 1.0)
		_invalidate_lod_state()

# 是否为当前活跃星球。
# 活跃星球使用局部体素坐标（player.global_position 即局部坐标）。
# 远景星球使用相对宇宙坐标（相对于活跃星球的偏移）。
var is_active_planet := false:
	set(value):
		if is_active_planet == value:
			return
		is_active_planet = value

# 远景星球在场景中的渲染中心（由 UniverseManager 计算）。
# 活跃星球忽略此值，直接使用 planet_center（= local_center）。
var distant_scene_center := Vector3.ZERO

@export var world_seed := 0:
	set(value):
		world_seed = value

@export var atmosphere_color := Color(0.3, 0.6, 1.0, 1.0)
@export var atmosphere_scale := 1.08
@export var atmosphere_power := 3.5
@export var atmosphere_intensity := 1.2

@export var cloud_scale := 1.03
@export var cloud_coverage := 0.45
@export var cloud_sharpness := 2.5
@export var cloud_color := Color(0.95, 0.95, 0.97, 1.0)
@export var cloud_rotation_speed := 0.02

@export var horizon_fog_enabled := true
@export var horizon_fog_color := Color(0.55, 0.70, 0.90, 1.0)
@export var horizon_fog_max_density := 0.04
@export var horizon_fog_max_distance := 200.0
@export var world_environment_path: NodePath = ^"../WorldEnvironment"

@export var space_sky_enabled := true
@export var space_star_brightness := 1.5
@export var space_nebula_intensity := 0.3

@export var player_node_path: NodePath = ^"../Player"
@export var show_debug_info := false
@export var debug_info_interval := 0.5

# LOD mesh nodes (created in _ready).
var _lod_meshes: Dictionary = {}
# Atmosphere shell (visible at LOD 2+).
var _atmosphere_mesh: MeshInstance3D = null
# Cloud shell (visible at LOD 2+).
var _cloud_mesh: MeshInstance3D = null
# WorldEnvironment for horizon fog.
var _world_env: WorldEnvironment = null
var _env: Environment = null
var _owns_world_env := false
var _current_lod_level := -1
var _previous_lod_level := -1
var _fade_alpha := 0.0
var _debug_elapsed := 0.0
var _cloud_time := 0.0
var _distances_cache: Dictionary = {}
var _is_initialized := false

# Space sky state.
var _space_sky_material: ShaderMaterial = null
var _surface_sky: Sky = null
var _surface_bg_mode: Environment.BGMode = Environment.BG_CLEAR_COLOR
var _surface_ambient_color: Color = Color.WHITE
var _surface_ambient_energy: float = 0.62
var _is_space_env_active := false


func _ready() -> void:
	_create_lod_meshes()
	_create_horizon_fog()
	_create_space_sky()
	_update_lod_distances_cache()
	_current_lod_level = _compute_current_lod()
	_previous_lod_level = _current_lod_level
	_apply_lod_visibility()
	# UniverseManager._update_all_lod_centers() may have updated
	# distant_scene_center / planet_center before _ready() ran.
	# Reposition meshes to the effective center so distant planets
	# (e.g. the Sun) are placed at their correct scene offset instead
	# of the creation-time planet_center (= universe_position).
	_reposition_meshes(get_effective_center())
	_is_initialized = true


func _process(delta: float) -> void:
	var new_level := _compute_current_lod()

	if new_level != _current_lod_level:
		_previous_lod_level = _current_lod_level
		_current_lod_level = new_level
		_apply_lod_visibility()
		lod_level_changed.emit(new_level, _previous_lod_level)

	var effective_center := get_effective_center()
	_fade_alpha = GDPlanetLod.compute_lod_fade_alpha(
		_get_player_position(), effective_center, planet_radius, _current_lod_level)
	_apply_fade_alpha()
	_update_cloud_time(delta)
	_update_horizon_fog()
	_update_space_environment()

	if show_debug_info:
		_maybe_log_debug(delta)


# --- Public API ---

func get_current_lod_level() -> int:
	return _current_lod_level


func get_fade_alpha() -> float:
	return _fade_alpha


func get_surface_distance() -> float:
	return GDPlanetLod.compute_surface_distance(
		_get_player_position(), get_effective_center(), planet_radius)


func get_lod_distances() -> Dictionary:
	return _distances_cache.duplicate()


# 返回当前生效的星球中心。
# 活跃星球使用 planet_center（局部坐标），远景星球使用 distant_scene_center。
func get_effective_center() -> Vector3:
	return distant_scene_center if not is_active_planet else planet_center


# 轻量更新场景中心位置（不重建 mesh，仅重新定位）。
# 用于活跃星球切换时更新远景星球的相对位置。
func set_scene_center(center: Vector3, active: bool) -> void:
	is_active_planet = active
	if active:
		# 活跃星球：center = local_center，需要重建以对齐 mesh。
		if planet_center != center:
			planet_center = center  # 触发 setter 重建
		else:
			_reposition_meshes(center)
	else:
		# 远景星球：center = 相对宇宙位置，仅重新定位 mesh。
		distant_scene_center = center
		_reposition_meshes(center)


# 重新定位所有 LOD mesh 到指定中心（不重建）。
func _reposition_meshes(center: Vector3) -> void:
	for level: int in _lod_meshes.keys():
		var mesh_instance: MeshInstance3D = _lod_meshes[level]
		if mesh_instance:
			mesh_instance.global_position = center
	if _atmosphere_mesh:
		_atmosphere_mesh.global_position = center
	if _cloud_mesh:
		_cloud_mesh.global_position = center


# --- LOD mesh creation ---

func _create_lod_meshes() -> void:
	# LOD 1 is managed by ChunkRendererBridge (simplified chunk views).
	# No mesh is needed here for LOD 1.

	# LOD 2: planet proxy sphere — medium detail with procedural terrain shader.
	var lod2_sphere := SphereMesh.new()
	lod2_sphere.radius = planet_radius
	lod2_sphere.height = planet_radius * 2.0
	lod2_sphere.radial_segments = 64
	lod2_sphere.rings = 32
	var lod2_material := ShaderMaterial.new()
	lod2_material.shader = PlanetSurfaceShader
	lod2_material.set_shader_parameter("noise_offset", Vector3(_world_seed_float(), _world_seed_float(), _world_seed_float()))
	lod2_material.set_shader_parameter("sea_level", 0.35)
	lod2_material.set_shader_parameter("snow_line", 0.85)
	lod2_sphere.surface_set_material(0, lod2_material)
	var lod2 := MeshInstance3D.new()
	lod2.name = "LOD2_ProxySphere"
	lod2.mesh = lod2_sphere
	add_child(lod2)
	lod2.global_position = planet_center
	lod2.visible = false
	_lod_meshes[LOD_PROXY_SPHERE] = lod2

	# LOD 3: low-poly sphere — very far / space view with same shader.
	var lod3_sphere := SphereMesh.new()
	lod3_sphere.radius = planet_radius
	lod3_sphere.height = planet_radius * 2.0
	lod3_sphere.radial_segments = 16
	lod3_sphere.rings = 8
	var lod3_material := ShaderMaterial.new()
	lod3_material.shader = PlanetSurfaceShader
	lod3_material.set_shader_parameter("noise_offset", Vector3(_world_seed_float(), _world_seed_float(), _world_seed_float()))
	lod3_material.set_shader_parameter("sea_level", 0.35)
	lod3_material.set_shader_parameter("snow_line", 0.85)
	lod3_sphere.surface_set_material(0, lod3_material)
	var lod3 := MeshInstance3D.new()
	lod3.name = "LOD3_LowPolySphere"
	lod3.mesh = lod3_sphere
	add_child(lod3)
	lod3.global_position = planet_center
	lod3.visible = false
	_lod_meshes[LOD_LOW_POLY] = lod3

	# LOD 4: billboard — extreme distance / deep space.
	# Camera-facing quad with procedural planet disc + atmosphere glow.
	# Sized to match the atmosphere shell so the glow ring is visible.
	var billboard_size := planet_radius * atmosphere_scale * 2.0
	var billboard_quad := QuadMesh.new()
	billboard_quad.size = Vector2(billboard_size, billboard_size)
	var billboard_material := ShaderMaterial.new()
	billboard_material.shader = PlanetBillboardShader
	billboard_material.set_shader_parameter("noise_offset", Vector3(_world_seed_float(), _world_seed_float(), _world_seed_float()))
	billboard_material.set_shader_parameter("sea_level", 0.35)
	billboard_material.set_shader_parameter("snow_line", 0.85)
	billboard_material.set_shader_parameter("atmosphere_color", Vector3(atmosphere_color.r, atmosphere_color.g, atmosphere_color.b))
	billboard_material.set_shader_parameter("atmosphere_intensity", atmosphere_intensity)
	# disc_radius = planet_radius / (planet_radius * atmosphere_scale) = 1.0 / atmosphere_scale
	billboard_material.set_shader_parameter("disc_radius", 1.0 / atmosphere_scale)
	billboard_material.set_shader_parameter("fade_alpha", 1.0)
	billboard_quad.surface_set_material(0, billboard_material)
	var lod4 := MeshInstance3D.new()
	lod4.name = "LOD4_Billboard"
	lod4.mesh = billboard_quad
	# Disable frustum culling for the billboard — at extreme distances the
	# bounding sphere can be imprecise and cause flickering. The quad is
	# cheap to render so over-drawing is acceptable.
	lod4.extra_cull_margin = 16384.0
	add_child(lod4)
	lod4.global_position = planet_center
	lod4.visible = false
	_lod_meshes[LOD_BILLBOARD] = lod4

	# Atmosphere shell — slightly larger sphere with Fresnel glow.
	var atmo_radius := planet_radius * atmosphere_scale
	var atmo_sphere := SphereMesh.new()
	atmo_sphere.radius = atmo_radius
	atmo_sphere.height = atmo_radius * 2.0
	atmo_sphere.radial_segments = 64
	atmo_sphere.rings = 32
	var atmo_material := ShaderMaterial.new()
	atmo_material.shader = PlanetAtmosphereShader
	atmo_material.set_shader_parameter("atmosphere_color", Vector3(atmosphere_color.r, atmosphere_color.g, atmosphere_color.b))
	atmo_material.set_shader_parameter("atmosphere_power", atmosphere_power)
	atmo_material.set_shader_parameter("atmosphere_intensity", atmosphere_intensity)
	atmo_material.set_shader_parameter("fade_alpha", 1.0)
	atmo_sphere.surface_set_material(0, atmo_material)
	_atmosphere_mesh = MeshInstance3D.new()
	_atmosphere_mesh.name = "Atmosphere"
	_atmosphere_mesh.mesh = atmo_sphere
	add_child(_atmosphere_mesh)
	_atmosphere_mesh.global_position = planet_center
	_atmosphere_mesh.visible = false

	# Cloud shell — between surface and atmosphere.
	var cloud_radius := planet_radius * cloud_scale
	var cloud_sphere := SphereMesh.new()
	cloud_sphere.radius = cloud_radius
	cloud_sphere.height = cloud_radius * 2.0
	cloud_sphere.radial_segments = 64
	cloud_sphere.rings = 32
	var cloud_material := ShaderMaterial.new()
	cloud_material.shader = PlanetCloudsShader
	cloud_material.set_shader_parameter("noise_offset", Vector3(_world_seed_float() + 100.0, _world_seed_float() + 200.0, _world_seed_float() + 300.0))
	cloud_material.set_shader_parameter("cloud_coverage", cloud_coverage)
	cloud_material.set_shader_parameter("cloud_sharpness", cloud_sharpness)
	cloud_material.set_shader_parameter("cloud_color", Vector3(cloud_color.r, cloud_color.g, cloud_color.b))
	cloud_material.set_shader_parameter("time", 0.0)
	cloud_material.set_shader_parameter("rotation_speed", cloud_rotation_speed)
	cloud_material.set_shader_parameter("fade_alpha", 1.0)
	cloud_sphere.surface_set_material(0, cloud_material)
	_cloud_mesh = MeshInstance3D.new()
	_cloud_mesh.name = "Clouds"
	_cloud_mesh.mesh = cloud_sphere
	add_child(_cloud_mesh)
	_cloud_mesh.global_position = planet_center
	_cloud_mesh.visible = false


# --- LOD computation ---

func _compute_current_lod() -> int:
	return GDPlanetLod.compute_lod_level(
		_get_player_position(), get_effective_center(), planet_radius)


func _get_player_position() -> Vector3:
	var player := get_node_or_null(player_node_path) as Node3D
	if player:
		return player.global_position
	return global_position


# --- LOD visibility management ---

func _apply_lod_visibility() -> void:
	# LOD 0 and LOD 1 are managed by ChunkRendererBridge.
	# Only toggle visibility for LOD 2, LOD 3 and LOD 4 meshes here.
	for level: int in _lod_meshes.keys():
		var mesh_instance: MeshInstance3D = _lod_meshes[level]
		mesh_instance.visible = (level == _current_lod_level)

	# Atmosphere shell is visible at LOD 2 and LOD 3.
	# At LOD 4 the billboard shader includes its own atmosphere glow.
	if _atmosphere_mesh:
		_atmosphere_mesh.visible = _current_lod_level >= LOD_PROXY_SPHERE \
			and _current_lod_level < LOD_BILLBOARD

	# Clouds are visible at LOD 2 and LOD 3 (same as atmosphere).
	# At LOD 4 the planet is too far for clouds to be distinguishable.
	if _cloud_mesh:
		_cloud_mesh.visible = _current_lod_level >= LOD_PROXY_SPHERE \
			and _current_lod_level < LOD_BILLBOARD


func _apply_fade_alpha() -> void:
	# Apply fade alpha to the current LOD mesh material.
	# Supports both ShaderMaterial and StandardMaterial3D.
	var mesh_instance: MeshInstance3D = _lod_meshes.get(_current_lod_level)
	if mesh_instance == null or mesh_instance.mesh == null:
		return

	var material := mesh_instance.mesh.surface_get_material(0)
	if material == null:
		return

	# _fade_alpha: 0.0 = fully in current LOD, 1.0 = ready to switch (should be fully transparent).
	# alpha: 1.0 = fully opaque, 0.0 = fully transparent (matches shader convention).
	var alpha := 1.0 - _fade_alpha

	if material is ShaderMaterial:
		var shader_mat: ShaderMaterial = material
		if _fade_alpha > 0.01:
			shader_mat.set_shader_parameter("fade_alpha", alpha)
		else:
			shader_mat.set_shader_parameter("fade_alpha", 1.0)
	elif material is StandardMaterial3D:
		var std_mat: StandardMaterial3D = material
		if _fade_alpha > 0.01:
			std_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			std_mat.albedo_color.a = alpha
		else:
			if std_mat.albedo_color.a < 1.0:
				std_mat.albedo_color.a = 1.0
				std_mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED

	# Also apply fade to atmosphere shell.
	if _atmosphere_mesh and _atmosphere_mesh.mesh:
		var atmo_mat := _atmosphere_mesh.mesh.surface_get_material(0) as ShaderMaterial
		if atmo_mat:
			if _fade_alpha > 0.01:
				atmo_mat.set_shader_parameter("fade_alpha", alpha)
			else:
				atmo_mat.set_shader_parameter("fade_alpha", 1.0)

	# Also apply fade to cloud shell.
	if _cloud_mesh and _cloud_mesh.mesh:
		var cloud_mat := _cloud_mesh.mesh.surface_get_material(0) as ShaderMaterial
		if cloud_mat:
			if _fade_alpha > 0.01:
				cloud_mat.set_shader_parameter("fade_alpha", alpha)
			else:
				cloud_mat.set_shader_parameter("fade_alpha", 1.0)


# --- Cache and state management ---

func _invalidate_lod_state() -> void:
	_update_lod_distances_cache()
	if _is_initialized:
		_rebuild_lod_meshes()


func _update_lod_distances_cache() -> void:
	_distances_cache = GDPlanetLod.compute_lod_distances(planet_radius)


func _rebuild_lod_meshes() -> void:
	for level: int in _lod_meshes.keys():
		var node: Node = _lod_meshes[level]
		node.queue_free()
	_lod_meshes.clear()
	if _atmosphere_mesh:
		_atmosphere_mesh.queue_free()
		_atmosphere_mesh = null
	if _cloud_mesh:
		_cloud_mesh.queue_free()
		_cloud_mesh = null
	if _owns_world_env and _world_env:
		_world_env.queue_free()
	_world_env = null
	_env = null
	_owns_world_env = false
	_create_lod_meshes()
	_create_horizon_fog()
	_create_space_sky()
	_apply_lod_visibility()
	_is_space_env_active = false


# --- Horizon fog ---

func _create_horizon_fog() -> void:
	if not horizon_fog_enabled:
		return

	# Try to reuse an existing WorldEnvironment from the scene.
	var existing_env_node := get_node_or_null(world_environment_path) as WorldEnvironment
	if existing_env_node and existing_env_node.environment:
		_world_env = existing_env_node
		_env = existing_env_node.environment
		_owns_world_env = false
	else:
		# No existing WorldEnvironment found — create our own.
		_env = Environment.new()
		_env.background_mode = Environment.BG_CLEAR_COLOR
		_world_env = WorldEnvironment.new()
		_world_env.name = "HorizonFog"
		_world_env.environment = _env
		add_child(_world_env)
		_owns_world_env = true

	_env.fog_enabled = true
	_env.fog_light_color = horizon_fog_color
	_env.fog_density = 0.0
	_env.fog_sky_affect = 0.0
	_env.fog_depth_begin = 0.0
	_env.fog_depth_end = horizon_fog_max_distance


func _update_horizon_fog() -> void:
	if not horizon_fog_enabled or _env == null:
		return

	# Fog is only active at LOD 0 and LOD 1 (near / mid surface).
	# At LOD 2+ the player is too far for fog to make sense.
	var surface_dist := get_surface_distance()

	# Altitude factor: fog fades as the player rises above the surface.
	# At surface (0m): full fog. At horizon_fog_max_distance: no fog.
	var altitude_factor := clampf(1.0 - surface_dist / horizon_fog_max_distance, 0.0, 1.0)

	# LOD factor: disable fog when viewing the planet from far away.
	var lod_factor := 1.0 if _current_lod_level <= LOD_SIMPLIFIED_MESH else 0.0

	var density := horizon_fog_max_density * altitude_factor * lod_factor
	_env.fog_density = density


# --- Cloud animation ---

func _update_cloud_time(delta: float) -> void:
	if _cloud_mesh == null or _cloud_mesh.mesh == null:
		return
	_cloud_time += delta
	var cloud_mat := _cloud_mesh.mesh.surface_get_material(0) as ShaderMaterial
	if cloud_mat:
		cloud_mat.set_shader_parameter("time", _cloud_time)


# --- Space sky and environment switching ---

func _create_space_sky() -> void:
	if not space_sky_enabled:
		return

	# Create the ShaderMaterial for the procedural star field.
	_space_sky_material = ShaderMaterial.new()
	_space_sky_material.shader = SpaceSkyShader
	_space_sky_material.set_shader_parameter("star_brightness", space_star_brightness)
	_space_sky_material.set_shader_parameter("star_seed", _world_seed_float() * 100.0)
	_space_sky_material.set_shader_parameter("nebula_intensity", space_nebula_intensity)


func _update_space_environment() -> void:
	if _env == null or not space_sky_enabled:
		return

	# Space environment is active when player is at LOD 2+ (far from planet).
	var should_be_space := _current_lod_level >= LOD_PROXY_SPHERE

	if should_be_space == _is_space_env_active:
		return

	_is_space_env_active = should_be_space

	if should_be_space:
		_activate_space_environment()
	else:
		_activate_surface_environment()


func _activate_space_environment() -> void:
	# Save the current surface environment state for restoration.
	_surface_sky = _env.sky
	_surface_bg_mode = _env.background_mode
	_surface_ambient_color = _env.ambient_light_color
	_surface_ambient_energy = _env.ambient_light_energy

	# Create a new Sky resource with the space shader material.
	var space_sky := Sky.new()
	space_sky.sky_material = _space_sky_material
	_env.sky = space_sky
	_env.background_mode = Environment.BG_SKY

	# Disable fog in space.
	_env.fog_enabled = false

	# Reduce ambient light in space — no atmosphere scattering.
	_env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	_env.ambient_light_color = Color(0.02, 0.02, 0.04, 1.0)
	_env.ambient_light_energy = 0.15


func _activate_surface_environment() -> void:
	# Restore the original surface sky.
	if _surface_sky != null:
		_env.sky = _surface_sky
		_surface_sky = null
	else:
		_env.sky = null

	_env.background_mode = _surface_bg_mode

	# Re-enable fog for surface view.
	if horizon_fog_enabled:
		_env.fog_enabled = true

	# Restore ambient light to surface levels.
	_env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	_env.ambient_light_color = _surface_ambient_color
	_env.ambient_light_energy = _surface_ambient_energy


# --- Seed helpers ---

# Convert the world seed to a deterministic float for noise offset.
# Uses a simple hash to spread different seeds across the noise space.
func _world_seed_float() -> float:
	var s := world_seed if world_seed != 0 else 12345
	return fmod(absf(sin(s * 12.9898 + 78.233) * 43758.5453), 1.0)


# --- Debug ---

func _maybe_log_debug(delta: float) -> void:
	_debug_elapsed += delta
	if _debug_elapsed < debug_info_interval:
		return
	_debug_elapsed = 0.0

	var player_pos := _get_player_position()
	var effective_center := get_effective_center()
	var dist := player_pos.distance_to(effective_center)
	var surface_dist := get_surface_distance()
	print("PlanetLodManager: lod=%d fade=%.2f dist=%.1f surface_dist=%.1f active=%s" % [
		_current_lod_level, _fade_alpha, dist, surface_dist, is_active_planet])
