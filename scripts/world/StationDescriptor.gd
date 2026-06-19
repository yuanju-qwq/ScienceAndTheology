# StationDescriptor — migration-only dimension prototype for a space station.
# A space station is a flat voxel dimension (planet_radius = 0) placed at
# a universe-space position in orbit around a parent planet.
# Unlike planets, stations use build-aware chunk loading: only chunks
# that contain player-placed blocks are loaded and simulated.
#
# U0 compatibility lifecycle (do not extend with new formal gameplay):
#   1. Player uses "Station Blueprint" item → opens setup UI.
#   2. Player confirms parameters → UniverseManager creates the station.
#   3. A MapConnector is generated linking the player's position to the
#      station's entrance cell.
#   4. Player enters the station via the connector and builds freely.
#   5. When the player leaves, the station is serialized and virtually
#      simulated via PlanetSummary (same as planets).
class_name StationDescriptor
extends Resource

# --- Station type enum ---

# Determines the visual style and default size of the station.
enum StationType {
	OUTPOST = 0,   # Small outpost — 1 chunk initial core.
	HABITAT = 1,   # Medium habitat — 3x1x3 chunk initial core.
	FACTORY  = 2,  # Large factory — 5x1x5 chunk initial core.
}

# Unique dimension identifier used as the chunk storage key in C++ WorldData.
# Format: "station_{index}" (e.g., "station_0", "station_1").
@export var dimension_id: StringName = &"station_0"

# Human-readable display name for UI (set by player during creation).
@export var display_name: String = "Space Station"

# Position of the station center in universe-space coordinates.
# Computed as: parent_planet.universe_position + orbit_offset.
@export var universe_position: Vector3 = Vector3.ZERO

# The dimension_id of the parent planet this station orbits.
@export var parent_planet_id: StringName = &"overworld"

# Orbital height above the parent planet surface in universe-space units.
# This determines where the station sits relative to the planet.
@export var orbit_height: float = 2000.0

# Station type — determines initial core size and visual style.
@export var station_type: StationType = StationType.OUTPOST

# Per-station seed for deterministic generation (if needed).
@export var seed: int = 0

# Gravity multiplier inside the station.
# 1.0 = standard downward gravity (artificial gravity).
# 0.0 = zero-G (players must use fly mode).
@export var gravity_multiplier: float = 1.0

# Atmosphere type inside the station (default: breathable for convenience).
@export var atmosphere_type: int = 2  # PlanetDescriptor.AtmosphereType.BREATHABLE

# --- Build-aware chunk tracking ---

# Set of occupied chunk coordinates stored as packed Vector3i strings.
# Format: "{x},{y},{z}" — e.g., "0,0,0", "1,0,0".
# Only these chunks are loaded, rendered, and simulated.
# Populated when the player places blocks in the station dimension.
@export var occupied_chunk_keys: PackedStringArray = PackedStringArray()

# --- Runtime cache (not serialized) ---

# Cached set for fast lookup. Rebuilt from occupied_chunk_keys on load.
var _occupied_chunks: Dictionary = {}

# The star system this station belongs to.
# Matches StarSystemDescriptor.system_id.
@export var system_id: StringName = &""


# --- Occupied chunk management ---

# Rebuild the runtime lookup cache from occupied_chunk_keys.
# Must be called after loading from save data.
func rebuild_occupied_cache() -> void:
	_occupied_chunks.clear()
	for key in occupied_chunk_keys:
		_occupied_chunks[key] = true


# Mark a chunk as occupied. Returns true if newly added.
func mark_chunk_occupied(chunk: Vector3i) -> bool:
	var key := _chunk_key(chunk)
	if _occupied_chunks.has(key):
		return false
	_occupied_chunks[key] = true
	occupied_chunk_keys.append(key)
	return true


# Check if a chunk is in the occupied set.
func is_chunk_occupied(chunk: Vector3i) -> bool:
	return _occupied_chunks.has(_chunk_key(chunk))


# Get all occupied chunk coordinates as Vector3i array.
func get_occupied_chunks() -> Array[Vector3i]:
	var result: Array[Vector3i] = []
	for key in occupied_chunk_keys:
		var parts := key.split(",")
		if parts.size() == 3:
			result.append(Vector3i(
				int(parts[0]), int(parts[1]), int(parts[2])))
	return result


# Get the number of occupied chunks.
func occupied_chunk_count() -> int:
	return occupied_chunk_keys.size()


# --- Initial core size ---

# Return the initial core size in chunks based on station type.
func initial_core_size() -> Vector3i:
	match station_type:
		StationType.OUTPOST:
			return Vector3i(1, 1, 1)
		StationType.HABITAT:
			return Vector3i(3, 1, 3)
		StationType.FACTORY:
			return Vector3i(5, 1, 5)
		_:
			return Vector3i(1, 1, 1)


# Mark all chunks in the initial core as occupied.
# Called once when the station is created.
func initialize_core_chunks() -> void:
	var size := initial_core_size()
	@warning_ignore("integer_division")
	var half_x := size.x / 2
	@warning_ignore("integer_division")
	var half_z := size.z / 2
	for cx in range(-half_x, -half_x + size.x):
		for cy in range(0, size.y):
			for cz in range(-half_z, -half_z + size.z):
				mark_chunk_occupied(Vector3i(cx, cy, cz))


# --- Gravity ---

# Compute the gravity influence radius for this station.
# Stations have a small gravity zone centered on their position.
func gravity_radius() -> float:
	# Station gravity zone = 2x the occupied footprint, minimum 128.
	var max_chunk_extent := maxf(
		float(initial_core_size().x),
		float(initial_core_size().z)) * 32.0
	return maxf(max_chunk_extent * 2.0, 128.0)


# Check whether a given universe-space position is within the
# gravity influence zone of this station.
func is_in_gravity_range(position: Vector3) -> bool:
	return position.distance_to(universe_position) <= gravity_radius()


# Compute the gravity direction at a given universe-space position.
# Station gravity always points downward (artificial gravity).
# Returns Vector3.ZERO if the position is outside gravity range.
func gravity_direction_at(position: Vector3) -> Vector3:
	if not is_in_gravity_range(position):
		return Vector3.ZERO
	return Vector3.DOWN


# --- Serialization helpers ---

# Convert to a Dictionary for save metadata (JSON-friendly).
func to_dict() -> Dictionary:
	return {
		"dimension_id": String(dimension_id),
		"display_name": display_name,
		"universe_position": [
			universe_position.x,
			universe_position.y,
			universe_position.z,
		],
		"parent_planet_id": String(parent_planet_id),
		"orbit_height": orbit_height,
		"station_type": station_type,
		"seed": seed,
		"gravity_multiplier": gravity_multiplier,
		"atmosphere_type": atmosphere_type,
		"occupied_chunk_keys": occupied_chunk_keys,
		"system_id": String(system_id),
	}


# Create from a Dictionary (loaded from save metadata).
static func from_dict(data: Dictionary) -> StationDescriptor:
	var station := StationDescriptor.new()
	station.dimension_id = StringName(data.get("dimension_id", "station_0") as String)
	station.display_name = data.get("display_name", "Space Station")
	var pos: Array = data.get("universe_position", [0.0, 0.0, 0.0])
	station.universe_position = Vector3(
		float(pos[0]) if pos.size() > 0 else 0.0,
		float(pos[1]) if pos.size() > 1 else 0.0,
		float(pos[2]) if pos.size() > 2 else 0.0)
	station.parent_planet_id = StringName(data.get("parent_planet_id", "overworld") as String)
	station.orbit_height = data.get("orbit_height", 2000.0)
	station.station_type = int(data.get("station_type", 0)) as StationType
	station.seed = data.get("seed", 0)
	station.gravity_multiplier = data.get("gravity_multiplier", 1.0)
	station.atmosphere_type = data.get("atmosphere_type", 2)
	station.occupied_chunk_keys = data.get("occupied_chunk_keys", PackedStringArray())
	station.system_id = StringName(data.get("system_id", "") as String)
	station.rebuild_occupied_cache()
	return station


# --- Internal helpers ---

func _chunk_key(chunk: Vector3i) -> String:
	return "%d,%d,%d" % [chunk.x, chunk.y, chunk.z]
