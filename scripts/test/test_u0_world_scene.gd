extends Node

const WORLD_SCENE := preload("res://WorldMap.tscn")
const SMOKE_SAVE_PATH := "user://u0_scene_smoke"
const WAIT_TIMEOUT_MSEC := 30000

var _failed := false


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	_configure_deterministic_session()
	var world := WORLD_SCENE.instantiate()
	var configured_bridge := world.get_node_or_null(
			"ChunkRendererBridge") as ChunkRendererBridge
	if configured_bridge != null:
		configured_bridge.loaded_radius = 1
		configured_bridge.view_radius = 1
		configured_bridge.start_chunk_radius = 1
	get_tree().root.add_child(world)

	var universe := world.get_node_or_null("UniverseManager") as UniverseManager
	var bridge := world.get_node_or_null("ChunkRendererBridge") as ChunkRendererBridge
	var player := world.get_node_or_null("Player") as CharacterBody3D
	if not _expect(universe != null, "UniverseManager is missing"):
		return
	if not _expect(bridge != null, "ChunkRendererBridge is missing"):
		return
	if not _expect(player != null, "Player is missing"):
		return

	var deadline := Time.get_ticks_msec() + WAIT_TIMEOUT_MSEC
	while (not bridge.is_initialized or bridge.get_world_data() == null
			or not bridge.get_world_data().has_chunk(
					String(bridge.active_dimension), 0, 0, 0)
			or bridge.get_world_data().get_async_pending_count() > 0):
		if Time.get_ticks_msec() >= deadline:
			_expect(false, "initial planet chunk generation timed out")
			return
		await get_tree().process_frame

	print("[U0SceneSmoke] chunk generation settled; checking spawn terrain")
	if not await _smoke_spawn_terrain(player, bridge, universe):
		return
	print("[U0SceneSmoke] spawn terrain passed; starting voxel/save checks")
	if not _smoke_voxel_and_save(bridge):
		return
	print("[U0SceneSmoke] voxel/save checks passed; starting LOD checks")
	if not _smoke_lod(universe):
		return
	print("[U0SceneSmoke] LOD checks passed; starting debug travel check")
	if not _smoke_debug_travel(universe, bridge):
		return

	_remove_smoke_save()
	print("U0 world scene smoke passed: generation, voxel edit, save/load, LOD, and debug travel.")
	get_tree().quit(0)


func _smoke_spawn_terrain(player: CharacterBody3D,
		bridge: ChunkRendererBridge, universe: UniverseManager) -> bool:
	var terrain: Dictionary = bridge.get_world_data().get_chunk_terrain(
			String(bridge.active_dimension), 0, 0, 0)
	var materials: PackedByteArray = terrain.get("materials", PackedByteArray())
	var snow_material := int(bridge.worldgen_config.get_material_id("snt:snow"))
	var ice_material := int(bridge.worldgen_config.get_material_id("snt:ice"))
	var non_air := 0
	var snow_blocks := 0
	var ice_blocks := 0
	for material in materials:
		if material != 0:
			non_air += 1
		if material == snow_material:
			snow_blocks += 1
		elif material == ice_material:
			ice_blocks += 1
	if not _expect(non_air > 0, "spawn chunk contains only air"):
		return false
	if not _expect(snow_blocks > 0,
			"polar spawn chunk is missing snow"):
		return false

	var deadline := Time.get_ticks_msec() + WAIT_TIMEOUT_MSEC
	while not player.is_on_floor():
		if Time.get_ticks_msec() >= deadline:
			return _expect(false,
					"player did not settle on spawn terrain; position=%s"
					% player.global_position)
		await get_tree().physics_frame

	var ground_cell := bridge.world_position_to_cell(
			player.global_position + Vector3.DOWN)
	var ground_info := bridge.get_cell_info(ground_cell, bridge.active_dimension)
	var ground_material := int(ground_info.get("data", {}).get("material", 0))
	if not _expect(ground_material == snow_material,
			"player did not settle on polar snow"):
		return false

	var active_lod := universe.get_node_or_null(
			"LOD_%s" % String(bridge.active_dimension)) as PlanetLodManager
	if not _expect(active_lod != null, "active planet LOD manager is missing"):
		return false
	var active_planet := universe.active_planet
	if not _expect(active_planet != null, "active planet is missing"):
		return false
	if not _expect(not active_lod.is_space_environment_active(),
			"surface view is still using the space environment"):
		return false
	var tick_system := universe.get_parent().get_node_or_null(
			"GDTickSystem") as GDTickSystem
	var day_state: Dictionary = tick_system.get_day_night_state() \
			if tick_system != null else {}
	if not _expect(bool(day_state.get("is_daytime", false))
			and float(day_state.get("time_of_day", 0.0)) >= 0.49,
			"new world did not start during daytime"):
		return false
	var sun_lod := universe.get_node_or_null(
			"LOD_star_sun") as PlanetLodManager
	var other_planet_lod := universe.get_node_or_null(
			"LOD_planet_mercury") as PlanetLodManager
	if not _expect(sun_lod != null and other_planet_lod != null
			and sun_lod.is_hidden_by_surface_atmosphere()
			and other_planet_lod.is_hidden_by_surface_atmosphere(),
			"compressed-orbit bodies are visible through the surface atmosphere"):
		return false
	var surface_position := player.global_position
	player.global_position = active_planet.local_center + Vector3.UP * (
			active_planet.planet_radius
			+ active_planet.atmosphere_height + 1.0)
	universe._update_distant_body_visibility()
	if not _expect(not other_planet_lod.is_hidden_by_surface_atmosphere(),
			"distant bodies did not reappear after leaving the atmosphere"):
		return false
	player.global_position = active_planet.local_center + Vector3.UP * (
			active_planet.planet_radius
			+ active_planet.space_start_altitude + 1.0)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _expect(active_lod.get_current_lod_level() >= PlanetLodManager.LOD_PROXY_SPHERE,
			"active planet did not switch to proxy LOD at space altitude "
			+ "(lod=%d active=%s surface_dist=%.1f space_start=%.1f)" % [
					active_lod.get_current_lod_level(), active_lod.is_active_planet,
					active_lod.get_surface_distance(),
					active_planet.space_start_altitude]):
		return false
	if not _expect(active_lod.is_space_environment_active(),
			"space altitude did not activate the space environment"):
		return false
	player.global_position = surface_position
	await get_tree().process_frame
	await get_tree().process_frame
	universe._update_distant_body_visibility()
	var world_environment := universe.get_parent().get_node_or_null(
			"WorldEnvironment") as WorldEnvironment
	if not _expect(world_environment != null
			and world_environment.environment != null
			and world_environment.environment.background_mode == Environment.BG_SKY
			and world_environment.environment.sky != null,
			"surface sky with visible sun/moon is missing"):
		return false
	var sky_material := world_environment.environment.sky.sky_material as ShaderMaterial
	if not _expect(sky_material != null and sky_material.shader != null
			and sky_material.shader.resource_path.ends_with("surface_sky.gdshader"),
			"surface sky is not using the day/night celestial shader"):
		return false

	var stone_material_id := int(
			bridge.worldgen_config.get_material_id("snt:stone"))
	var water_material_id := int(
			bridge.worldgen_config.get_material_id("snt:water"))
	var stone_material := bridge._get_material(stone_material_id) as ShaderMaterial
	var water_material := bridge._get_material(water_material_id) as ShaderMaterial
	if not _expect(stone_material != null and stone_material.shader.resource_path.ends_with(
			"terrain_block.gdshader"), "stone is not using the opaque shader"):
		return false
	if not _expect(water_material != null and water_material.shader.resource_path.ends_with(
			"terrain_block_transparent.gdshader"),
			"water is not using the transparent depth shader"):
		return false

	print("[U0SceneSmoke] polar spawn position=%s snow=%d ice=%d non_air=%d"
			% [player.global_position, snow_blocks, ice_blocks, non_air])
	return true


func _configure_deterministic_session() -> void:
	var game_session := get_tree().root.get_node_or_null("GameSession")
	if game_session == null:
		return
	game_session.set("universe_mode", "solar_system")
	game_session.set("universe_seed", 20260619)
	game_session.set("system_density", 0.0)
	game_session.set("save_path", "")


func _smoke_voxel_and_save(bridge: ChunkRendererBridge) -> bool:
	var world_data := bridge.get_world_data()
	var dimension := String(bridge.active_dimension)
	var original: Dictionary = world_data.get_terrain_cell(
		dimension, 0, 0, 0, 0, 0, 0)
	if not _expect(not original.is_empty(), "origin chunk cell is unavailable"):
		return false

	var original_material := int(original.get("material", 0))
	var placed_material := bridge.get_workbench_material_id()
	if placed_material == 0:
		placed_material = 1

	print("[U0SceneSmoke] voxel write/read")
	if not _expect(world_data.set_terrain_cell(
			dimension, 0, 0, 0, 0, 0, 0, placed_material),
			"voxel placement failed"):
		return false
	var placed: Dictionary = world_data.get_terrain_cell(
		dimension, 0, 0, 0, 0, 0, 0)
	if not _expect(int(placed.get("material", 0)) == placed_material,
			"placed voxel was not readable"):
		return false

	if not _expect(world_data.set_terrain_cell(
			dimension, 0, 0, 0, 0, 0, 0, 0), "voxel mining failed"):
		return false
	var mined: Dictionary = world_data.get_terrain_cell(
		dimension, 0, 0, 0, 0, 0, 0)
	if not _expect(int(mined.get("material", -1)) == 0,
			"mined voxel did not become air"):
		return false

	world_data.set_terrain_cell(dimension, 0, 0, 0, 0, 0, 0, placed_material)
	_remove_smoke_save()
	var save_path := ProjectSettings.globalize_path(SMOKE_SAVE_PATH)
	print("[U0SceneSmoke] save_dimension")
	var save_started_usec := Time.get_ticks_usec()
	var saved_count := int(world_data.save_dimension(save_path, dimension))
	var save_elapsed_ms := float(Time.get_ticks_usec() - save_started_usec) / 1000.0
	if not _expect(saved_count > 0, "dimension save wrote no chunks"):
		return false
	print("[U0SceneSmoke] unload_dimension")
	var unloaded_count := int(world_data.unload_dimension(dimension))
	if not _expect(unloaded_count > 0, "dimension unload removed no chunks"):
		return false
	print("[U0SceneSmoke] load_dimension")
	var load_started_usec := Time.get_ticks_usec()
	var loaded_count := int(world_data.load_dimension(save_path, dimension))
	var load_elapsed_ms := float(Time.get_ticks_usec() - load_started_usec) / 1000.0
	if not _expect(loaded_count == saved_count,
			"dimension reload chunk count changed: %d -> %d" % [
				saved_count, loaded_count]):
		return false
	var restored: Dictionary = world_data.get_terrain_cell(
		dimension, 0, 0, 0, 0, 0, 0)
	if not _expect(int(restored.get("material", 0)) == placed_material,
			"saved voxel material was not restored"):
		return false
	world_data.set_terrain_cell(dimension, 0, 0, 0, 0, 0, 0, original_material)
	print("[U0SceneSmoke] save_io chunks=%d save_ms=%.2f load_ms=%.2f" % [
		saved_count, save_elapsed_ms, load_elapsed_ms])
	return true


func _smoke_lod(universe: UniverseManager) -> bool:
	var planet := universe.active_planet
	if not _expect(planet != null, "active planet is missing"):
		return false
	var near_position := planet.universe_position + Vector3.UP * (planet.planet_radius + 1.0)
	var far_position := planet.universe_position + Vector3.UP * (planet.planet_radius * 100.0)
	var near_lod := int(GDPlanetLod.compute_lod_level(
		near_position, planet.universe_position, planet.planet_radius))
	var far_lod := int(GDPlanetLod.compute_lod_level(
		far_position, planet.universe_position, planet.planet_radius))
	return _expect(near_lod == PlanetLodManager.LOD_REAL_CHUNKS,
			"near planet position did not select real chunks") \
		and _expect(far_lod > near_lod, "far planet position did not select a coarser LOD")


func _smoke_debug_travel(universe: UniverseManager,
		bridge: ChunkRendererBridge) -> bool:
	var initial := universe.active_planet
	var target: PlanetDescriptor = null
	for candidate: PlanetDescriptor in universe.get_landable_planets():
		if initial == null or candidate.dimension_id != initial.dimension_id:
			target = candidate
			break
	if not _expect(target != null, "solar preset has no second landable planet"):
		return false
	if not _expect(universe.travel_to_planet(target),
			"migration-only debug travel was rejected"):
		return false
	if not _expect(universe.active_planet == target
			and bridge.active_dimension == target.dimension_id,
			"debug travel did not switch the active prototype dimension"):
		return false
	if target.atmosphere_type == PlanetDescriptor.AtmosphereType.NONE:
		universe._update_distant_body_visibility()
		var sun_lod := universe.get_node_or_null(
				"LOD_star_sun") as PlanetLodManager
		if not _expect(sun_lod != null
				and not sun_lod.is_hidden_by_surface_atmosphere(),
				"airless planet incorrectly hides distant celestial bodies"):
			return false
	return true


func _expect(condition: bool, message: String) -> bool:
	if condition:
		return true
	if _failed:
		return false
	_failed = true
	push_error("U0 world scene smoke failed: " + message)
	_remove_smoke_save()
	get_tree().quit(1)
	return false


func _remove_smoke_save() -> void:
	var absolute_path := ProjectSettings.globalize_path(SMOKE_SAVE_PATH)
	if not DirAccess.dir_exists_absolute(absolute_path):
		return
	_remove_directory_contents(absolute_path)
	DirAccess.remove_absolute(absolute_path)


func _remove_directory_contents(path: String) -> void:
	var dir := DirAccess.open(path)
	if dir == null:
		return
	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if entry != "." and entry != "..":
			var child := path.path_join(entry)
			if dir.current_is_dir():
				_remove_directory_contents(child)
				DirAccess.remove_absolute(child)
			else:
				DirAccess.remove_absolute(child)
		entry = dir.get_next()
	dir.list_dir_end()
