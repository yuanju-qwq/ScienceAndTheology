# RandomUniverseGenerator — procedurally generates a random universe layout.
# Uses the universe seed to deterministically create a star, planets
# with varied properties, and orbital positions. Produces an array
# of PlanetDescriptor resources compatible with UniverseManager.
class_name RandomUniverseGenerator
extends RefCounted

# Minimum and maximum number of planets (excluding the star).
const MIN_PLANETS := 3
const MAX_PLANETS := 12

# Minimum orbital spacing between planets (in universe-space units).
const MIN_ORBIT_SPACING := 2000.0

# Planet radius range (in voxel blocks).
const MIN_PLANET_RADIUS := 128.0
const MAX_PLANET_RADIUS := 800.0

# Star radius range.
const MIN_STAR_RADIUS := 200.0
const MAX_STAR_RADIUS := 400.0

# Gravity multiplier range.
const MIN_GRAVITY_MULT := 0.2
const MAX_GRAVITY_MULT := 2.5

# Terrain height scale range.
const MIN_TERRAIN_HEIGHT := 4.0
const MAX_TERRAIN_HEIGHT := 24.0

# Sea level fraction range (0 = no water, 0.5 = half terrain height).
const MAX_SEA_LEVEL := 0.5

# Atmosphere color palette — predefined hue ranges for variety.
const ATMO_PALETTES: Array[Color] = [
	Color(0.3, 0.6, 1.0, 1.0),
	Color(0.85, 0.45, 0.25, 0.6),
	Color(0.9, 0.75, 0.4, 1.0),
	Color(0.55, 0.82, 0.85, 1.0),
	Color(0.25, 0.4, 0.85, 1.0),
	Color(0.8, 0.65, 0.45, 1.0),
	Color(0.6, 0.55, 0.5, 0.3),
	Color(0.4, 0.8, 0.4, 0.8),
	Color(0.7, 0.3, 0.8, 0.7),
	Color(0.9, 0.9, 0.7, 1.0),
]


# Generate a random universe with a star and procedurally placed planets.
# Returns an Array of PlanetDescriptor resources.
static func generate(universe_seed: int) -> Array[PlanetDescriptor]:
	var rng := RandomNumberGenerator.new()
	rng.seed = universe_seed

	var planets: Array[PlanetDescriptor] = []

	planets.append(_create_star(rng))

	var planet_count := rng.randi_range(MIN_PLANETS, MAX_PLANETS)
	var orbit_distance := MIN_ORBIT_SPACING

	for i in range(planet_count):
		orbit_distance += rng.randf_range(MIN_ORBIT_SPACING * 0.8, MIN_ORBIT_SPACING * 1.5)
		var angle := rng.randf_range(0.0, TAU)
		var offset_y := rng.randf_range(-500.0, 500.0)
		var pos := Vector3(cos(angle) * orbit_distance, offset_y, sin(angle) * orbit_distance)
		planets.append(_create_planet(rng, i, pos))

	return planets


# --- Star generation ---

static func _create_star(rng: RandomNumberGenerator) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = &"star_primary"
	desc.display_name = "Star"
	desc.universe_position = Vector3.ZERO
	desc.planet_radius = rng.randf_range(MIN_STAR_RADIUS, MAX_STAR_RADIUS)
	desc.local_center = Vector3(0.0, -desc.planet_radius, 0.0)
	desc.seed = 1
	desc.is_star = true

	var hue := rng.randf_range(0.05, 0.15)
	desc.star_color = Color.from_hsv(hue, 0.6, 1.0)
	desc.atmosphere_color = Color.from_hsv(hue, 0.5, 1.0, 1.0)
	desc.atmosphere_scale = rng.randf_range(1.1, 1.3)
	desc.atmosphere_power = rng.randf_range(1.5, 3.0)
	desc.atmosphere_intensity = rng.randf_range(1.5, 2.5)
	desc.star_light_energy = rng.randf_range(1.5, 3.0)
	return desc


# --- Planet generation ---

static func _create_planet(rng: RandomNumberGenerator, index: int,
		universe_pos: Vector3) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"planet_%d" % index)
	desc.display_name = "Planet %d" % (index + 1)
	desc.universe_position = universe_pos
	desc.planet_radius = rng.randf_range(MIN_PLANET_RADIUS, MAX_PLANET_RADIUS)
	desc.local_center = Vector3(0.0, -desc.planet_radius, 0.0)
	desc.seed = rng.randi()
	desc.gravity_multiplier = rng.randf_range(MIN_GRAVITY_MULT, MAX_GRAVITY_MULT)

	# Terrain parameters — varied per planet.
	desc.terrain_height_scale = rng.randf_range(MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT)
	desc.elevation_noise_scale = rng.randf_range(0.002, 0.015)
	desc.elevation_octaves = rng.randi_range(3, 7)
	desc.detail_noise_scale = rng.randf_range(0.01, 0.06)
	desc.detail_octaves = rng.randi_range(2, 5)
	desc.cave_noise_scale = rng.randf_range(0.02, 0.06)
	desc.cave_octaves = rng.randi_range(3, 6)
	desc.cave_threshold = rng.randf_range(0.25, 0.5)

	# Sea level — some planets have water, some don't.
	if rng.randf() < 0.4:
		desc.sea_level_fraction = 0.0
	else:
		desc.sea_level_fraction = rng.randf_range(0.1, MAX_SEA_LEVEL)

	# Core / mantle ratios — some variation.
	desc.core_radius_ratio = rng.randf_range(0.03, 0.08)
	desc.mantle_radius_ratio = rng.randf_range(0.4, 0.6)
	desc.core_boundary_noise_scale = rng.randf_range(0.01, 0.04)
	desc.core_boundary_noise_octaves = rng.randi_range(2, 4)
	desc.core_boundary_noise_amplitude = rng.randf_range(0.1, 0.25)

	# Atmosphere — pick from palette with slight randomization.
	var palette_idx := rng.randi_range(0, ATMO_PALETTES.size() - 1)
	var base_atmo := ATMO_PALETTES[palette_idx]
	desc.atmosphere_color = Color(
		clampf(base_atmo.r + rng.randf_range(-0.1, 0.1), 0.0, 1.0),
		clampf(base_atmo.g + rng.randf_range(-0.1, 0.1), 0.0, 1.0),
		clampf(base_atmo.b + rng.randf_range(-0.1, 0.1), 0.0, 1.0),
		rng.randf_range(0.3, 1.0))
	desc.atmosphere_scale = rng.randf_range(1.01, 1.15)
	desc.atmosphere_power = rng.randf_range(2.0, 5.0)
	desc.atmosphere_intensity = rng.randf_range(0.2, 1.8)

	# Clouds — some planets have thick clouds, some have none.
	desc.cloud_coverage = rng.randf_range(0.0, 0.95)
	desc.cloud_sharpness = rng.randf_range(1.0, 4.0)
	desc.cloud_color = Color(
		rng.randf_range(0.7, 1.0),
		rng.randf_range(0.7, 1.0),
		rng.randf_range(0.7, 1.0))
	desc.cloud_rotation_speed = rng.randf_range(0.01, 0.1)

	# Horizon fog — thicker atmosphere = more fog.
	desc.horizon_fog_color = Color(
		desc.atmosphere_color.r * 0.8,
		desc.atmosphere_color.g * 0.8,
		desc.atmosphere_color.b * 0.8)
	desc.horizon_fog_max_density = desc.atmosphere_intensity * 0.04
	desc.horizon_fog_max_distance = 100.0 + desc.planet_radius * 0.2

	return desc
