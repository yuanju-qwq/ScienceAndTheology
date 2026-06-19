# StarSystemDescriptor — data class describing a single star system in the universe.
# A star system contains 0-3 stars and 0-12 planets orbiting the primary star.
# Supports two generation states:
#   "placeholder" — only position, type, and seed are known; no detailed body data.
#   "realized"    — full star and planet descriptors have been generated.
# The placeholder → realized transition happens on-demand when the player
# approaches the system, enabling a virtually infinite universe.
class_name StarSystemDescriptor
extends Resource

# System type constants.
const TYPE_SINGLE_STAR := "SINGLE_STAR"
const TYPE_BINARY := "BINARY"
const TYPE_TRINARY := "TRINARY"
const TYPE_STARLESS := "STARLESS"
const TYPE_REMNANT := "REMNANT"

# Generation state constants.
const STATE_PLACEHOLDER := "placeholder"
const STATE_REALIZED := "realized"

# Unique system identifier (e.g. &"sys_0").
@export var system_id: StringName = &""

# System type: one of TYPE_SINGLE_STAR, TYPE_BINARY, TYPE_TRINARY,
# TYPE_STARLESS, TYPE_REMNANT.
@export var system_type: String = TYPE_SINGLE_STAR

# Position of the system barycenter in universe-space coordinates.
@export var universe_position: Vector3 = Vector3.ZERO

# Bounding radius of the system in universe-space units.
# Used for LOD switching and proximity-based realization.
# For placeholder systems this is an estimate; for realized systems
# it is computed from the outermost orbit.
@export var system_radius: float = 5000.0

# Per-system seed for deterministic generation of stars and planets.
@export var system_seed: int = 0

# Generation state: STATE_PLACEHOLDER or STATE_REALIZED.
@export var generation_state: String = STATE_PLACEHOLDER

# Stars in this system (1-3 for star systems, 0 for starless).
# Only populated when generation_state == STATE_REALIZED.
@export var stars: Array[PlanetDescriptor] = []

# Planets orbiting the primary star (or barycenter for starless).
# Only populated when generation_state == STATE_REALIZED.
@export var planets: Array[PlanetDescriptor] = []

# Distance from the player's current position (updated each frame by UniverseManager).
# Not serialized — runtime only.
var distance_to_player: float = INF


# Check whether this system has been realized (full body data available).
func is_realized() -> bool:
	return generation_state == STATE_REALIZED


# Get all bodies (stars + planets) as a flat array.
# Only meaningful when is_realized() is true.
func all_bodies() -> Array[PlanetDescriptor]:
	var result: Array[PlanetDescriptor] = []
	result.append_array(stars)
	result.append_array(planets)
	return result


# Get all landable (non-star) planets.
func get_landable_planets() -> Array[PlanetDescriptor]:
	var result: Array[PlanetDescriptor] = []
	for planet in planets:
		if not planet.is_star:
			result.append(planet)
	return result


# Get the primary star (first star in the stars array).
# Returns null for starless systems.
func get_primary_star() -> PlanetDescriptor:
	if stars.is_empty():
		return null
	return stars[0]


# Check whether a given universe-space position is within this system's
# bounding radius (used for proximity-based realization).
func is_in_range(position: Vector3) -> bool:
	return position.distance_to(universe_position) <= system_radius


# Serialize the placeholder-level data to a Dictionary for save files.
# Only saves data that exists in both placeholder and realized states.
func to_placeholder_dict() -> Dictionary:
	return {
		"system_id": String(system_id),
		"system_type": system_type,
		"universe_position": [
			universe_position.x,
			universe_position.y,
			universe_position.z,
		],
		"system_radius": system_radius,
		"system_seed": system_seed,
		"generation_state": generation_state,
	}


# Serialize the full system (including realized body data) to a Dictionary.
func to_dict() -> Dictionary:
	var d := to_placeholder_dict()
	var stars_arr: Array = []
	var planets_arr: Array = []
	for star in stars:
		stars_arr.append(_planet_to_dict(star))
	for planet in planets:
		planets_arr.append(_planet_to_dict(planet))
	d["stars"] = stars_arr
	d["planets"] = planets_arr
	return d


# Deserialize a placeholder from a Dictionary.
static func from_placeholder_dict(data: Dictionary) -> StarSystemDescriptor:
	var sys := StarSystemDescriptor.new()
	sys.system_id = StringName(data.get("system_id", "") as String)
	sys.system_type = data.get("system_type", TYPE_SINGLE_STAR)
	var pos: Array = data.get("universe_position", [0.0, 0.0, 0.0])
	sys.universe_position = Vector3(float(pos[0]), float(pos[1]), float(pos[2]))
	sys.system_radius = data.get("system_radius", 5000.0)
	sys.system_seed = int(data.get("system_seed", 0))
	sys.generation_state = data.get("generation_state", STATE_PLACEHOLDER)
	return sys


# Deserialize a full system (including body data) from a Dictionary.
static func from_dict(data: Dictionary) -> StarSystemDescriptor:
	var sys := from_placeholder_dict(data)
	sys.stars.clear()
	sys.planets.clear()
	for star_data in data.get("stars", []):
		sys.stars.append(_planet_from_dict(star_data))
	for planet_data in data.get("planets", []):
		sys.planets.append(_planet_from_dict(planet_data))
	return sys


# Helper: serialize a PlanetDescriptor to a minimal Dictionary for save.
static func _planet_to_dict(planet: PlanetDescriptor) -> Dictionary:
	return {
		"dimension_id": String(planet.dimension_id),
		"display_name": planet.display_name,
		"universe_position": [
			planet.universe_position.x,
			planet.universe_position.y,
			planet.universe_position.z,
		],
		"planet_radius": planet.planet_radius,
		"local_center": [
			planet.local_center.x,
			planet.local_center.y,
			planet.local_center.z,
		],
		"seed": planet.seed,
		"is_star": planet.is_star,
		"system_id": String(planet.system_id),
		"star_spectral_type": planet.star_spectral_type,
		"is_primary_star": planet.is_primary_star,
		"gravity_multiplier": planet.gravity_multiplier,
		"atmosphere_type": planet.atmosphere_type,
		"toxic_damage_per_sec": planet.toxic_damage_per_sec,
		"corrosive_damage_per_sec": planet.corrosive_damage_per_sec,
		"vacuum_damage_per_sec": planet.vacuum_damage_per_sec,
		"star_color": [
			planet.star_color.r,
			planet.star_color.g,
			planet.star_color.b,
		],
		"star_light_energy": planet.star_light_energy,
		"atmosphere_color": [
			planet.atmosphere_color.r,
			planet.atmosphere_color.g,
			planet.atmosphere_color.b,
			planet.atmosphere_color.a,
		],
		"atmosphere_scale": planet.atmosphere_scale,
		"atmosphere_power": planet.atmosphere_power,
		"atmosphere_intensity": planet.atmosphere_intensity,
		"terrain_height_scale": planet.terrain_height_scale,
		"elevation_noise_scale": planet.elevation_noise_scale,
		"elevation_octaves": planet.elevation_octaves,
		"sea_level_fraction": planet.sea_level_fraction,
	}


# Helper: deserialize a PlanetDescriptor from a minimal Dictionary.
static func _planet_from_dict(data: Dictionary) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(data.get("dimension_id", "") as String)
	desc.display_name = data.get("display_name", "Planet")
	var pos: Array = data.get("universe_position", [0.0, 0.0, 0.0])
	desc.universe_position = Vector3(float(pos[0]), float(pos[1]), float(pos[2]))
	desc.planet_radius = data.get("planet_radius", 512.0)
	var lc: Array = data.get("local_center", [0.0, -512.0, 0.0])
	desc.local_center = Vector3(float(lc[0]), float(lc[1]), float(lc[2]))
	desc.seed = int(data.get("seed", 0))
	desc.is_star = data.get("is_star", false)
	desc.system_id = StringName(data.get("system_id", "") as String)
	desc.star_spectral_type = int(data.get("star_spectral_type", 5))
	desc.is_primary_star = data.get("is_primary_star", false)
	desc.gravity_multiplier = data.get("gravity_multiplier", 1.0)
	desc.atmosphere_type = int(data.get("atmosphere_type", PlanetDescriptor.AtmosphereType.BREATHABLE)) as PlanetDescriptor.AtmosphereType
	desc.toxic_damage_per_sec = data.get("toxic_damage_per_sec", 5.0)
	desc.corrosive_damage_per_sec = data.get("corrosive_damage_per_sec", 8.0)
	desc.vacuum_damage_per_sec = data.get("vacuum_damage_per_sec", 3.0)
	var sc: Array = data.get("star_color", [1.0, 0.95, 0.8])
	desc.star_color = Color(float(sc[0]), float(sc[1]), float(sc[2]))
	desc.star_light_energy = data.get("star_light_energy", 2.2)
	var ac: Array = data.get("atmosphere_color", [0.3, 0.6, 1.0, 1.0])
	desc.atmosphere_color = Color(float(ac[0]), float(ac[1]), float(ac[2]), float(ac[3]) if ac.size() > 3 else 1.0)
	desc.atmosphere_scale = data.get("atmosphere_scale", 1.08)
	desc.atmosphere_power = data.get("atmosphere_power", 3.5)
	desc.atmosphere_intensity = data.get("atmosphere_intensity", 1.2)
	desc.terrain_height_scale = data.get("terrain_height_scale", 16.0)
	desc.elevation_noise_scale = data.get("elevation_noise_scale", 0.008)
	desc.elevation_octaves = data.get("elevation_octaves", 5)
	desc.sea_level_fraction = data.get("sea_level_fraction", 0.3)
	return desc
