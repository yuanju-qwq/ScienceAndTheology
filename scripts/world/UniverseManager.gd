# UniverseManager — manages all planets in the universe.
# Supports two generation modes: "solar_system" (preset) and
# "random" (procedural). Provides APIs for gravity computation,
# planet loading/unloading, and active planet tracking.
#
# Each planet is represented by a PlanetDescriptor and has its own
# PlanetLodManager for LOD rendering. The ChunkRendererBridge
# switches its active dimension based on which planet the player
# is currently near.
class_name UniverseManager
extends Node3D

const BuiltinTerrainContentScript := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")

signal active_planet_changed(planet: PlanetDescriptor)
signal planet_loaded(planet: PlanetDescriptor)
signal planet_unloaded(planet: PlanetDescriptor)

# Universe generation mode: "solar_system" or "random".
@export var universe_mode: String = "solar_system"

# Universe seed for deterministic generation.
@export var universe_seed: int = 0

# Distance threshold at which a planet's voxel chunks are loaded.
# When the player is closer than this to a planet, chunks start loading.
@export var load_distance_multiplier := 4.0

# Distance threshold at which a planet's voxel chunks are unloaded.
# When the player is farther than this, chunks are removed from memory.
# Should be larger than load_distance to avoid flickering at boundaries.
@export var unload_distance_multiplier := 6.0

# Path to the player node for distance calculations.
@export var player_node_path: NodePath = ^"../Player"

# Path to the ChunkRendererBridge for dimension switching.
@export var chunk_renderer_bridge_path: NodePath = ^"../ChunkRendererBridge"

# Whether to show debug information about planet states.
@export var debug_universe := false
@export var debug_interval := 1.0

# All planets in the universe (including the star).
var planets: Array[PlanetDescriptor] = []

# The planet the player is currently on or nearest to.
var active_planet: PlanetDescriptor = null

# Planets whose voxel chunks are currently loaded in memory.
var _loaded_planets: Dictionary = {}

# PlanetLodManager instances keyed by dimension_id.
var _lod_managers: Dictionary = {}

var _player: Node3D = null
var _chunk_bridge: ChunkRendererBridge = null
var _debug_elapsed := 0.0
var _bridge_initialized := false


func _ready() -> void:
	_apply_game_session_overrides()
	_generate_universe()
	_find_references()
	_create_lod_managers()
	_set_initial_active_planet()


# Override exported values from GameSession when entering from the main menu.
func _apply_game_session_overrides() -> void:
	if GameSession.universe_mode != "":
		universe_mode = GameSession.universe_mode
	if GameSession.universe_seed != 0:
		universe_seed = GameSession.universe_seed


func _process(delta: float) -> void:
	if _player == null:
		return

	_update_active_planet()
	_update_planet_loading()
	_maybe_debug_log(delta)


# --- Universe generation ---

func _generate_universe() -> void:
	if universe_seed == 0:
		universe_seed = randi()

	match universe_mode:
		"solar_system":
			planets = SolarSystemPreset.generate(universe_seed)
		"random":
			planets = RandomUniverseGenerator.generate(universe_seed)
		_:
			push_warning("UniverseManager: unknown mode '%s', falling back to solar_system" % universe_mode)
			planets = SolarSystemPreset.generate(universe_seed)


func _find_references() -> void:
	_player = get_node_or_null(player_node_path) as Node3D
	_chunk_bridge = get_node_or_null(chunk_renderer_bridge_path) as ChunkRendererBridge


# --- LOD manager creation ---

func _create_lod_managers() -> void:
	for planet in planets:
		if planet.is_star:
			_create_star_lod(planet)
		else:
			_create_planet_lod(planet)


func _create_planet_lod(planet: PlanetDescriptor) -> void:
	var lod := PlanetLodManager.new()
	lod.name = "LOD_%s" % String(planet.dimension_id)
	lod.planet_center = planet.universe_position
	lod.planet_radius = planet.planet_radius
	lod.world_seed = planet.seed
	lod.atmosphere_color = planet.atmosphere_color
	lod.atmosphere_scale = planet.atmosphere_scale
	lod.atmosphere_power = planet.atmosphere_power
	lod.atmosphere_intensity = planet.atmosphere_intensity
	lod.cloud_scale = planet.cloud_scale
	lod.cloud_coverage = planet.cloud_coverage
	lod.cloud_sharpness = planet.cloud_sharpness
	lod.cloud_color = planet.cloud_color
	lod.cloud_rotation_speed = planet.cloud_rotation_speed
	lod.horizon_fog_color = planet.horizon_fog_color
	lod.horizon_fog_max_density = planet.horizon_fog_max_density
	lod.horizon_fog_max_distance = planet.horizon_fog_max_distance
	lod.player_node_path = ^"../../Player"
	lod.world_environment_path = ^"../../WorldEnvironment"
	add_child(lod)
	_lod_managers[planet.dimension_id] = lod


func _create_star_lod(planet: PlanetDescriptor) -> void:
	var lod := PlanetLodManager.new()
	lod.name = "LOD_%s" % String(planet.dimension_id)
	lod.planet_center = planet.universe_position
	lod.planet_radius = planet.planet_radius
	lod.world_seed = planet.seed
	lod.atmosphere_color = planet.atmosphere_color
	lod.atmosphere_scale = planet.atmosphere_scale
	lod.atmosphere_power = planet.atmosphere_power
	lod.atmosphere_intensity = planet.atmosphere_intensity
	lod.cloud_scale = 1.0
	lod.cloud_coverage = 0.0
	lod.horizon_fog_max_density = 0.0
	lod.player_node_path = ^"../../Player"
	lod.world_environment_path = ^"../../WorldEnvironment"
	add_child(lod)
	_lod_managers[planet.dimension_id] = lod


# --- Active planet tracking ---

func _set_initial_active_planet() -> void:
	if _player == null:
		if planets.size() > 0:
			active_planet = _find_first_landable_planet()
		return

	var nearest := find_nearest_planet(_player.global_position)
	if nearest != null:
		_set_active_planet(nearest)


func _update_active_planet() -> void:
	if _player == null:
		return

	var nearest := find_nearest_planet(_player.global_position)
	if nearest == null:
		return

	if active_planet == null or nearest.dimension_id != active_planet.dimension_id:
		_set_active_planet(nearest)


func _set_active_planet(planet: PlanetDescriptor) -> void:
	var old := active_planet
	active_planet = planet
	active_planet_changed.emit(planet)

	if _chunk_bridge != null and not planet.is_star:
		if not _bridge_initialized:
			var config := BuiltinTerrainContentScript.create_config_for_universe(planets)
			_chunk_bridge.initialize_for_universe(config, planet.dimension_id)
			_bridge_initialized = true
		else:
			_chunk_bridge.set_active_dimension(planet.dimension_id)


# --- Planet loading / unloading ---

func _update_planet_loading() -> void:
	if _player == null:
		return

	var player_pos := _player.global_position

	for planet in planets:
		if planet.is_star:
			continue

		var dist := player_pos.distance_to(planet.universe_position)
		var load_dist := planet.planet_radius * load_distance_multiplier
		var unload_dist := planet.planet_radius * unload_distance_multiplier
		var is_loaded := _loaded_planets.has(planet.dimension_id)

		if not is_loaded and dist <= load_dist:
			_load_planet(planet)
		elif is_loaded and dist > unload_dist:
			_unload_planet(planet)


func _load_planet(planet: PlanetDescriptor) -> void:
	_loaded_planets[planet.dimension_id] = true
	planet_loaded.emit(planet)


func _unload_planet(planet: PlanetDescriptor) -> void:
	_loaded_planets.erase(planet.dimension_id)
	planet_unloaded.emit(planet)


# --- Public API ---

# Find the nearest planet to a given universe-space position.
# Stars are excluded from consideration.
func find_nearest_planet(position: Vector3) -> PlanetDescriptor:
	var best: PlanetDescriptor = null
	var best_dist: float = INF

	for planet in planets:
		if planet.is_star:
			continue
		var dist := position.distance_to(planet.universe_position)
		if dist < best_dist:
			best_dist = dist
			best = planet

	return best


# Compute the combined gravity direction at a given universe-space position.
# Iterates all landable planets, picks the one with the strongest gravity
# influence (closest / highest gravity_multiplier), and returns its
# gravity direction. Returns Vector3.ZERO if in deep space (no gravity range).
func compute_gravity_direction(position: Vector3) -> Vector3:
	var best_dir := Vector3.ZERO
	var best_influence := 0.0

	for planet in planets:
		if planet.is_star:
			continue
		if not planet.is_in_gravity_range(position):
			continue

		var dist := position.distance_to(planet.universe_position)
		var dir := planet.gravity_direction_at(position)
		if dir == Vector3.ZERO:
			continue

		# Gravity influence: stronger when closer and higher multiplier.
		# Uses inverse-square falloff within the gravity radius.
		var gravity_radius := planet.gravity_radius()
		var t := 1.0 - (dist / gravity_radius)
		var influence := planet.gravity_multiplier * t * t

		if influence > best_influence:
			best_influence = influence
			best_dir = dir

	return best_dir


# Compute the gravity strength multiplier at a given position.
# Returns 0.0 if in deep space.
func compute_gravity_multiplier(position: Vector3) -> float:
	var best_mult := 0.0

	for planet in planets:
		if planet.is_star:
			continue
		if not planet.is_in_gravity_range(position):
			continue

		var dist := position.distance_to(planet.universe_position)
		var gravity_radius := planet.gravity_radius()
		var t := 1.0 - (dist / gravity_radius)
		var influence := planet.gravity_multiplier * t * t

		if influence > best_mult:
			best_mult = influence

	return best_mult


# Check whether a position is within any planet's gravity range.
func is_in_any_gravity_range(position: Vector3) -> bool:
	for planet in planets:
		if planet.is_star:
			continue
		if planet.is_in_gravity_range(position):
			return true
	return false


# Get the PlanetDescriptor for a specific dimension_id.
func get_planet_by_dimension(dimension_id: StringName) -> PlanetDescriptor:
	for planet in planets:
		if planet.dimension_id == dimension_id:
			return planet
	return null


# Get the PlanetLodManager for a specific dimension_id.
func get_lod_manager(dimension_id: StringName) -> PlanetLodManager:
	return _lod_managers.get(dimension_id)


# Check whether a planet's voxel chunks are currently loaded.
func is_planet_loaded(dimension_id: StringName) -> bool:
	return _loaded_planets.has(dimension_id)


# Get all landable (non-star) planets.
func get_landable_planets() -> Array[PlanetDescriptor]:
	var result: Array[PlanetDescriptor] = []
	for planet in planets:
		if not planet.is_star:
			result.append(planet)
	return result


# --- Debug ---

func _maybe_debug_log(delta: float) -> void:
	if not debug_universe:
		return
	_debug_elapsed += delta
	if _debug_elapsed < debug_interval:
		return
	_debug_elapsed = 0.0

	var player_pos := _player.global_position if _player else Vector3.ZERO
	var active_name := active_planet.display_name if active_planet else "None"
	var loaded_count := _loaded_planets.size()
	print("UniverseManager: mode=%s seed=%d planets=%d active=%s loaded=%d pos=%s" % [
		universe_mode, universe_seed, planets.size(), active_name, loaded_count,
		str(player_pos)])


# --- Internal helpers ---

func _find_first_landable_planet() -> PlanetDescriptor:
	for planet in planets:
		if not planet.is_star:
			return planet
	return null
