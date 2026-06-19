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
	if not _expect(universe != null, "UniverseManager is missing"):
		return
	if not _expect(bridge != null, "ChunkRendererBridge is missing"):
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

	print("[U0SceneSmoke] chunk generation settled; starting voxel/save checks")
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
	return _expect(universe.active_planet == target
			and bridge.active_dimension == target.dimension_id,
			"debug travel did not switch the active prototype dimension")


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
