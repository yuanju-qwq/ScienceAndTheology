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
const CHUNK_SIZE := 32
const SPAWN_LOCAL_COORD := CHUNK_SIZE * 0.5 + 0.5

signal active_planet_changed(planet: PlanetDescriptor)
signal planet_loaded(planet: PlanetDescriptor)
signal planet_unloaded(planet: PlanetDescriptor)
signal system_realized(system: StarSystemDescriptor)
signal station_created(station: StationDescriptor)
signal station_loaded(station: StationDescriptor)
signal station_unloaded(station: StationDescriptor)

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

# Path to the GDQuestSystem for quest tracking.
@export var quest_system_path: NodePath = ^"../GDQuestSystem"

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

# --- Space station management ---

# All player-built space stations.
var stations: Array[StationDescriptor] = []

# Auto-incrementing counter for station dimension IDs.
var station_counter: int = 0

# The station the player is currently inside (null if on a planet).
var active_station: StationDescriptor = null

# Planets whose voxel chunks are currently loaded in memory.
var _loaded_planets: Dictionary = {}

# Stations whose voxel chunks are currently loaded in memory.
var _loaded_stations: Dictionary = {}

# PlanetLodManager instances keyed by dimension_id.
var _lod_managers: Dictionary = {}

# Virtual planet simulator for distant unloaded planets.
var _virtual_sim: VirtualPlanetSimulator = null

# Universe save manager for per-planet serialization and universe metadata.
var _save_manager: UniverseSaveManager = null

# Save directory for per-planet chunk serialization.
var _save_dir: String = ""

# Player game mode persisted across saves (0=SURVIVAL, 1=CREATIVE, 2=OBSERVER).
var player_game_mode: int = 0

# Player health persisted across saves.
var player_health: float = 100.0

# Player source law serialized as Dictionary (drives health_max, health_regen, etc.).
var player_source_law_dict: Dictionary = {}

# Player satiation serialized as Dictionary.
var player_satiation_dict: Dictionary = {}

var _player: Node3D = null
var _chunk_bridge: ChunkRendererBridge = null
var tick_system: GDTickSystem = null
var tick_system_initialized := false
var quest_system: GDQuestSystem = null
var quest_system_initialized := false
var _debug_elapsed := 0.0
var _bridge_initialized := false

# GameplayConfig overrides from GameSession, applied when world data is available.
var _session_gameplay_config: Dictionary = {}

# Planet generation overrides from GameSession (random mode only).
var _planet_overrides: Dictionary = {}

# Radius scale factor for newly realized planets (random mode size override).
var _planet_radius_scale: float = 1.0

# Default console permission level from world creation (0=PLAYER, 1=CHEATER, 2=OP).
var _permission_level: int = 1

# Expose permission level for PlayerController to read.
func get_permission_level() -> int:
	return _permission_level

# 浮动原点：跟踪玩家的 double 精度宇宙坐标。
# 解决多星球大尺度距离下的 float 精度问题。
var floating_origin: FloatingOrigin = null

# 旅行进行中标志：为 true 时跳过 _update_active_planet 的自动切换，
# 避免 travel_to_planet 设置活跃星球后被同一帧的逻辑覆盖。
var _is_traveling := false

# --- Per-process task scheduling buckets -------------------------------------
# _process runs 10 subsystem updates every frame by default. Most of them are
# IO-bound or only affect LOD/streaming that the player cannot perceive at
# 60 FPS. Tasks are split into three buckets so heavy IO runs at lower rate:
#
#   every_frame : player universe position + tick driver (gameplay critical)
#   medium      : 0.2s — distance/LOD/active-planet/loading updates
#   slow        : 0.5s — system streaming + realization (heaviest IO)
#
# Accumulators are independent so each bucket can be tuned without touching
# the others. Set interval to 0.0 to force a bucket to run every frame.
const MEDIUM_BUCKET_INTERVAL := 0.2
const SLOW_BUCKET_INTERVAL := 0.5
var _medium_bucket_elapsed := 0.0
var _slow_bucket_elapsed := 0.0


func get_station_counter() -> int:
	return station_counter


func set_station_counter(value: int) -> void:
	station_counter = value


func get_quest_system() -> GDQuestSystem:
	return quest_system


func _ready() -> void:
	var total_started_usec := Time.get_ticks_usec()
	var stage_started_usec := total_started_usec
	_apply_game_session_overrides()
	_print_perf("UniverseManager.apply_game_session_overrides", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_generate_universe()
	_print_perf("UniverseManager.generate_universe systems=%d mode=%s" % [
		systems.size(),
		universe_mode,
	], stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_load_player_game_mode()
	_print_perf("UniverseManager.load_player_game_mode", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_realize_initial_system()
	_print_perf("UniverseManager.realize_initial_system realized=%d bodies=%d" % [
		get_realized_system_count(),
		all_planets.size(),
	], stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_apply_planet_overrides()
	_print_perf("UniverseManager.apply_planet_overrides", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_find_references()
	_print_perf("UniverseManager.find_references", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_create_virtual_simulator()
	_print_perf("UniverseManager.create_virtual_simulator", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_create_save_manager()
	_print_perf("UniverseManager.create_save_manager", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_create_lod_managers_for_realized()
	_print_perf("UniverseManager.create_lod_managers_for_realized lod_count=%d" %
			_lod_managers.size(), stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_create_floating_origin()
	_print_perf("UniverseManager.create_floating_origin", stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_set_initial_active_planet()
	var active_name := "null"
	if active_planet != null:
		active_name = active_planet.display_name
	_print_perf("UniverseManager.set_initial_active_planet active=%s" % active_name,
			stage_started_usec)
	_print_perf("UniverseManager._ready total systems=%d realized=%d bodies=%d lod=%d" % [
		systems.size(),
		get_realized_system_count(),
		all_planets.size(),
		_lod_managers.size(),
	], total_started_usec)


func _print_perf(label: String, started_usec: int) -> void:
	print("[Perf] %s elapsed_ms=%.2f" % [
		label,
		float(Time.get_ticks_usec() - started_usec) / 1000.0,
	])


func _process(delta: float) -> void:
	if _player == null:
		return

	# --- every_frame bucket --------------------------------------------------
	# Player universe position and tick driver are gameplay-critical: the
	# first feeds gravity/LOD precision, the second advances simulation.
	_update_player_universe_position()
	_drivetick_system(delta)

	# --- medium bucket (0.2s) ------------------------------------------------
	# Distance / active-planet / visibility / loading updates. These affect
	# LOD transitions and chunk streaming but are not perceptible at 60 FPS
	# when throttled to 5 Hz.
	_medium_bucket_elapsed += delta
	if _medium_bucket_elapsed >= MEDIUM_BUCKET_INTERVAL:
		_medium_bucket_elapsed = 0.0
		_update_system_distances()
		# 旅行进行中时跳过自动活跃星球切换，避免覆盖 travel_to_planet 的设置。
		if not _is_traveling:
			_update_active_planet()
		_update_distant_body_visibility()
		_update_planet_loading()
		_update_station_loading()

	# --- slow bucket (0.5s) --------------------------------------------------
	# System streaming + realization are the heaviest IO operations
	# (placeholder generation, planet realization). 2 Hz is sufficient
	# because the player crosses system-stream boundaries slowly.
	_slow_bucket_elapsed += delta
	if _slow_bucket_elapsed >= SLOW_BUCKET_INTERVAL:
		_slow_bucket_elapsed = 0.0
		_update_system_streaming()
		_update_system_realization()

	_maybe_debug_log(delta)


# 创建 FloatingOrigin 实例，用于跟踪玩家的 double 精度宇宙坐标。
func _create_floating_origin() -> void:
	if floating_origin == null:
		floating_origin = FloatingOrigin.new()
	# 初始化渲染原点到初始活跃星球的宇宙位置（若有）。
	if active_planet != null:
		floating_origin.set_origin_vec3(active_planet.universe_position)
		floating_origin.set_universe_position_vec3(active_planet.universe_position)


# 每帧更新玩家的宇宙坐标。
# 玩家宇宙坐标 = 活跃星球宇宙坐标 + (玩家场景坐标 - 活跃星球 local_center)。
# 这样玩家在星球表面移动时，宇宙坐标同步更新，远景距离计算保持精度。
func _update_player_universe_position() -> void:
	if floating_origin == null or active_planet == null or _player == null:
		return
	var local_offset := _player.global_position - active_planet.local_center
	var ux: float = active_planet.universe_position.x + local_offset.x
	var uy: float = active_planet.universe_position.y + local_offset.y
	var uz: float = active_planet.universe_position.z + local_offset.z
	floating_origin.set_universe_position(ux, uy, uz)


# --- Game session overrides ---

func _apply_game_session_overrides() -> void:
	var game_session := get_node_or_null(^"/root/GameSession")
	if game_session != null:
		var session_mode := str(game_session.get("universe_mode"))
		var session_seed := int(str(game_session.get("universe_seed")).to_int())
		var session_density := float(str(game_session.get("system_density")).to_float())
		var session_save_path := str(game_session.get("save_path"))
		var session_game_mode := int(str(game_session.get("game_mode")).to_int())
		var session_gameplay_config: Dictionary = {}
		var raw_gameplay_config: Variant = game_session.get("gameplay_config")
		if raw_gameplay_config is Dictionary:
			session_gameplay_config = raw_gameplay_config
		var session_permission := int(str(game_session.get("permission_level")).to_int())
		var session_planet_overrides: Dictionary = {}
		var raw_planet_overrides: Variant = game_session.get("planet_overrides")
		if raw_planet_overrides is Dictionary:
			session_planet_overrides = raw_planet_overrides
		if session_mode != "":
			universe_mode = session_mode
		if session_seed != 0:
			universe_seed = session_seed
		if session_density > 0.0:
			system_density = session_density
		if session_save_path != "":
			_save_dir = ProjectSettings.globalize_path(session_save_path)
		if session_game_mode >= 0 and session_game_mode <= 2:
			player_game_mode = session_game_mode
		if session_permission >= 0 and session_permission <= 2:
			_permission_level = session_permission
		if not session_gameplay_config.is_empty():
			_session_gameplay_config = session_gameplay_config
		if not session_planet_overrides.is_empty() and universe_mode == "random":
			_planet_overrides = session_planet_overrides

	# U0 captures use a deterministic world without affecting normal sessions.
	if OS.get_environment("SNT_U0_BASELINE") == "1":
		universe_mode = "solar_system"
		var baseline_seed := OS.get_environment("SNT_U0_BASELINE_SEED")
		universe_seed = (
				baseline_seed.to_int() if baseline_seed.is_valid_int() else 20260619)


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
			push_warning(
					"UniverseManager: unknown mode '%s', falling back to solar_system"
					% universe_mode)
			var sys := SolarSystemPreset.create_placeholder(universe_seed)
			systems.append(sys)


func _load_player_game_mode() -> void:
	if _save_dir == "":
		return
	var meta_path := _save_dir + "/universe_meta.json"
	if not FileAccess.file_exists(meta_path):
		return
	var file := FileAccess.open(meta_path, FileAccess.READ)
	if file == null:
		return
	var json_str := file.get_as_text()
	file.close()
	var json := JSON.new()
	if json.parse(json_str) != OK:
		return
	var meta: Dictionary = json.data
	player_game_mode = int(meta.get("player_game_mode", 0))
	player_health = float(meta.get("player_health", 100.0))
	player_source_law_dict = meta.get("player_source_law", {})
	player_satiation_dict = meta.get("player_satiation", {})


func _apply_planet_overrides() -> void:
	if _planet_overrides.is_empty():
		return

	# Find the spawn planet (breathable preferred).
	var spawn := _find_spawn_planet()
	if spawn == null:
		return

	# Apply terrain overrides to the spawn planet.
	var tp: String = str(_planet_overrides.get("terrain_preset", "default"))
	if tp != "default":
		match tp:
			"flat": spawn.terrain_height_scale = 6.0
			"hilly": spawn.terrain_height_scale = 14.0
			"mountainous": spawn.terrain_height_scale = 24.0
			"extreme": spawn.terrain_height_scale = 36.0

	var sl: String = str(_planet_overrides.get("sea_level_preset", "default"))
	if sl != "default":
		match sl:
			"none": spawn.sea_level_fraction = 0.0
			"low": spawn.sea_level_fraction = 0.1
			"medium": spawn.sea_level_fraction = 0.3
			"high": spawn.sea_level_fraction = 0.5

	var cv: String = str(_planet_overrides.get("cave_preset", "default"))
	if cv != "default":
		match cv:
			"sparse": spawn.cave_threshold = 0.55
			"normal": spawn.cave_threshold = 0.35
			"dense": spawn.cave_threshold = 0.18

	var at: String = str(_planet_overrides.get("atmosphere_preset", "default"))
	if at != "default":
		match at:
			"none": spawn.atmosphere_type = PlanetDescriptor.AtmosphereType.NONE
			"thin": spawn.atmosphere_type = PlanetDescriptor.AtmosphereType.THIN
			"breathable": spawn.atmosphere_type = PlanetDescriptor.AtmosphereType.BREATHABLE
			"toxic": spawn.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
			"corrosive": spawn.atmosphere_type = PlanetDescriptor.AtmosphereType.CORROSIVE

	# Apply size override — compute scale factor for other planets.
	var sz: String = str(_planet_overrides.get("size_preset", "default"))
	if sz != "default":
		var original_radius := spawn.planet_radius
		match sz:
			"small": spawn.planet_radius *= 0.5
			"medium": spawn.planet_radius *= 1.0
			"large": spawn.planet_radius *= 1.8
			"huge": spawn.planet_radius *= 3.0
		spawn.local_center.y = -spawn.planet_radius

		# Compute scale factor for other planets.
		if original_radius > 0.0:
			_planet_radius_scale = spawn.planet_radius / original_radius

	# Scale all other realized planets by the radius factor.
	if _planet_radius_scale != 1.0:
		for sys in systems:
			if not sys.is_realized():
				continue
			for planet in sys.planets:
				if planet.dimension_id == spawn.dimension_id:
					continue
				planet.planet_radius *= _planet_radius_scale
				planet.local_center.y = -planet.planet_radius


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
	tick_system = get_node_or_null(tick_system_path) as GDTickSystem
	quest_system = get_node_or_null(quest_system_path)


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

	# 使用 FloatingOrigin 的 double 精度宇宙坐标计算距离，
	# 避免大尺度下 float 精度损失导致系统流式加载错误。
	var player_upos: Vector3 = (
			floating_origin.get_universe_position()
			if floating_origin != null
			else _player.global_position)

	# Generate new placeholders around the player.
	var cells := _grid.get_cells_around(player_upos, system_stream_radius)
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
		var dist: float = (
				floating_origin.distance_to_player_vec3(
						sys.universe_position)
				if floating_origin != null
				else player_upos.distance_to(
						sys.universe_position))
		if dist > system_stream_radius * 1.2:
			to_remove.append(i)

	if not to_remove.is_empty():
		to_remove.reverse()
		for idx in to_remove:
			systems.remove_at(idx)


# --- System distance and realization ---

# Update the distance from each system to the player.
# 使用 FloatingOrigin 的 double 精度宇宙距离，避免大尺度 float 误差。
func _update_system_distances() -> void:
	if _player == null:
		return
	for sys in systems:
		if floating_origin != null:
			sys.distance_to_player = floating_origin.distance_to_player_vec3(sys.universe_position)
		else:
			sys.distance_to_player = _player.global_position.distance_to(sys.universe_position)


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
		# Apply radius scale for newly realized planets.
		if _planet_radius_scale != 1.0 and sys.system_id != &"sys_sol":
			for planet in sys.planets:
				planet.planet_radius *= _planet_radius_scale
				planet.local_center.y = -planet.planet_radius
		_create_lod_managers_for_system(sys)
		system_realized.emit(sys)


# --- LOD manager creation ---

# Create LOD managers for all currently realized systems.
func _create_lod_managers_for_realized() -> void:
	var started_usec := Time.get_ticks_usec()
	var realized_count := 0
	for sys in systems:
		if sys.is_realized():
			realized_count += 1
			_create_lod_managers_for_system(sys)
	_print_perf("UniverseManager.create_lod_managers_for_realized systems=%d lod_count=%d" % [
		realized_count,
		_lod_managers.size(),
	], started_usec)


# Create LOD managers for a single realized system.
func _create_lod_managers_for_system(system: StarSystemDescriptor) -> void:
	var started_usec := Time.get_ticks_usec()
	var before_count := _lod_managers.size()
	for star in system.stars:
		_create_star_lod(star)
	for planet in system.planets:
		_create_planet_lod(planet)
	_print_perf("UniverseManager.create_lod_managers_for_system system=%s stars=%d planets=%d added=%d" % [
		String(system.system_id),
		system.stars.size(),
		system.planets.size(),
		_lod_managers.size() - before_count,
	], started_usec)


func _create_planet_lod(planet: PlanetDescriptor) -> void:
	var lod := PlanetLodManager.new()
	lod.name = "LOD_%s" % String(planet.dimension_id)
	lod.planet_center = planet.universe_position
	lod.planet_radius = planet.planet_radius
	lod.atmosphere_height = planet.atmosphere_height
	lod.space_start_altitude = planet.space_start_altitude
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
	lod.atmosphere_height = planet.atmosphere_height
	lod.space_start_altitude = planet.space_start_altitude
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
		print("[UniverseManager] _set_initial_active_planet: "
				+ "no player, active_planet=%s"
				% [active_planet.display_name if active_planet else "null"])
		return

	# Choose a spawn planet: prefer breathable atmosphere (e.g., Earth).
	var spawn_planet := _find_spawn_planet()
	if spawn_planet != null:
		# Place player above the planet's local surface.
		# Player position doubles as the planet-local coordinate; the surface
		# top is at y≈0 (local_center = (0, -radius, 0)). Spawn above the
		# highest possible terrain (terrain_height_scale) to avoid clipping.
		var spawn_y := spawn_planet.terrain_height_scale + 4.0
		_player.global_position = Vector3(
			SPAWN_LOCAL_COORD, spawn_y, SPAWN_LOCAL_COORD)
		print("[UniverseManager] _set_initial_active_planet: "
				+ "spawn_planet=%s radius=%.1f local_center=%s spawn_pos=%s"
				% [spawn_planet.display_name, spawn_planet.planet_radius,
						spawn_planet.local_center, _player.global_position])
		_set_active_planet(spawn_planet)
		print("[UniverseManager] _set_initial_active_planet: "
				+ "after set_active, active_planet=%s bridge_initialized=%s"
				% [active_planet.display_name if active_planet else "null",
						_bridge_initialized])
		return

	var nearest := find_nearest_planet(_player.global_position)
	print("[UniverseManager] _set_initial_active_planet: "
			+ "no spawn_planet, nearest=%s player_pos=%s"
			% [nearest.display_name if nearest else "null",
					_player.global_position])
	if nearest != null:
		_set_active_planet(nearest)


# Find a suitable planet for the player's initial spawn.
# Prefers planets with breathable atmosphere (e.g., Earth), then falls back
# to the first landable planet. Stars are excluded.
func _find_spawn_planet() -> PlanetDescriptor:
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			if planet.atmosphere_type == PlanetDescriptor.AtmosphereType.BREATHABLE:
				return planet
	return _find_first_landable_planet()


func _update_active_planet() -> void:
	if _player == null:
		return

	# Check if the player is inside a station first.
	# 空间站使用局部坐标判断（空间站通常在玩家附近）。
	var nearest_station := find_nearest_station(_player.global_position)
	if nearest_station != null and nearest_station.is_in_gravity_range(_player.global_position):
		if active_station == null or nearest_station.dimension_id != active_station.dimension_id:
			_set_active_station(nearest_station)
		return

	# If we already have an active planet and the player is still within its
	# local gravity range, keep it. The player position doubles as the
	# planet-local coordinate, so we check against local_center.
	if active_planet != null and not active_planet.is_star:
		var local_dist := _player.global_position.distance_to(active_planet.local_center)
		if local_dist <= active_planet.gravity_radius():
			return

	# Otherwise, find the nearest planet by universe distance.
	# 使用 FloatingOrigin 的 double 精度距离，避免大尺度浮点误差。
	var nearest := _find_nearest_planet_by_universe_distance()
	if nearest == null:
		return

	if active_planet == null or nearest.dimension_id != active_planet.dimension_id:
		_set_active_planet(nearest)


# 使用 FloatingOrigin 的 double 精度距离查找最近的非恒星星球。
func _find_nearest_planet_by_universe_distance() -> PlanetDescriptor:
	if floating_origin == null:
		return null
	var best: PlanetDescriptor = null
	var best_dist: float = INF
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			var dist := floating_origin.distance_to_player_vec3(planet.universe_position)
			if dist < best_dist:
				best_dist = dist
				best = planet
	return best


func _set_active_station(station: StationDescriptor) -> void:
	active_station = station
	active_planet = null

	if _chunk_bridge != null:
		if not _bridge_initialized:
			var bridge_setup_started_usec := Time.get_ticks_usec()
			var config_started_usec := Time.get_ticks_usec()
			var config := BuiltinTerrainContentScript.create_config_for_universe(all_planets)
			_print_perf("UniverseManager.create_terrain_config station=%s planets=%d" % [
				String(station.dimension_id),
				all_planets.size(),
			], config_started_usec)
			var init_started_usec := Time.get_ticks_usec()
			_chunk_bridge.initialize_for_universe(config, station.dimension_id)
			_print_perf("UniverseManager.chunk_bridge_initialize station=%s" %
					String(station.dimension_id), init_started_usec)
			_bridge_initialized = true
			var tick_started_usec := Time.get_ticks_usec()
			_initializetick_system()
			_print_perf("UniverseManager.initialize_tick_system_after_bridge", tick_started_usec)
			_print_perf("UniverseManager.first_bridge_setup total", bridge_setup_started_usec)
		else:
			_chunk_bridge.set_active_dimension(station.dimension_id)


func _set_active_planet(planet: PlanetDescriptor) -> void:
	var _old := active_planet
	active_planet = planet
	active_station = null
	active_planet_changed.emit(planet)

	if _chunk_bridge != null and not planet.is_star:
		if not _bridge_initialized:
			var bridge_setup_started_usec := Time.get_ticks_usec()
			var config_started_usec := Time.get_ticks_usec()
			var config := BuiltinTerrainContentScript.create_config_for_universe(all_planets)
			_print_perf("UniverseManager.create_terrain_config planet=%s planets=%d" % [
				String(planet.dimension_id),
				all_planets.size(),
			], config_started_usec)
			var init_started_usec := Time.get_ticks_usec()
			_chunk_bridge.initialize_for_universe(config, planet.dimension_id)
			_print_perf("UniverseManager.chunk_bridge_initialize planet=%s" %
					String(planet.dimension_id), init_started_usec)
			_bridge_initialized = true
			var tick_started_usec := Time.get_ticks_usec()
			_initializetick_system()
			_print_perf("UniverseManager.initialize_tick_system_after_bridge", tick_started_usec)
			_print_perf("UniverseManager.first_bridge_setup total", bridge_setup_started_usec)
		else:
			_chunk_bridge.set_active_dimension(planet.dimension_id)

	# 更新所有 LOD 管理器的场景中心坐标。
	# 活跃星球使用 local_center，远景星球使用相对宇宙偏移。
	_update_all_lod_centers()
	_update_distant_body_visibility()

	# 重新居中浮动原点到新活跃星球的宇宙位置。
	if floating_origin != null:
		floating_origin.recenter_to_vec3(planet.universe_position)


# 更新所有 LOD 管理器的场景中心。
# 活跃星球：planet_center = local_center（局部体素坐标）。
# 远景星球：distant_scene_center =
# (distant.universe_position - active.universe_position) + active.local_center。
# 这样玩家（在活跃星球局部坐标系中）与所有星球之间的相对距离正确。
func _update_all_lod_centers() -> void:
	if active_planet == null:
		return
	var active_up := active_planet.universe_position
	var active_lc := active_planet.local_center
	# Deactivate and reposition distant bodies first. The active manager is
	# applied last because it owns the shared surface/space environment.
	for dim_id in _lod_managers.keys():
		var lod: PlanetLodManager = _lod_managers[dim_id]
		if lod == null or dim_id == active_planet.dimension_id:
			continue
		# 远景星球：相对活跃星球的宇宙偏移 + 活跃星球的 local_center。
		var planet := get_planet_by_dimension(dim_id)
		if planet == null:
			continue
		var relative := planet.universe_position - active_up
		var scene_center := relative + active_lc
		lod.set_scene_center(scene_center, false)

	var active_lod: PlanetLodManager = _lod_managers.get(active_planet.dimension_id)
	if active_lod != null:
		# 活跃星球：使用 local_center 作为场景中心，并接管全局环境。
		active_lod.set_scene_center(active_lc, true)


# Hide compressed-orbit planet proxy meshes while the player remains inside
# an atmosphere. The surface sky renders the local sun and moon instead.
func _update_distant_body_visibility() -> void:
	var hide_distant := false
	if active_planet != null and _player != null \
			and active_planet.atmosphere_type != PlanetDescriptor.AtmosphereType.NONE:
		var altitude := maxf(0.0, _player.global_position.distance_to(
				active_planet.local_center) - active_planet.planet_radius)
		var atmosphere_height := maxf(
				active_planet.horizon_fog_max_distance,
				active_planet.atmosphere_height)
		hide_distant = altitude <= atmosphere_height

	for dim_id in _lod_managers.keys():
		var lod: PlanetLodManager = _lod_managers[dim_id]
		if lod == null:
			continue
		var is_active_body: bool = active_planet != null \
				and dim_id == active_planet.dimension_id
		lod.set_hidden_by_surface_atmosphere(
				hide_distant and not is_active_body)


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

			# 活跃星球使用局部坐标距离（player_pos 即局部坐标）。
			# 远景星球使用 FloatingOrigin 的 double 精度宇宙距离。
			var dist: float
			if active_planet != null and planet.dimension_id == active_planet.dimension_id:
				dist = player_pos.distance_to(planet.local_center)
			elif floating_origin != null:
				dist = floating_origin.distance_to_player_vec3(planet.universe_position)
			else:
				dist = player_pos.distance_to(planet.universe_position)
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
		for item_key: String in accumulated.keys():
			var count: int = accumulated[item_key]
			print("UniverseManager: reconcile %d x %s for %s" % [
				count, item_key, String(dimension_id)])

	summary.clear_accumulated()


# --- Save / load universe ---

# Set the save directory for per-planet chunk serialization.
# Must be called before any planet is unloaded.
func set_save_dir(path: String) -> void:
	_save_dir = ProjectSettings.globalize_path(path)


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
func find_nearest_system(pos: Vector3) -> StarSystemDescriptor:
	var best: StarSystemDescriptor = null
	var best_dist: float = INF
	for sys in systems:
		var dist := pos.distance_to(sys.universe_position)
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
func find_nearest_planet(pos: Vector3) -> PlanetDescriptor:
	var best: PlanetDescriptor = null
	var best_dist: float = INF

	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			var dist := pos.distance_to(planet.universe_position)
			if dist < best_dist:
				best_dist = dist
				best = planet

	return best


# --- Public API: multi-planet travel ---

# 迁移期调试传送到指定星球。
# 该 API 切换活跃 dimension 并重定位玩家，只用于 U0 回归和调试；
# 正式连续宇宙旅行不得依赖该入口。
# 参数：
#   planet          — 目标星球描述符（必须已实现，不能是恒星）。
#   spawn_offset_y  — 玩家在目标星球表面的额外 Y 偏移（默认在地形高度上方 4 格）。
# 返回 true 表示旅行成功。
func travel_to_planet(planet: PlanetDescriptor, spawn_offset_y: float = 4.0) -> bool:
	if planet == null:
		push_warning("UniverseManager: travel_to_planet — planet is null")
		return false
	if planet.is_star:
		push_warning(
				"UniverseManager: travel_to_planet — cannot travel to a star (%s)"
				% planet.display_name)
		return false

	# 确保目标星球所在的星系已实现。
	var sys := get_system_for_planet(planet.dimension_id)
	if sys == null or not sys.is_realized():
		push_warning(
				"UniverseManager: travel_to_planet — system not realized for %s"
				% planet.display_name)
		return false

	print(("[UniverseManager] travel_to_planet: %s "
			+ "(dim=%s, universe_pos=%s, radius=%.1f)") % [
					planet.display_name, String(planet.dimension_id),
					planet.universe_position, planet.planet_radius])

	_is_traveling = true

	# 切换活跃星球（会触发 _update_all_lod_centers 和 floating_origin recenter）。
	_set_active_planet(planet)

	# 将玩家传送到目标星球的表面。
	# 局部坐标系约定：local_center = (0, -radius, 0)，地表在 y≈0。
	# 玩家生成在区块中心，避免刚开始移动就跨越两条区块边界。
	if _player != null:
		var spawn_y := planet.terrain_height_scale + spawn_offset_y
		_player.global_position = Vector3(
			SPAWN_LOCAL_COORD, spawn_y, SPAWN_LOCAL_COORD)
		# 重置玩家速度，避免残留的飞行惯性。
		if _player is CharacterBody3D:
			(_player as CharacterBody3D).velocity = Vector3.ZERO
		print("[UniverseManager] travel_to_planet: "
				+ "player repositioned to %s" % _player.global_position)

	# 更新玩家宇宙坐标。
	_update_player_universe_position()

	_is_traveling = false

	# 确保目标星球的 chunk 开始加载。
	if not _loaded_planets.has(planet.dimension_id):
		_load_planet(planet)

	print("[UniverseManager] travel_to_planet: complete, active=%s" % (
		active_planet.display_name if active_planet else "null"))
	return true


# 通过星球显示名称旅行（用于控制台命令）。
# 名称匹配不区分大小写，支持部分匹配（如 "mars" 匹配 "Mars"）。
# 返回 true 表示旅行成功。
func travel_to_planet_by_name(name: String) -> bool:
	var target := find_planet_by_name(name)
	if target == null:
		push_warning("UniverseManager: no planet found matching '%s'" % name)
		return false
	return travel_to_planet(target)


# 通过名称查找星球（不区分大小写，支持部分匹配）。
# 只在已实现的星系中查找，排除恒星。
func find_planet_by_name(name: String) -> PlanetDescriptor:
	var name_lower := name.to_lower()
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			if planet.display_name.to_lower() == name_lower:
				return planet
	# 部分匹配。
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			if planet.display_name.to_lower().contains(name_lower):
				return planet
	return null


# 获取所有可旅行的星球（已实现星系中的非恒星星球）。
# 返回数组，每个元素是 { "name": String, "dimension": StringName, "planet": PlanetDescriptor }。
func get_travelable_planets() -> Array:
	var result: Array = []
	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_star:
				continue
			result.append({
				"name": planet.display_name,
				"dimension": planet.dimension_id,
				"planet": planet,
			})
	return result


# 获取玩家当前的宇宙坐标（double 精度，通过 FloatingOrigin）。
func get_player_universe_position() -> Vector3:
	if floating_origin == null:
		return Vector3.ZERO
	return floating_origin.get_universe_position()


# 获取玩家到指定星球的宇宙距离（double 精度计算）。
func get_distance_to_planet(planet: PlanetDescriptor) -> float:
	if floating_origin == null or planet == null:
		return INF
	return floating_origin.distance_to_player_vec3(planet.universe_position)


# Find the nearest space station to a given universe-space position.
# Returns null if no stations exist.
func find_nearest_station(pos: Vector3) -> StationDescriptor:
	var best: StationDescriptor = null
	var best_dist: float = INF

	for station in stations:
		var dist := pos.distance_to(station.universe_position)
		if dist < best_dist:
			best_dist = dist
			best = station

	return best


# Compute the combined gravity direction at a given position.
# Considers gravity from planets, stars, and space stations.
# Station gravity always points downward (artificial gravity).
#
# 坐标系说明：
# - pos 是玩家的局部坐标（active planet 的局部体素坐标系）。
# - 活跃星球和空间站使用局部坐标计算重力（正确）。
# - 远景星球/恒星使用 FloatingOrigin 的 double 精度宇宙距离计算（避免 float 误差）。
#   但重力方向仍需转换回局部坐标系，以便玩家控制器使用。
func compute_gravity_direction(pos: Vector3) -> Vector3:
	# Check stations first — they have the strongest local gravity.
	for station in stations:
		if station.is_in_gravity_range(pos):
			return station.gravity_direction_at(pos)

	# When an active planet is set, use its local_center for gravity.
	# The player position doubles as the planet-local coordinate, so
	# local_center is the correct gravity source (not universe_position).
	# This avoids the player being pulled toward the Sun (at the universe
	# origin) while standing on a planet's local surface.
	if active_planet != null and not active_planet.is_star:
		var local_dist := pos.distance_to(active_planet.local_center)
		if local_dist > 0.001 and local_dist <= active_planet.gravity_radius():
			return (active_planet.local_center - pos).normalized()

	# 远景星球/恒星：使用 FloatingOrigin 的 double 精度距离判断。
	# 玩家在太空中（已离开活跃星球引力范围）时，由最近的恒星/星球提供重力。
	if floating_origin == null:
		return Vector3.ZERO

	# 场景坐标系：活跃星球的 local_center 对应其 universe_position。
	# 远景星球的场景位置 = (body.universe_position - active.universe_position) + active.local_center。
	var active_lc := active_planet.local_center if active_planet != null else Vector3.ZERO

	var best_dir := Vector3.ZERO
	var best_influence := 0.0

	for sys in systems:
		if not sys.is_realized():
			continue
		for body in sys.all_bodies():
			# double 精度距离计算。
			var dist := floating_origin.distance_to_player_vec3(body.universe_position)
			var gravity_radius := body.gravity_radius()
			if dist > gravity_radius or dist < 0.001:
				continue

			# 重力方向：从玩家场景位置指向星球场景位置。
			var body_scene := (
					floating_origin.universe_to_render_vec3(
							body.universe_position)
					+ active_lc)
			var dir := (body_scene - pos).normalized()
			var t := 1.0 - (dist / gravity_radius)
			var influence := body.gravity_multiplier * t * t

			if influence > best_influence:
				best_influence = influence
				best_dir = dir

	return best_dir


# Compute the gravity strength multiplier at a given position.
# Includes station gravity.
func compute_gravity_multiplier(pos: Vector3) -> float:
	# Check stations first.
	for station in stations:
		if station.is_in_gravity_range(pos):
			return station.gravity_multiplier

	# When an active planet is set, use its gravity multiplier directly
	# if the player is within its local gravity range.
	if active_planet != null and not active_planet.is_star:
		var local_dist := pos.distance_to(active_planet.local_center)
		if local_dist <= active_planet.gravity_radius():
			return active_planet.gravity_multiplier

	# 远景星球/恒星：使用 FloatingOrigin 的 double 精度距离。
	if floating_origin == null:
		return 0.0

	var best_mult := 0.0

	for sys in systems:
		if not sys.is_realized():
			continue
		for body in sys.all_bodies():
			var dist := floating_origin.distance_to_player_vec3(body.universe_position)
			var gravity_radius := body.gravity_radius()
			if dist > gravity_radius:
				continue

			var t := 1.0 - (dist / gravity_radius)
			var influence := body.gravity_multiplier * t * t

			if influence > best_mult:
				best_mult = influence

	return best_mult


# Check whether a position is within any planet's gravity range.
# Also checks space station gravity ranges.
func is_in_any_gravity_range(pos: Vector3) -> bool:
	for station in stations:
		if station.is_in_gravity_range(pos):
			return true

	for sys in systems:
		if not sys.is_realized():
			continue
		for planet in sys.planets:
			if planet.is_in_gravity_range(pos):
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


# Read-only snapshot for low-frequency prototype baseline capture.
func get_runtime_metrics() -> Dictionary:
	return {
		"universe_mode": universe_mode,
		"universe_seed": universe_seed,
		"system_count": systems.size(),
		"realized_system_count": get_realized_system_count(),
		"planet_count": all_planets.size(),
		"station_count": stations.size(),
		"loaded_planet_count": _loaded_planets.size(),
		"loaded_station_count": _loaded_stations.size(),
		"virtually_simulated_count": (
				_virtual_sim.get_simulated_dimensions().size() if _virtual_sim else 0),
		"active_planet": (
				String(active_planet.dimension_id) if active_planet else ""),
		"active_station": (
				String(active_station.dimension_id) if active_station else ""),
	}


# --- Tick system integration ---

# Initialize the GDTickSystem with the world data and register all subsystems.
# Called once after ChunkRendererBridge is initialized and GDWorldData is available.
func _initializetick_system() -> void:
	if tick_system == null or tick_system_initialized:
		return
	var started_usec := Time.get_ticks_usec()

	var world_data: GDWorldData = _chunk_bridge.get_world_data() if _chunk_bridge else null
	if world_data == null:
		push_warning("UniverseManager: cannot initialize tick system — no world data")
		return

	tick_system.set_world_data(world_data)

	# Apply gameplay config overrides from GameSession.
	if not _session_gameplay_config.is_empty():
		var current_config := world_data.get_gameplay_config()
		for key in _session_gameplay_config:
			if _session_gameplay_config.has(key):
				current_config[key] = _session_gameplay_config[key]
		world_data.set_gameplay_config(current_config)
		_session_gameplay_config = {}

	# Register subsystems in priority order.
	# DayNight must be first (priority 0) so other systems can read is_daytime.
	tick_system.register_day_night_system()
	tick_system.register_season_system()
	tick_system.register_ecosystem_system()

	tick_system_initialized = true

	# Initialize quest system after tick system is ready.
	_initializequest_system()
	_print_perf("UniverseManager.initializetick_system total", started_usec)


# Initialize the GDQuestSystem with quest content and tick counter.
func _initializequest_system() -> void:
	if quest_system == null or quest_system_initialized:
		return
	var started_usec := Time.get_ticks_usec()

	# Load quest content from QuestDatabase.
	var quest_db := preload("res://scripts/quest/QuestDatabase.gd").new()
	quest_db.name = "QuestDatabase"
	add_child(quest_db)
	quest_db.load_content(quest_system)

	quest_system_initialized = true
	_print_perf("UniverseManager.initializequest_system total", started_usec)


# Drive the simulation tick each frame.
# Updates the player chunk position and advances the simulation.
func _drivetick_system(delta: float) -> void:
	if tick_system == null or not tick_system_initialized:
		return

	# Determine the active dimension: station or planet.
	var dimension: String
	if active_station != null:
		dimension = String(active_station.dimension_id)
	elif active_planet != null:
		dimension = String(active_planet.dimension_id)
	else:
		dimension = "overworld"

	var player_chunk := _compute_player_chunk()

	tick_system.add_player_chunk(GameCommandServer.LOCAL_PLAYER_HANDLE, dimension, player_chunk.x, player_chunk.y, player_chunk.z)
	tick_system.tick(delta)

	# Sync tick counter to quest system for REACH_TICK conditions.
	if quest_system != null and quest_system_initialized:
		quest_system.set_tick_counter(tick_system.get_tick_count())


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
	var station_name := active_station.display_name if active_station else "None"
	var loaded_count := _loaded_planets.size()
	var loaded_station_count := _loaded_stations.size()
	var sim_count := _virtual_sim.get_simulated_dimensions().size() if _virtual_sim else 0
	var realized_count := get_realized_system_count()
	print("UniverseManager: mode=%s seed=%d systems=%d realized=%d "
			+ "planets=%d stations=%d active=%s active_station=%s "
			+ "loaded=%d loaded_stations=%d simulating=%d pos=%s" % [
					universe_mode, universe_seed, systems.size(),
					realized_count, all_planets.size(), stations.size(),
					active_name, station_name, loaded_count,
					loaded_station_count, sim_count, str(player_pos)])


# --- Internal helpers ---

func _find_first_landable_planet() -> PlanetDescriptor:
	for sys in systems:
		if sys.is_realized():
			var landable := sys.get_landable_planets()
			if not landable.is_empty():
				return landable[0]
	return null


# --- Space station management ---

# Create a new space station orbiting the given parent planet.
# Returns the created StationDescriptor, or null on failure.
# Parameters:
#   display_name:    player-chosen name for the station.
#   parent_planet:   the PlanetDescriptor this station orbits.
#   orbit_height:    height above the planet surface in universe units.
#   station_type:    StationDescriptor.StationType enum value.
#   gravity_mult:    gravity multiplier inside the station (1.0 = normal).
func create_station(
		display_name: String,
		parent_planet: PlanetDescriptor,
		orbit_height: float,
		station_type: int = StationDescriptor.StationType.OUTPOST,
		gravity_mult: float = 1.0) -> StationDescriptor:
	if parent_planet == null:
		push_warning("UniverseManager: cannot create station — no parent planet")
		return null

	var station := StationDescriptor.new()
	station.dimension_id = StringName(&"station_%d" % station_counter)
	station.display_name = display_name
	station.parent_planet_id = parent_planet.dimension_id
	station.orbit_height = orbit_height
	station.station_type = station_type as StationDescriptor.StationType
	station.gravity_multiplier = gravity_mult
	station.atmosphere_type = PlanetDescriptor.AtmosphereType.BREATHABLE
	station.seed = universe_seed ^ (station.dimension_id.hash() & 0xFFFFFFFF)
	station.system_id = parent_planet.system_id

	# Compute universe position: above the planet at orbit_height.
	# Direction: use the planet's "up" (positive Y in universe space).
	var orbit_dir := Vector3.UP
	station.universe_position = (
			parent_planet.universe_position
			+ orbit_dir * (parent_planet.planet_radius + orbit_height))

	# Mark the initial core chunks as occupied.
	station.initialize_core_chunks()

	# Fill the initial core with a floor at y=0.
	_populate_station_core(station)

	# Register the station.
	stations.append(station)
	station_counter += 1

	station_created.emit(station)
	return station


# Get a StationDescriptor by its dimension_id.
func get_station_by_dimension(dimension_id: StringName) -> StationDescriptor:
	for station in stations:
		if station.dimension_id == dimension_id:
			return station
	return null


# Check whether a station's voxel chunks are currently loaded.
func is_station_loaded(dimension_id: StringName) -> bool:
	return _loaded_stations.has(dimension_id)


# Get all stations orbiting a specific planet.
func get_stations_for_planet(planet_dimension_id: StringName) -> Array[StationDescriptor]:
	var result: Array[StationDescriptor] = []
	for station in stations:
		if station.parent_planet_id == planet_dimension_id:
			result.append(station)
	return result


# Fill the initial core chunks of a newly created station with a floor
# and perimeter walls. Uses stone for the floor and deepstone for walls.
# This is called once during create_station() after the core chunks are
# marked as occupied.
func _populate_station_core(station: StationDescriptor) -> void:
	if _chunk_bridge == null:
		return
	var world_data: GDWorldData = _chunk_bridge.get_world_data()
	if world_data == null:
		return

	var dim := String(station.dimension_id)
	var core_size := station.initial_core_size()
	var chunk_size := 32

	# Material IDs from BuiltinTerrainContent.
	var floor_material := 1    # MAT_STONE
	var wall_material := 13    # MAT_DEEPSTONE

	# Compute the global block range for the core.
	var half_x := core_size.x / 2.0
	var half_z := core_size.z / 2.0
	var min_x := -half_x * chunk_size
	var max_x := (-half_x + core_size.x) * chunk_size
	var min_z := -half_z * chunk_size
	var max_z := (-half_z + core_size.z) * chunk_size
	var wall_height := 4  # Walls are 4 blocks tall.

	for gx in range(min_x, max_x):
		for gz in range(min_z, max_z):
			# Floor at y=0.
			_set_station_block(world_data, dim, gx, 0, gz, floor_material)

			# Perimeter walls.
			var on_x_min := gx == min_x
			var on_x_max := gx == max_x - 1
			var on_z_min := gz == min_z
			var on_z_max := gz == max_z - 1
			if on_x_min or on_x_max or on_z_min or on_z_max:
				for gy in range(1, wall_height + 1):
					_set_station_block(world_data, dim, gx, gy, gz, wall_material)


# Helper: set a single block in the station dimension.
func _set_station_block(world_data: GDWorldData, dimension: String,
		block_x: int, block_y: int, block_z: int, material: int) -> void:
	var chunk_size := 32
	var cx := int(floorf(float(block_x) / chunk_size))
	var cy := int(floorf(float(block_y) / chunk_size))
	var cz := int(floorf(float(block_z) / chunk_size))
	var lx := block_x - cx * chunk_size
	var ly := block_y - cy * chunk_size
	var lz := block_z - cz * chunk_size
	world_data.set_terrain_cell(dimension, cx, cy, cz, lx, ly, lz, material)


# Load a station's chunks into memory.
# Called when the player approaches or enters the station.
func _load_station(station: StationDescriptor) -> void:
	var dim := station.dimension_id
	if _loaded_stations.has(dim):
		return

	_loaded_stations[dim] = true

	# Reconcile virtual production if the station was being simulated.
	if _virtual_sim != null and _virtual_sim.is_simulating(dim):
		_reconcile_virtual_production(dim)
		_virtual_sim.unregister_planet(dim)

	# Load chunk data from disk via the save manager.
	if _save_manager != null and _save_dir != "":
		_save_manager.load_planet(_save_dir, dim)

	station_loaded.emit(station)


# Unload a station's chunks from memory.
# Called when the player moves away from the station.
func _unload_station(station: StationDescriptor) -> void:
	var dim := station.dimension_id

	# Capture a production summary before unloading.
	var summary := PlanetSummary.create_from_world(dim, 0)

	# Serialize chunk data to disk and unload from memory.
	if _save_manager != null and _save_dir != "":
		_save_manager.save_planet(_save_dir, dim)

	# Unload chunks from memory.
	if _chunk_bridge != null:
		var world_data := _chunk_bridge.get_world_data()
		if world_data != null:
			world_data.unload_dimension(String(dim))

	# Register for virtual simulation.
	if _virtual_sim != null and summary.has_production():
		_virtual_sim.register_planet(summary)

	_loaded_stations.erase(dim)
	station_unloaded.emit(station)


# Update station loading/unloading based on player distance.
func _update_station_loading() -> void:
	if _player == null:
		return

	var player_pos := _player.global_position

	for station in stations:
		var dist := player_pos.distance_to(station.universe_position)
		var load_dist := station.gravity_radius()
		var unload_dist := load_dist * 1.5
		var is_loaded := _loaded_stations.has(station.dimension_id)

		if not is_loaded and dist <= load_dist:
			_load_station(station)
		elif is_loaded and dist > unload_dist:
			_unload_station(station)
