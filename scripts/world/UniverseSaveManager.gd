# UniverseSaveManager — high-level save/load orchestrator for a multi-planet universe.
#
# Directory layout on disk:
#   {save_dir}/
#     universe_header.bin          ← C++ binary: seed + universe_mode
#     universe_meta.json           ← JSON: planet descriptors (debug-friendly)
#     planets/
#       {dimension_id}/
#         planet_data.bin          ← C++ binary: header + production summary
#         regions/                 ← C++ region files for this planet's chunks
#
# The C++ SaveManager handles binary chunk I/O (planet_data.bin, regions/).
# This GDScript class handles the orchestration: which planets to save/load,
# when to serialize/deserialize summaries, and universe metadata persistence.
#
# Format choice rationale:
#   - planet_data.bin: binary, because summary is read/written frequently
#     (every planet unload/load) and the data is structured with fixed fields.
#   - universe_meta.json: JSON, because it's only written during full save,
#     and human-readability is valuable for debugging planet configurations.
class_name UniverseSaveManager
extends Node

signal save_completed(save_dir: String)
signal load_completed(save_dir: String)
signal planet_save_completed(dimension_id: StringName)
signal planet_load_completed(dimension_id: StringName)

# Reference to the UniverseManager (set via setup()).
var _universe_manager: UniverseManager = null

# Reference to the GDWorldData (obtained via ChunkRendererBridge).
var _world_data: GDWorldData = null


func _ready() -> void:
	pass


# --- Setup ---

# Set the UniverseManager reference. Must be called before any save/load.
func setup(universe_manager: UniverseManager, world_data: GDWorldData) -> void:
	_universe_manager = universe_manager
	_world_data = world_data


# --- Full universe save ---

# Save the entire universe: universe header + all loaded planets + summaries.
# This is the "save game" operation.
func save_universe(save_dir: String) -> bool:
	if _universe_manager == null or _world_data == null:
		push_warning("UniverseSaveManager: not initialized, call setup() first")
		return false

	# 1. Write universe header (C++ binary).
	var ok := GDWorldData.write_universe_header(
		save_dir, _world_data.get_seed(), _universe_manager.universe_mode)
	if not ok:
		push_warning("UniverseSaveManager: failed to write universe header")
		return false

	# 2. Save all loaded planets' chunk data.
	for planet in _universe_manager.get_landable_planets():
		var dim := planet.dimension_id
		if _universe_manager.is_planet_loaded(dim):
			var count := _world_data.save_dimension(save_dir, String(dim))
			if count < 0:
				push_warning("UniverseSaveManager: failed to save planet %s" % String(dim))
			else:
				planet_save_completed.emit(dim)

	# 3. Save universe metadata (JSON, for debugging).
	_save_universe_meta(save_dir)

	# 4. Save any planet summaries for unloaded planets (binary).
	_save_all_summaries(save_dir)

	save_completed.emit(save_dir)
	return true


# --- Full universe load ---

# Load the universe: header + metadata. Does NOT load any planet chunks;
# planets are loaded on-demand when the player approaches them.
func load_universe(save_dir: String) -> bool:
	if _universe_manager == null or _world_data == null:
		push_warning("UniverseSaveManager: not initialized, call setup() first")
		return false

	# 1. Read universe header.
	var header := GDWorldData.read_universe_header(save_dir)
	if not header.get("ok", false):
		push_warning("UniverseSaveManager: failed to read universe header")
		return false

	# 2. Apply universe mode and seed.
	_universe_manager.universe_mode = header.get("universe_mode", "solar_system")
	_world_data.set_seed(header.get("seed", 0))

	# 3. Load universe metadata (JSON).
	_load_universe_meta(save_dir)

	# 4. Load any saved summaries for virtual simulation (binary).
	_load_all_summaries(save_dir)

	load_completed.emit(save_dir)
	return true


# --- Per-planet save ---

# Save a single planet's chunk data and summary.
# Called by UniverseManager when unloading a planet.
func save_planet(save_dir: String, dimension_id: StringName) -> int:
	if _world_data == null:
		return -1

	var count := _world_data.save_dimension(save_dir, String(dimension_id))
	if count >= 0:
		_save_planet_summary(save_dir, dimension_id)
		planet_save_completed.emit(dimension_id)

	return count


# --- Per-planet load ---

# Load a single planet's chunk data from disk.
# Called by UniverseManager when the player approaches a planet.
func load_planet(save_dir: String, dimension_id: StringName) -> int:
	if _world_data == null:
		return -1

	var count := _world_data.load_dimension(save_dir, String(dimension_id))
	if count >= 0:
		planet_load_completed.emit(dimension_id)

	return count


# --- Planet summary persistence (binary via planet_data.bin) ---

# Save a planet's production summary by writing it into planet_data.bin.
# This overwrites the planet_data.bin header section with the summary appended.
func _save_planet_summary(save_dir: String, dimension_id: StringName) -> void:
	if _universe_manager == null or _world_data == null:
		return

	var sim := _universe_manager.get_virtual_simulator()
	if sim == null:
		return

	var summary := sim.get_summary(dimension_id)
	if summary == null:
		return

	# Write planet_data.bin with header + summary.
	var pdir := save_dir + "/planets/" + String(dimension_id)
	var summary_dict := summary.to_dict()
	GDWorldData.write_planet_data(pdir, _world_data.get_seed(), String(dimension_id), summary_dict)


# Load a planet's production summary from planet_data.bin.
# Returns null if no summary is found in the binary file.
func _load_planet_summary(save_dir: String, dimension_id: StringName) -> PlanetSummary:
	var pdir := save_dir + "/planets/" + String(dimension_id)
	var result := GDWorldData.read_planet_data(pdir)

	if not result.get("ok", false):
		return null

	if not result.get("has_summary", false):
		return null

	var summary_dict: Dictionary = result.get("summary", {})
	if summary_dict.is_empty():
		return null

	return PlanetSummary.from_dict(summary_dict)


# Save all virtual simulation summaries to binary.
func _save_all_summaries(save_dir: String) -> void:
	if _universe_manager == null:
		return

	var sim := _universe_manager.get_virtual_simulator()
	if sim == null:
		return

	for dim in sim.get_simulated_dimensions():
		_save_planet_summary(save_dir, dim)


# Load all saved summaries and register them with the virtual simulator.
func _load_all_summaries(save_dir: String) -> void:
	if _universe_manager == null:
		return

	var sim := _universe_manager.get_virtual_simulator()
	if sim == null:
		return

	var planets := GDWorldData.list_planets(save_dir)
	for dim_str in planets:
		var dim := StringName(dim_str)
		# Only load summaries for planets that are NOT currently loaded.
		if _universe_manager.is_planet_loaded(dim):
			continue

		var summary := _load_planet_summary(save_dir, dim)
		if summary != null and summary.has_production():
			sim.register_planet(summary)


# --- Universe metadata (JSON, for debugging) ---

# Save universe metadata: planet descriptors and their state.
func _save_universe_meta(save_dir: String) -> void:
	if _universe_manager == null:
		return

	var meta := {
		"universe_mode": _universe_manager.universe_mode,
		"universe_seed": _universe_manager.universe_seed,
		"planets": [],
	}

	for planet in _universe_manager.planets:
		var pd := {
			"dimension_id": String(planet.dimension_id),
			"display_name": planet.display_name,
			"universe_position": [planet.universe_position.x, planet.universe_position.y, planet.universe_position.z],
			"planet_radius": planet.planet_radius,
			"seed": planet.seed,
			"is_star": planet.is_star,
			"gravity_multiplier": planet.gravity_multiplier,
			"atmosphere_color": [planet.atmosphere_color.r, planet.atmosphere_color.g, planet.atmosphere_color.b],
			"loaded": _universe_manager.is_planet_loaded(planet.dimension_id),
		}
		meta["planets"].append(pd)

	var path := save_dir + "/universe_meta.json"
	var json := JSON.new()
	var json_str := json.stringify(meta, "\t")
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(json_str)
		file.close()


# Load universe metadata and restore planet descriptors.
func _load_universe_meta(save_dir: String) -> void:
	var path := save_dir + "/universe_meta.json"
	if not FileAccess.file_exists(path):
		return

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return

	var json_str := file.get_as_text()
	file.close()

	var json := JSON.new()
	if json.parse(json_str) != OK:
		return

	var meta: Dictionary = json.data
	_universe_manager.universe_mode = meta.get("universe_mode", "solar_system")
	_universe_manager.universe_seed = int(meta.get("universe_seed", 0))

	# Planet descriptors are regenerated from the universe seed,
	# so we don't need to restore them from the meta file.
	# The meta file is primarily for debugging and validation.


# --- Utility ---

# List all saved planets in a save directory.
func list_saved_planets(save_dir: String) -> Array:
	return GDWorldData.list_planets(save_dir)


# Check if a save directory contains a valid universe.
func has_universe_save(save_dir: String) -> bool:
	var header := GDWorldData.read_universe_header(save_dir)
	return header.get("ok", false)


# Delete a planet's save data from disk.
func delete_planet_save(save_dir: String, dimension_id: StringName) -> bool:
	var dim_str := String(dimension_id)
	var planet_path := save_dir + "/planets/" + dim_str

	if not DirAccess.dir_exists_absolute(planet_path):
		return true

	return DirAccess.remove_absolute(planet_path) == OK
