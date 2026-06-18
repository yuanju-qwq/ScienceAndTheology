# SolarSystemPreset — generates a simplified solar system layout.
# Produces a StarSystemDescriptor containing the Sun, inner rocky planets,
# and outer gas/ice giants. Orbital distances and radii are scaled for
# gameplay (not realistic).
#
# Follows the same placeholder → realize pattern as RandomUniverseGenerator:
# create_placeholder() returns a minimal descriptor, and realize() fills in
# the fixed star and planet data on demand.
#
# Also provides a legacy generate_flat() method that returns a flat
# Array[PlanetDescriptor] for backward compatibility.
class_name SolarSystemPreset
extends RefCounted

# Scale factor: 1 unit in universe space = 1 voxel block.
# Orbital distances are compressed so inter-planet travel is feasible.
# Real solar system distances are far too large for voxel-scale gameplay.
const ORBIT_SCALE := 3000.0

# Estimated system radius for the placeholder (Neptune orbit + padding).
const ESTIMATED_SYSTEM_RADIUS := ORBIT_SCALE * 30.0 + 2000.0


# --- Placeholder API ---

# Create a placeholder StarSystemDescriptor for the solar system.
# Contains only position, type, and seed — no star/planet data.
static func create_placeholder(universe_seed: int) -> StarSystemDescriptor:
	var sys := StarSystemDescriptor.new()
	sys.system_id = &"sys_sol"
	sys.system_type = StarSystemDescriptor.TYPE_SINGLE_STAR
	sys.universe_position = Vector3.ZERO
	sys.system_seed = universe_seed
	sys.system_radius = ESTIMATED_SYSTEM_RADIUS
	sys.generation_state = StarSystemDescriptor.STATE_PLACEHOLDER
	return sys


# Realize a solar system placeholder: populate the fixed star and planet data.
# Returns the modified StarSystemDescriptor (generation_state = "realized").
static func realize(system: StarSystemDescriptor) -> StarSystemDescriptor:
	if system.is_realized():
		return system

	var seed_val := system.system_seed

	var sun := _create_sun(seed_val)
	sun.system_id = system.system_id
	system.stars.append(sun)

	var planets_data := [
		_create_mercury(seed_val),
		_create_venus(seed_val),
		_create_earth(seed_val),
		_create_mars(seed_val),
		_create_jupiter(seed_val),
		_create_saturn(seed_val),
		_create_uranus(seed_val),
		_create_neptune(seed_val),
	]

	for planet in planets_data:
		planet.system_id = system.system_id
		system.planets.append(planet)

	system.generation_state = StarSystemDescriptor.STATE_REALIZED
	_recompute_system_radius(system)
	return system


# --- Convenience: generate (placeholder + realize in one step) ---

# Generate the solar system as a realized StarSystemDescriptor.
# Returns a single system with the Sun as primary star and 8 planets.
static func generate(universe_seed: int) -> StarSystemDescriptor:
	var sys := create_placeholder(universe_seed)
	return realize(sys)


# --- Legacy API: flat planet list generation ---

# Generate the solar system as a flat Array of PlanetDescriptor resources.
# Kept for backward compatibility with the old UniverseManager API.
static func generate_flat(universe_seed: int) -> Array[PlanetDescriptor]:
	var planets: Array[PlanetDescriptor] = []
	planets.append(_create_sun(universe_seed))
	planets.append(_create_mercury(universe_seed))
	planets.append(_create_venus(universe_seed))
	planets.append(_create_earth(universe_seed))
	planets.append(_create_mars(universe_seed))
	planets.append(_create_jupiter(universe_seed))
	planets.append(_create_saturn(universe_seed))
	planets.append(_create_uranus(universe_seed))
	planets.append(_create_neptune(universe_seed))
	return planets


# --- Star ---

static func _create_sun(_universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"star_sun"
	desc.display_name = "Sun"
	desc.universe_position = Vector3.ZERO
	desc.planet_radius = 256.0
	desc.local_center = Vector3(0.0, -256.0, 0.0)
	desc.seed = 1
	desc.is_star = true
	desc.star_spectral_type = StarSpectralType.Type.G
	desc.is_primary_star = true
	desc.star_color = Color(1.0, 0.95, 0.8)
	desc.star_light_energy = 2.2
	desc.atmosphere_color = Color(1.0, 0.9, 0.4, 1.0)
	desc.atmosphere_scale = 1.2
	desc.atmosphere_power = 2.0
	desc.atmosphere_intensity = 2.0
	return desc


# --- Inner rocky planets ---

static func _create_mercury(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_mercury"
	desc.display_name = "Mercury"
	desc.universe_position = Vector3(ORBIT_SCALE * 0.39, 0.0, 0.0)
	desc.planet_radius = 128.0
	desc.local_center = Vector3(0.0, -128.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "mercury")
	desc.gravity_multiplier = 0.38
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.NONE
	desc.terrain_height_scale = 8.0
	desc.elevation_noise_scale = 0.012
	desc.cave_threshold = 0.4
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.6, 0.55, 0.5, 0.3)
	desc.atmosphere_scale = 1.01
	desc.atmosphere_intensity = 0.1
	desc.cloud_scale = 1.0
	desc.cloud_coverage = 0.0
	desc.horizon_fog_max_density = 0.0
	return desc


static func _create_venus(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_venus"
	desc.display_name = "Venus"
	desc.universe_position = Vector3(ORBIT_SCALE * 0.72, 0.0, 0.0)
	desc.planet_radius = 480.0
	desc.local_center = Vector3(0.0, -480.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "venus")
	desc.gravity_multiplier = 0.9
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
	desc.terrain_height_scale = 12.0
	desc.elevation_noise_scale = 0.006
	desc.cave_threshold = 0.45
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.9, 0.75, 0.4, 1.0)
	desc.atmosphere_scale = 1.12
	desc.atmosphere_power = 2.5
	desc.atmosphere_intensity = 1.8
	desc.cloud_color = Color(0.85, 0.75, 0.5)
	desc.cloud_coverage = 0.9
	desc.cloud_sharpness = 1.5
	desc.horizon_fog_color = Color(0.8, 0.7, 0.4)
	desc.horizon_fog_max_density = 0.08
	return desc


static func _create_earth(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_earth"
	desc.display_name = "Earth"
	desc.universe_position = Vector3(ORBIT_SCALE * 1.0, 0.0, 0.0)
	desc.planet_radius = 512.0
	desc.local_center = Vector3(0.0, -512.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "earth")
	desc.gravity_multiplier = 1.0
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.BREATHABLE
	desc.terrain_height_scale = 16.0
	desc.elevation_noise_scale = 0.008
	desc.elevation_octaves = 5
	desc.detail_noise_scale = 0.03
	desc.detail_octaves = 3
	desc.cave_noise_scale = 0.04
	desc.cave_octaves = 4
	desc.cave_threshold = 0.35
	desc.sea_level_fraction = 0.3
	desc.atmosphere_color = Color(0.3, 0.6, 1.0, 1.0)
	desc.atmosphere_scale = 1.08
	desc.atmosphere_power = 3.5
	desc.atmosphere_intensity = 1.2
	desc.cloud_coverage = 0.45
	desc.horizon_fog_color = Color(0.55, 0.70, 0.90)
	desc.horizon_fog_max_density = 0.04
	return desc


static func _create_mars(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_mars"
	desc.display_name = "Mars"
	desc.universe_position = Vector3(ORBIT_SCALE * 1.52, 0.0, 0.0)
	desc.planet_radius = 320.0
	desc.local_center = Vector3(0.0, -320.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "mars")
	desc.gravity_multiplier = 0.38
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.THIN
	desc.terrain_height_scale = 20.0
	desc.elevation_noise_scale = 0.005
	desc.elevation_octaves = 4
	desc.cave_threshold = 0.3
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.85, 0.45, 0.25, 0.6)
	desc.atmosphere_scale = 1.03
	desc.atmosphere_power = 4.0
	desc.atmosphere_intensity = 0.4
	desc.cloud_color = Color(0.8, 0.6, 0.45)
	desc.cloud_coverage = 0.15
	desc.cloud_sharpness = 3.0
	desc.horizon_fog_color = Color(0.75, 0.50, 0.35)
	desc.horizon_fog_max_density = 0.015
	return desc


# --- Outer gas/ice giants ---

static func _create_jupiter(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_jupiter"
	desc.display_name = "Jupiter"
	desc.universe_position = Vector3(ORBIT_SCALE * 5.2, 0.0, 0.0)
	desc.planet_radius = 768.0
	desc.local_center = Vector3(0.0, -768.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "jupiter")
	desc.gravity_multiplier = 2.5
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
	desc.terrain_height_scale = 4.0
	desc.elevation_noise_scale = 0.003
	desc.elevation_octaves = 6
	desc.cave_threshold = 0.5
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.8, 0.65, 0.45, 1.0)
	desc.atmosphere_scale = 1.15
	desc.atmosphere_power = 2.0
	desc.atmosphere_intensity = 1.5
	desc.cloud_color = Color(0.75, 0.6, 0.4)
	desc.cloud_coverage = 0.95
	desc.cloud_sharpness = 1.0
	desc.cloud_rotation_speed = 0.08
	desc.horizon_fog_color = Color(0.7, 0.6, 0.45)
	desc.horizon_fog_max_density = 0.06
	return desc


static func _create_saturn(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_saturn"
	desc.display_name = "Saturn"
	desc.universe_position = Vector3(ORBIT_SCALE * 9.5, 0.0, 0.0)
	desc.planet_radius = 640.0
	desc.local_center = Vector3(0.0, -640.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "saturn")
	desc.gravity_multiplier = 1.07
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
	desc.terrain_height_scale = 3.0
	desc.elevation_noise_scale = 0.002
	desc.elevation_octaves = 6
	desc.cave_threshold = 0.5
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.85, 0.78, 0.55, 1.0)
	desc.atmosphere_scale = 1.12
	desc.atmosphere_power = 2.2
	desc.atmosphere_intensity = 1.3
	desc.cloud_color = Color(0.82, 0.75, 0.5)
	desc.cloud_coverage = 0.85
	desc.cloud_sharpness = 1.2
	desc.cloud_rotation_speed = 0.06
	desc.horizon_fog_color = Color(0.8, 0.72, 0.5)
	desc.horizon_fog_max_density = 0.05
	return desc


static func _create_uranus(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_uranus"
	desc.display_name = "Uranus"
	desc.universe_position = Vector3(ORBIT_SCALE * 19.2, 0.0, 0.0)
	desc.planet_radius = 448.0
	desc.local_center = Vector3(0.0, -448.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "uranus")
	desc.gravity_multiplier = 0.89
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
	desc.terrain_height_scale = 2.0
	desc.elevation_noise_scale = 0.002
	desc.elevation_octaves = 5
	desc.cave_threshold = 0.5
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.55, 0.82, 0.85, 1.0)
	desc.atmosphere_scale = 1.10
	desc.atmosphere_power = 3.0
	desc.atmosphere_intensity = 1.0
	desc.cloud_color = Color(0.5, 0.78, 0.8)
	desc.cloud_coverage = 0.6
	desc.cloud_sharpness = 2.0
	desc.cloud_rotation_speed = 0.04
	desc.horizon_fog_color = Color(0.5, 0.75, 0.8)
	desc.horizon_fog_max_density = 0.03
	return desc


static func _create_neptune(universe_seed: int) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"planet_neptune"
	desc.display_name = "Neptune"
	desc.universe_position = Vector3(ORBIT_SCALE * 30.0, 0.0, 0.0)
	desc.planet_radius = 432.0
	desc.local_center = Vector3(0.0, -432.0, 0.0)
	desc.seed = _hash_planet_seed(universe_seed, "neptune")
	desc.gravity_multiplier = 1.14
	desc.atmosphere_type = PlanetDescriptor.AtmosphereType.TOXIC
	desc.terrain_height_scale = 2.0
	desc.elevation_noise_scale = 0.002
	desc.elevation_octaves = 5
	desc.cave_threshold = 0.5
	desc.sea_level_fraction = 0.0
	desc.atmosphere_color = Color(0.25, 0.4, 0.85, 1.0)
	desc.atmosphere_scale = 1.10
	desc.atmosphere_power = 3.0
	desc.atmosphere_intensity = 1.1
	desc.cloud_color = Color(0.2, 0.35, 0.8)
	desc.cloud_coverage = 0.5
	desc.cloud_sharpness = 2.0
	desc.cloud_rotation_speed = 0.05
	desc.horizon_fog_color = Color(0.25, 0.4, 0.75)
	desc.horizon_fog_max_density = 0.03
	return desc


# --- Utility ---

# Deterministic hash combining the universe seed with a planet name
# to produce a unique per-planet seed.
static func _hash_planet_seed(universe_seed: int, planet_name: String) -> int:
	var combined := String.num_uint64(uint64_t(universe_seed)) + ":" + planet_name
	return combined.hash()


# Recompute the system bounding radius from the outermost planet position.
static func _recompute_system_radius(system: StarSystemDescriptor) -> void:
	var max_dist := 0.0
	for star in system.stars:
		var dist := star.universe_position.distance_to(system.universe_position)
		dist += star.planet_radius
		if dist > max_dist:
			max_dist = dist
	for planet in system.planets:
		var dist := planet.universe_position.distance_to(system.universe_position)
		dist += planet.planet_radius
		if dist > max_dist:
			max_dist = dist
	if max_dist < 1000.0:
		max_dist = 1000.0
	system.system_radius = max_dist
