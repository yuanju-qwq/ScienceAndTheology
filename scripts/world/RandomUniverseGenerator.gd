# RandomUniverseGenerator — procedurally generates a random universe layout.
# Uses the universe seed to deterministically create star system placeholders
# distributed across the universe. Each placeholder contains only position,
# type, and seed data; full star/planet generation happens on-demand via
# StarSystemGenerator.realize() when the player approaches.
#
# Also provides a legacy generate_flat() method that returns a single
# fully-realized system as a flat Array[PlanetDescriptor] for backward
# compatibility with the old UniverseManager API.
class_name RandomUniverseGenerator
extends RefCounted

# Minimum and maximum number of star systems in the universe.
const MIN_SYSTEMS := 5
const MAX_SYSTEMS := 20

# Minimum spacing between system barycenters (in universe-space units).
const MIN_SYSTEM_SPACING := 30000.0

# Maximum Y offset for system positions (slight vertical spread).
const MAX_SYSTEM_Y_OFFSET := 5000.0

# --- Legacy constants (kept for generate_flat compatibility) ---

const MIN_PLANETS := 3
const MAX_PLANETS := 12
const MIN_ORBIT_SPACING := 2000.0
const MIN_PLANET_RADIUS := 128.0
const MAX_PLANET_RADIUS := 800.0
const MIN_STAR_RADIUS := 200.0
const MAX_STAR_RADIUS := 400.0
const MIN_GRAVITY_MULT := 0.2
const MAX_GRAVITY_MULT := 2.5
const MIN_TERRAIN_HEIGHT := 4.0
const MAX_TERRAIN_HEIGHT := 24.0
const MAX_SEA_LEVEL := 0.5

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


# --- New API: system-based generation ---

# Generate a random universe as an array of star system placeholders.
# The first system (index 0) is always realized immediately so the player
# has a starting system with full detail. All other systems remain as
# placeholders until the player approaches them.
# Returns an Array of StarSystemDescriptor resources.
static func generate(universe_seed: int) -> Array[StarSystemDescriptor]:
	var rng := RandomNumberGenerator.new()
	rng.seed = universe_seed

	var system_count := rng.randi_range(MIN_SYSTEMS, MAX_SYSTEMS)
	var systems: Array[StarSystemDescriptor] = []

	for i in range(system_count):
		var pos := _generate_system_position(rng, i, systems)
		var sys := StarSystemGenerator.create_placeholder(i, universe_seed, pos)

		if i == 0:
			StarSystemGenerator.realize(sys)

		systems.append(sys)

	return systems


# Generate a position for a new system that maintains minimum spacing
# from all existing systems. Uses rejection sampling with a fallback
# that gradually increases the allowed distance.
static func _generate_system_position(rng: RandomNumberGenerator,
		system_index: int, existing: Array[StarSystemDescriptor]) -> Vector3:
	if system_index == 0:
		return Vector3.ZERO

	var max_attempts := 50
	for _attempt in range(max_attempts):
		var angle := rng.randf_range(0.0, TAU)
		var dist := rng.randf_range(MIN_SYSTEM_SPACING, MIN_SYSTEM_SPACING * 3.0)
		var offset_y := rng.randf_range(-MAX_SYSTEM_Y_OFFSET, MAX_SYSTEM_Y_OFFSET)
		var candidate := Vector3(cos(angle) * dist, offset_y, sin(angle) * dist)

		if system_index > 0:
			candidate += existing[0].universe_position

		var too_close := false
		for other in existing:
			if candidate.distance_to(other.universe_position) < MIN_SYSTEM_SPACING:
				too_close = true
				break

		if not too_close:
			return candidate

	var fallback_angle := rng.randf_range(0.0, TAU)
	var fallback_dist := MIN_SYSTEM_SPACING * (2.0 + float(system_index) * 0.5)
	var fallback_y := rng.randf_range(-MAX_SYSTEM_Y_OFFSET, MAX_SYSTEM_Y_OFFSET)
	var fallback_pos := Vector3(cos(fallback_angle) * fallback_dist, fallback_y, sin(fallback_angle) * fallback_dist)
	if system_index > 0:
		fallback_pos += existing[0].universe_position
	return fallback_pos


# --- Legacy API: flat planet list generation ---

# Generate a random universe with a single star and procedurally placed planets.
# Returns a flat Array of PlanetDescriptor resources.
# This is the original generation method, kept for backward compatibility
# and the "solar_system" mode fallback.
static func generate_flat(universe_seed: int) -> Array[PlanetDescriptor]:
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


# --- Legacy star generation ---

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


# --- Legacy planet generation ---

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
