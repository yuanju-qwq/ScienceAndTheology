# UniverseManager — manages all star systems and planets in the universe.
# Supports two generation modes: "solar_system" (preset) and
# "random" (procedural with on-demand generation and realization).
#
# In "random" mode, the universe is effectively infinite. A SpatialUniverseGrid
# divides space into cells and deterministically assigns star systems to cells
# based on the universe seed. System placeholders are generated on-demand as
# the player explores and unloaded when far away (unless realized). This
# enables a virtually infinite universe without upfront generation cost.
#
# Performance strategy for a factory game:
#   - Only the active planet's chunks are loaded in memory and rendered.
#   - When the player leaves a planet, its chunks are serialized to disk
#     and unloaded from memory. A PlanetSummary captures production rates.
#   - VirtualPlanetSimulator advances time for unloaded planets using
#     their summaries, approximating factory output without full chunk data.
#   - When the player returns, chunks are loaded from disk and the
#     accumulated virtual production is reconciled into the game state.
#
# Save layout (each planet stored independently):
#   {save_dir}/
#     universe_header.bin
#     universe_meta.json
#     systems/
#       {system_id}/
#         system_meta.json
#         planets/
#           {dimension_id}/
#             planet_data.bin
#             regions/
class_name UniverseManager
extends Node3D

const BuiltinTerrainContentScript := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")

signal active_planet_changed(planet: PlanetDescriptor)
signal planet_loaded(planet: PlanetDescriptor)
signal planet_unloaded(planet: PlanetDescriptor)
signal system_realized(system: StarSystemDescriptor)

# Universe generation mode: "solar_system" or "random".
@export var universe_mode: String = "solar_system"

# Universe seed for deterministic generation.
@export var universe_seed: int = 0

# Distance multiplier at which a planet's chunks are loaded.
@export var load_distance_multiplier := 4.0

# Distance multiplier at which a planet's chunks are serialized and unloaded.
@export var unload_distance_multiplier := 6.0

# Distance at which a placeholder system gets realized.
@export var realize_distance := 50000.0

# Radius around the player to generate system placeholders (random mode).
# Systems beyond this distance are unloaded if they are still placeholders.
@export var system_stream_radius := 200000.0

# Density of star systems in the universe grid (random mode).
# Fraction of grid cells that contain a star system (0.05 - 0.8).
@export var system_density: float = SpatialUniverseGrid.DEFAULT_DENSITY

# Path to the player node for distance calculations.
@export var player_node_path: NodePath = ^"../Player"

# Path to the ChunkRendererBridge for dimension switching.
@export var chunk_renderer_bridge_path: NodePath = ^"../ChunkRendererBridge"

# Path to the GDTickSystem for simulation tick driving.
@export var tick_system_path: NodePath = ^"../GDTickSystem"

# Whether to show debug information about planet states.
@export var debug_universe := false
@export var debug_interval := 1.0

# All star systems currently in scope (random mode: dynamically managed).
var systems: Array[StarSystemDescriptor] = []

# Spatial grid for deterministic infinite universe generation (random mode).
var _grid: SpatialUniverseGrid = null

# Convenience: flat list of all planets across all realized systems.
# Stars are included. Placeholder systems contribute no entries.
var all_planets: Array[PlanetDescriptor]:
	get:
		var result: Array[PlanetDescriptor] = []
		for sys in systems:
			if sys.is_realized():
				result.append_array(sys.all_bodies())
		return result

# Backward compatibility alias: `planets` now delegates to `all_planets`.
var planets: Array[PlanetDescriptor]:
	get: return all_planets

# The planet the player is currently on or nearest to.
var active_planet: PlanetDescriptor = null

# Planets whose voxel chunks are currently loaded in memory.
var _loaded_planets: Dictionary = {}

# PlanetLodManager instances keyed by dimension_id.
var _lod_managers: Dictionary = {}

# Virtual planet simulator for distant unloaded planets.
var _virtual_sim: VirtualPlanetSimulator = null

# Universe save manager for per-planet serialization and universe metadata.
var _save_manager: UniverseSaveManager = null

# Save directory for per-planet chunk serialization.
var _save_dir: String = ""

var _player: Node3D = null
var _chunk_bridge: ChunkRendererBridge = null
var _tick_system: GDTickSystem = null
var _tick_system_initialized := false
var _debug_elapsed := 0.0
var _bridge_initialized := false


func _ready() -> void:
	_apply_game_session_overrides()
	_generate_universe()
	_realize_initial_system()
	_find_references()
	_create_virtual_simulator()
	_create_save_manager()
	_create_lod_managers_for_realized()
	_set_initial_active_planet()


func _process(delta: float) -> void:
	if _player == null:
		return

	_update_system_streaming()
	_update_system_distances()
	_update_system_realization()
	_update_active_planet()
	_update_planet_loading()
	_drive_tick_system(delta)
	_maybe_debug_log(delta)


# --- Game session overrides ---

func _apply_game_session_overrides() -> void:
	if GameSession.universe_mode != "":
		universe_mode = GameSession.universe_mode
	if GameSession.universe_seed != 0:
		universe_seed = GameSession.universe_seed
	if GameSession.system_density > 0.0:
		system_density = GameSession.system_density


# --- Universe generation ---

func _generate_universe() -> void:
	if universe_seed == 0:
		universe_seed = randi()

	match universe_mode:
		"solar_system":
			var sys := SolarSystemPreset.create_placeholder(universe_seed)
			systems.append(sys)
		"random":
			_grid = RandomUniverseGenerator.create_grid(universe_seed, system_density)
			var sys_array := RandomUniverseGenerator.generate_around(
				_grid, Vector3.ZERO, system_stream_radius)
			systems = sys_array
		_:
			push_warning("UniverseManager: unknown mode '%s', falling back to solar_system" % universe_mode)
			var sys := SolarSystemPreset.create_placeholder(universe_seed)
			systems.append(sys)


# --- Initial system realization ---

# Realize the system closest to the origin so the player has a starting
# system with full detail immediately on game start.
func _realize_initial_system() -> void:
	var best: StarSystemDescriptor = null
	var best_dist: float = INF
	for sys in systems:
		var dist := sys.universe_position.length()
		if dist < best_dist:
			best_dist = dist
			best = sys
	if best == null:
		return
	if best.is_realized():
		return
	if best.system_id == &"sys_sol":
		SolarSystemPreset.realize(best)
	else:
		StarSystemGenerator.realize(best)


func _find_references() -> void:
	_player = get_node_or_null(player_node_path) as Node3D
	_chunk_bridge = get_node_or_null(chunk_renderer_bridge_path) as ChunkRendererBridge
	_tick_system = get_node_or_null(tick_system_path) as GDTickSystem


# --- Virtual simulator ---

func _create_virtual_simulator() -> void:
	_virtual_sim = VirtualPlanetSimulator.new()
	_virtual_sim.name = "VirtualPlanetSimulator"
	add_child(_virtual_sim)


# --- Save manager ---

func _create_save_manager() -> void:
	_save_manager = UniverseSaveManager.new()
	_save_manager.name = "UniverseSaveManager"
	add_child(_save_manager)


# --- System streaming (infinite universe) ---

# Dynamically add and remove system placeholders based on player position.
# New placeholders are generated via SpatialUniverseGrid for cells within
# system_stream_radius. Distant placeholders (not yet realized) are removed.
# Realized systems are never removed because the player may have built on them.
func _update_system_streaming() -> void:
	if _grid == null or _player == null:
		return

	var player_pos := _player.global_position

	# Generate new placeholders around the player.
	var cells := _grid.get_cells_around(player_pos, system_stream_radius)
	var existing_ids: Dictionary = {}
	for sys in systems:
		existing_ids[sys.system_id] = true

	for cell in cells:
		var cell_id := StringName(&"sys_%d_%d" % [cell.x, cell.y])
		if existing_ids.has(cell_id):
			continue
		var sys := _grid.create_placeholder_for_cell(cell)
		systems.append(sys)
		existing_ids[sys.system_id] = true

	# Remove distant placeholders that have not been realized.
	var to_remove: Array[int] = []
	for i in range(systems.size()):
		var sys := systems[i]
		if sys.is_realized():
			continue
		var dist := player_pos.distance_to(sys.universe_position)
		if dist > system_stream_radius * 1.2:
			to_remove.append(i)

	if not to_remove.is_empty():
		to_remove.reverse()
		for idx in to_remove:
			systems.remove_at(idx)


# --- System distance and realization ---

# Update the distance from each system to the player.
func _update_system_distances() -> void:
	if _player == null:
		return
	var player_pos := _player.global_position
	for sys in systems:
		sys.distance_to_player = player_pos.distance_to(sys.universe_position)


# Realize placeholder systems that are close enough to the player.
# Dispatches to the correct realize function based on system_id:
#   "sys_sol" → SolarSystemPreset.realize()
#   all others → StarSystemGenerator.realize()
func _update_system_realization() -> void:
	for sys in systems:
		if sys.is_realized():
			continue
		if sys.distance_to_player > realize_distance:
			continue
		if sys.system_id == &"sys_sol":
			SolarSystemPreset.realize(sys)
		else:
			StarSystemGenerator.realize(sys)
		_create_lod_managers_for_system(sys)
		system_realized.emit(sys)


# --- LOD manager creation ---

# Create LOD managers for all currently realized systems.
func _create_lod_managers_for_realized() -> void:
	for sys in systems:
		if sys.is_realized():
			_create_lod_managers_for_system(sys)


# Create LOD managers for a single realized system.
func _create_lod_managers_for_system(system: StarSystemDescriptor) -> void:
	for star in system.stars:
		_create_star_lod(star)
	for planet in system.planets:
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
			var config := BuiltinTerrainContentScript.create_config_for_universe(all_planets)
			_chunk_bridge.initialize_for_universe(config, planet.dimension_id)
			_bridge_initialized = true
			_initialize_tick_system()
		else:
			_chunk_bridge.set_active_dimension(planet.dimension_id)


# --- Planet loading / unloading with serialization ---

func _update_planet_loading() -> void:
	if _player == null:
		return

	var player_pos := _player.global_position

	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
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
	var dim := planet.dimension_id
	_loaded_planets[dim] = true

	# If we have a virtual simulation summary, reconcile it before loading chunks.
	if _virtual_sim != null and _virtual_sim.is_simulating(dim):
		_reconcile_virtual_production(dim)
		_virtual_sim.unregister_planet(dim)

	# Load chunk data from disk via the save manager.
	if _save_manager != null and _save_dir != "":
		_save_manager.load_planet(_save_dir, dim)

	planet_loaded.emit(planet)


func _unload_planet(planet: PlanetDescriptor) -> void:
	var dim := planet.dimension_id

	# Capture a production summary before unloading.
	var summary := PlanetSummary.create_from_world(dim, 0)

	# Serialize chunk data to disk and unload from memory via the save manager.
	if _save_manager != null and _save_dir != "":
		_save_manager.save_planet(_save_dir, dim)

	# Unload chunks from memory.
	if _chunk_bridge != null:
		var world_data := _chunk_bridge.get_world_data()
		if world_data != null:
			world_data.unload_dimension(String(dim))

	# Register the planet for virtual simulation.
	if _virtual_sim != null and summary.has_production():
		_virtual_sim.register_planet(summary)

	_loaded_planets.erase(dim)
	planet_unloaded.emit(planet)


# --- Virtual production reconciliation ---

# When a player returns to a planet, inject the accumulated virtual
# production into the actual game state. This is the "reconciliation"
# step that makes distant factories feel like they kept running.
func _reconcile_virtual_production(dimension_id: StringName) -> void:
	if _virtual_sim == null:
		return

	var summary := _virtual_sim.get_summary(dimension_id)
	if summary == null:
		return

	var accumulated := summary.get_all_accumulated_production()
	if accumulated.is_empty():
		summary.clear_accumulated()
		return

	# TODO: Inject accumulated items into the planet's storage/logistics.
	# This requires scanning the loaded chunk data for storage containers
	# and logistics towers, then adding items to their inventories.
	# For now, log the reconciliation for debugging.
	if debug_universe:
		for item_key in accumulated.keys():
			var count: int = accumulated[item_key]
			print("UniverseManager: reconcile %d x %s for %s" % [
				count, item_key, String(dimension_id)])

	summary.clear_accumulated()


# --- Save / load universe ---

# Set the save directory for per-planet chunk serialization.
# Must be called before any planet is unloaded.
func set_save_dir(path: String) -> void:
	_save_dir = path


# Get the current save directory.
func get_save_dir() -> String:
	return _save_dir


# Save the entire universe (all loaded planets + metadata + summaries).
func save_universe() -> bool:
	if _save_manager == null or _save_dir == "":
		push_warning("UniverseManager: save_dir not set, cannot save")
		return false

	# Ensure the save manager has the right references.
	var world_data := _chunk_bridge.get_world_data() if _chunk_bridge else null
	_save_manager.setup(self, world_data)

	return _save_manager.save_universe(_save_dir)


# Load the universe (header + metadata + summaries).
# Planet chunks are loaded on-demand when the player approaches.
func load_universe() -> bool:
	if _save_manager == null or _save_dir == "":
		push_warning("UniverseManager: save_dir not set, cannot load")
		return false

	var world_data := _chunk_bridge.get_world_data() if _chunk_bridge else null
	_save_manager.setup(self, world_data)

	return _save_manager.load_universe(_save_dir)


# --- Public API: system queries ---

# Find the nearest star system to a given universe-space position.
func find_nearest_system(position: Vector3) -> StarSystemDescriptor:
	var best: StarSystemDescriptor = null
	var best_dist: float = INF
	for sys in systems:
		var dist := position.distance_to(sys.universe_position)
		if dist < best_dist:
			best_dist = dist
			best = sys
	return best


# Get a StarSystemDescriptor by its system_id.
func get_system_by_id(system_id: StringName) -> StarSystemDescriptor:
	for sys in systems:
		if sys.system_id == system_id:
			return sys
	return null


# Get the StarSystemDescriptor that contains a given planet dimension.
func get_system_for_planet(dimension_id: StringName) -> StarSystemDescriptor:
	for sys in systems:
		if not sys.is_realized():
			continue
		for body in sys.all_bodies():
			if body.dimension_id == dimension_id:
				return sys
	return null


# Get the number of realized systems.
func get_realized_system_count() -> int:
	var count := 0
	for sys in systems:
		if sys.is_realized():
			count += 1
	return count


# Get the total number of systems (including placeholders).
func get_system_count() -> int:
	return systems.size()


# --- Public API: planet queries ---

# Find the nearest planet to a given universe-space position.
# Stars are excluded from consideration.
# Only considers planets in realized systems.
func find_nearest_planet(position: Vector3) -> PlanetDescriptor:
	var best: PlanetDescriptor = null
	var best_dist: float = INF

	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			var dist := position.distance_to(planet.universe_position)
			if dist < best_dist:
				best_dist = dist
				best = planet

	return best


# Compute the combined gravity direction at a given universe-space position.
# Considers gravity from both planets and stars within the nearest system.
# Stars exert gravity but are not landable.
func compute_gravity_direction(position: Vector3) -> Vector3:
	var nearest_sys := find_nearest_system(position)
	if nearest_sys == null or not nearest_sys.is_realized():
		return Vector3.ZERO

	var best_dir := Vector3.ZERO
	var best_influence := 0.0

	for body in nearest_sys.all_bodies():
		var dist := position.distance_to(body.universe_position)
		var gravity_radius := body.gravity_radius()
		if dist > gravity_radius or dist < 0.001:
			continue

		var dir := (body.universe_position - position).normalized()
		var t := 1.0 - (dist / gravity_radius)
		var influence := body.gravity_multiplier * t * t

		if influence > best_influence:
			best_influence = influence
			best_dir = dir

	return best_dir


# Compute the gravity strength multiplier at a given position.
func compute_gravity_multiplier(position: Vector3) -> float:
	var nearest_sys := find_nearest_system(position)
	if nearest_sys == null or not nearest_sys.is_realized():
		return 0.0

	var best_mult := 0.0

	for body in nearest_sys.all_bodies():
		var dist := position.distance_to(body.universe_position)
		var gravity_radius := body.gravity_radius()
		if dist > gravity_radius:
			continue

		var t := 1.0 - (dist / gravity_radius)
		var influence := body.gravity_multiplier * t * t

		if influence > best_mult:
			best_mult = influence

	return best_mult


# Check whether a position is within any planet's gravity range.
func is_in_any_gravity_range(position: Vector3) -> bool:
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_in_gravity_range(position):
				return true
	return false


# Get the PlanetDescriptor for a specific dimension_id.
func get_planet_by_dimension(dimension_id: StringName) -> PlanetDescriptor:
	for sys in systems:
		if not sys.is_realized():
			continue
		for body in sys.all_bodies():
			if body.dimension_id == dimension_id:
				return body
	return null


# Get the PlanetLodManager for a specific dimension_id.
func get_lod_manager(dimension_id: StringName) -> PlanetLodManager:
	return _lod_managers.get(dimension_id)


# Check whether a planet's voxel chunks are currently loaded.
func is_planet_loaded(dimension_id: StringName) -> bool:
	return _loaded_planets.has(dimension_id)


# Check whether a planet is currently being virtually simulated.
func is_planet_simulating(dimension_id: StringName) -> bool:
	return _virtual_sim != null and _virtual_sim.is_simulating(dimension_id)


# Get the virtual simulation time for a planet.
func get_planet_virtual_time(dimension_id: StringName) -> float:
	if _virtual_sim == null:
		return 0.0
	return _virtual_sim.get_virtual_time(dimension_id)


# Get all landable (non-star) planets across all realized systems.
func get_landable_planets() -> Array[PlanetDescriptor]:
	var result: Array[PlanetDescriptor] = []
	for sys in systems:
		if sys.is_realized():
			result.append_array(sys.get_landable_planets())
	return result


# Get the virtual planet simulator (for external access).
func get_virtual_simulator() -> VirtualPlanetSimulator:
	return _virtual_sim


# Get the universe save manager (for external access).
func get_save_manager() -> UniverseSaveManager:
	return _save_manager


# --- Tick system integration ---

# Initialize the GDTickSystem with the world data and register all subsystems.
# Called once after ChunkRendererBridge is initialized and GDWorldData is available.
func _initialize_tick_system() -> void:
	if _tick_system == null or _tick_system_initialized:
		return

	var world_data: GDWorldData = _chunk_bridge.get_world_data() if _chunk_bridge else null
	if world_data == null:
		push_warning("UniverseManager: cannot initialize tick system — no world data")
		return

	_tick_system.set_world_data(world_data)

	# Register subsystems in priority order.
	# DayNight must be first (priority 0) so other systems can read is_daytime.
	_tick_system.register_day_night_system()
	_tick_system.register_block_physics_system()
	_tick_system.register_machine_system()
	_tick_system.register_region_system()
	_tick_system.register_tree_growth_system()
	_tick_system.register_season_system()

	_tick_system_initialized = true


# Drive the simulation tick each frame.
# Updates the player chunk position and advances the simulation.
func _drive_tick_system(delta: float) -> void:
	if _tick_system == null or not _tick_system_initialized:
		return

	# Compute the player's chunk coordinates from world position.
	var dimension := String(active_planet.dimension_id) if active_planet else "overworld"
	var player_chunk := _compute_player_chunk()

	_tick_system.set_player_chunk(dimension, player_chunk.x, player_chunk.y, player_chunk.z)
	_tick_system.tick(delta)


# Compute the player's current chunk coordinates.
func _compute_player_chunk() -> Vector3i:
	if _player == null:
		return Vector3i.ZERO
	return GDChunkHelper.world_position_to_chunk(_player.global_position, 32)


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
	var sim_count := _virtual_sim.get_simulated_dimensions().size() if _virtual_sim else 0
	var realized_count := get_realized_system_count()
	print("UniverseManager: mode=%s seed=%d systems=%d realized=%d planets=%d active=%s loaded=%d simulating=%d pos=%s" % [
		universe_mode, universe_seed, systems.size(), realized_count,
		all_planets.size(), active_name, loaded_count, sim_count, str(player_pos)])


# --- Internal helpers ---

func _find_first_landable_planet() -> PlanetDescriptor:
	for sys in systems:
		if sys.is_realized():
			var landable := sys.get_landable_planets()
			if not landable.is_empty():
				return landable[0]
	return null
