# StarSystemGenerator — realizes placeholder star systems into full detail.
# Given a StarSystemDescriptor in "placeholder" state, generates all star
# and planet descriptors based on the system type and seed. The generation
# is fully deterministic: the same seed always produces the same system.
#
# System type determines the star configuration:
#   SINGLE_STAR — 1 main-sequence star, 3-12 planets
#   BINARY      — 1 primary + 1 companion, 3-10 planets (orbit primary)
#   TRINARY     — 1 primary + 2 companions, 2-8 planets (orbit primary)
#   STARLESS    — 0 stars, 1-3 rogue planets
#   REMNANT     — 1 remnant star (WD/NS/BH), 0-4 planets
class_name StarSystemGenerator
extends RefCounted

# --- Planet count ranges per system type ---

const PLANETS_SINGLE_MIN := 3
const PLANETS_SINGLE_MAX := 12
const PLANETS_BINARY_MIN := 3
const PLANETS_BINARY_MAX := 10
const PLANETS_TRINARY_MIN := 2
const PLANETS_TRINARY_MAX := 8
const PLANETS_STARLESS_MIN := 1
const PLANETS_STARLESS_MAX := 3
const PLANETS_REMNANT_MIN := 0
const PLANETS_REMNANT_MAX := 4

# --- Orbital parameters ---

# Base orbital spacing in universe-space units.
const BASE_ORBIT_SPACING := 2000.0

# Companion star orbital distance multiplier (relative to outermost planet).
const COMPANION_ORBIT_MULT := 3.0

# Y-axis offset range for orbital planes (slight inclination).
const ORBIT_Y_OFFSET := 500.0

# --- Planet property ranges ---

const MIN_PLANET_RADIUS := 128.0
const MAX_PLANET_RADIUS := 800.0
const MIN_GRAVITY_MULT := 0.2
const MAX_GRAVITY_MULT := 2.5
const MIN_TERRAIN_HEIGHT := 4.0
const MAX_TERRAIN_HEIGHT := 24.0
const MAX_SEA_LEVEL := 0.5

# Atmosphere color palette — same as RandomUniverseGenerator for consistency.
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

# System type generation weights (for placeholder creation).
const TYPE_WEIGHTS: Dictionary = {
	StarSystemDescriptor.TYPE_SINGLE_STAR: 55.0,
	StarSystemDescriptor.TYPE_BINARY: 25.0,
	StarSystemDescriptor.TYPE_TRINARY: 8.0,
	StarSystemDescriptor.TYPE_STARLESS: 7.0,
	StarSystemDescriptor.TYPE_REMNANT: 5.0,
}


# --- Placeholder generation ---

# Create a placeholder StarSystemDescriptor with position and type determined
# by the system index and universe seed. No star/planet data is generated.
static func create_placeholder(system_index: int, universe_seed: int,
		universe_position: Vector3) -> StarSystemDescriptor:
	var sys := StarSystemDescriptor.new()
	sys.system_id = StringName(&"sys_%d" % system_index)
	sys.universe_position = universe_position
	sys.system_seed = _hash_system_seed(universe_seed, system_index)
	sys.generation_state = StarSystemDescriptor.STATE_PLACEHOLDER

	var rng := RandomNumberGenerator.new()
	rng.seed = sys.system_seed
	sys.system_type = _random_system_type(rng)
	sys.system_radius = _estimate_system_radius(sys.system_type, rng)

	return sys


# Pick a random system type using weighted probability.
static func _random_system_type(rng: RandomNumberGenerator) -> String:
	var total_weight := 0.0
	for w in TYPE_WEIGHTS.values():
		total_weight += w
	var roll := rng.randf() * total_weight
	var accumulated := 0.0
	for type_key in TYPE_WEIGHTS.keys():
		accumulated += TYPE_WEIGHTS[type_key]
		if roll <= accumulated:
			return type_key
	return StarSystemDescriptor.TYPE_SINGLE_STAR


# Estimate system bounding radius for a placeholder system.
# This is a rough estimate; the exact radius is computed on realization.
static func _estimate_system_radius(system_type: String, rng: RandomNumberGenerator) -> float:
	var max_planets := PLANETS_SINGLE_MAX
	match system_type:
		StarSystemDescriptor.TYPE_BINARY:
			max_planets = PLANETS_BINARY_MAX
		StarSystemDescriptor.TYPE_TRINARY:
			max_planets = PLANETS_TRINARY_MAX
		StarSystemDescriptor.TYPE_STARLESS:
			max_planets = PLANETS_STARLESS_MAX
		StarSystemDescriptor.TYPE_REMNANT:
			max_planets = PLANETS_REMNANT_MAX
	var outermost_orbit := BASE_ORBIT_SPACING * (1.0 + float(max_planets) * 1.2)
	if system_type == StarSystemDescriptor.TYPE_BINARY:
		outermost_orbit *= COMPANION_ORBIT_MULT * 0.5
	elif system_type == StarSystemDescriptor.TYPE_TRINARY:
		outermost_orbit *= COMPANION_ORBIT_MULT * 0.7
	return outermost_orbit


# --- System realization ---

# Realize a placeholder system: generate all star and planet descriptors.
# The system_seed is used to create a deterministic RNG, so the same
# placeholder always produces the same realized system.
# Returns the modified StarSystemDescriptor (generation_state = "realized").
static func realize(system: StarSystemDescriptor) -> StarSystemDescriptor:
	if system.is_realized():
		return system

	var rng := RandomNumberGenerator.new()
	rng.seed = system.system_seed

	match system.system_type:
		StarSystemDescriptor.TYPE_SINGLE_STAR:
			_realize_single_star(system, rng)
		StarSystemDescriptor.TYPE_BINARY:
			_realize_binary(system, rng)
		StarSystemDescriptor.TYPE_TRINARY:
			_realize_trinary(system, rng)
		StarSystemDescriptor.TYPE_STARLESS:
			_realize_starless(system, rng)
		StarSystemDescriptor.TYPE_REMNANT:
			_realize_remnant(system, rng)
		_:
			_realize_single_star(system, rng)

	system.generation_state = StarSystemDescriptor.STATE_REALIZED
	_recompute_system_radius(system)
	return system


# --- SINGLE_STAR realization ---

static func _realize_single_star(system: StarSystemDescriptor,
		rng: RandomNumberGenerator) -> void:
	var primary := _create_star_body(rng, system.system_id, 0, true)
	primary.universe_position = system.universe_position
	system.stars.append(primary)

	var planet_count := rng.randi_range(PLANETS_SINGLE_MIN, PLANETS_SINGLE_MAX)
	_generate_planets(system, rng, planet_count, primary.universe_position)


# --- BINARY realization ---

static func _realize_binary(system: StarSystemDescriptor,
		rng: RandomNumberGenerator) -> void:
	var primary := _create_star_body(rng, system.system_id, 0, true)
	primary.universe_position = system.universe_position
	system.stars.append(primary)

	var companion := _create_companion_star(rng, system.system_id, 1, primary)
	companion.universe_position = _compute_companion_position(
		primary.universe_position, rng, primary.planet_radius)
	system.stars.append(companion)

	var planet_count := rng.randi_range(PLANETS_BINARY_MIN, PLANETS_BINARY_MAX)
	_generate_planets(system, rng, planet_count, primary.universe_position)


# --- TRINARY realization ---

static func _realize_trinary(system: StarSystemDescriptor,
		rng: RandomNumberGenerator) -> void:
	var primary := _create_star_body(rng, system.system_id, 0, true)
	primary.universe_position = system.universe_position
	system.stars.append(primary)

	var companion_a := _create_companion_star(rng, system.system_id, 1, primary)
	companion_a.universe_position = _compute_companion_position(
		primary.universe_position, rng, primary.planet_radius)
	system.stars.append(companion_a)

	var companion_b := _create_companion_star(rng, system.system_id, 2, companion_a)
	companion_b.universe_position = _compute_companion_position(
		companion_a.universe_position, rng, companion_a.planet_radius)
	companion_b.is_primary_star = false
	system.stars.append(companion_b)

	var planet_count := rng.randi_range(PLANETS_TRINARY_MIN, PLANETS_TRINARY_MAX)
	_generate_planets(system, rng, planet_count, primary.universe_position)


# --- STARLESS realization ---

static func _realize_starless(system: StarSystemDescriptor,
		rng: RandomNumberGenerator) -> void:
	var planet_count := rng.randi_range(PLANETS_STARLESS_MIN, PLANETS_STARLESS_MAX)
	_generate_starless_planets(system, rng, planet_count)


# --- REMNANT realization ---

static func _realize_remnant(system: StarSystemDescriptor,
		rng: RandomNumberGenerator) -> void:
	var remnant := _create_remnant_body(rng, system.system_id, 0)
	remnant.universe_position = system.universe_position
	system.stars.append(remnant)

	var planet_count := rng.randi_range(PLANETS_REMNANT_MIN, PLANETS_REMNANT_MAX)
	_generate_planets(system, rng, planet_count, remnant.universe_position)


# --- Star body creation ---

# Create a main-sequence star body with a random spectral type.
static func _create_star_body(rng: RandomNumberGenerator, system_id: StringName,
		star_index: int, is_primary: bool) -> PlanetDescriptor:
	var spectral_type := StarSpectralType.random_type(rng)
	var radius := StarSpectralType.random_radius(spectral_type, rng)
	var color := StarSpectralType.get_color(spectral_type)
	var light_energy := StarSpectralType.random_light_energy(spectral_type, rng)

	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"star_%d_sys_%s" % [star_index, String(system_id)])
	desc.display_name = StarSpectralType.get_display_name(spectral_type)
	desc.planet_radius = radius
	desc.local_center = Vector3(0.0, -radius, 0.0)
	desc.seed = rng.randi()
	desc.is_star = true
	desc.system_id = system_id
	desc.star_spectral_type = spectral_type
	desc.is_primary_star = is_primary
	desc.star_color = color
	desc.star_light_energy = light_energy
	desc.atmosphere_color = Color(color.r, color.g, color.b, 1.0)
	desc.atmosphere_scale = rng.randf_range(1.1, 1.3)
	desc.atmosphere_power = rng.randf_range(1.5, 3.0)
	desc.atmosphere_intensity = rng.randf_range(1.5, 2.5)
	return desc


# Create a companion star (always a cooler type than or equal to the primary).
# Companions are typically K or M type for realistic binary systems.
static func _create_companion_star(rng: RandomNumberGenerator, system_id: StringName,
		star_index: int, primary: PlanetDescriptor) -> PlanetDescriptor:
	var companion_type := _random_companion_type(rng, primary.star_spectral_type)
	var radius := StarSpectralType.random_radius(companion_type, rng)
	var color := StarSpectralType.get_color(companion_type)
	var light_energy := StarSpectralType.random_light_energy(companion_type, rng)

	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"star_%d_sys_%s" % [star_index, String(system_id)])
	desc.display_name = StarSpectralType.get_display_name(companion_type)
	desc.planet_radius = radius
	desc.local_center = Vector3(0.0, -radius, 0.0)
	desc.seed = rng.randi()
	desc.is_star = true
	desc.system_id = system_id
	desc.star_spectral_type = companion_type
	desc.is_primary_star = false
	desc.star_color = color
	desc.star_light_energy = light_energy
	desc.atmosphere_color = Color(color.r, color.g, color.b, 1.0)
	desc.atmosphere_scale = rng.randf_range(1.05, 1.2)
	desc.atmosphere_power = rng.randf_range(1.5, 3.0)
	desc.atmosphere_intensity = rng.randf_range(1.0, 2.0)
	return desc


# Pick a companion spectral type that is cooler than or equal to the primary.
# This follows the empirical rule that companions in binary systems tend
# to be similar to or cooler than the primary.
static func _random_companion_type(rng: RandomNumberGenerator,
		primary_type: int) -> int:
	var min_type := primary_type
	if StarSpectralType.is_main_sequence(primary_type):
		if primary_type < StarSpectralType.Type.K:
			min_type = StarSpectralType.Type.K
	var max_type := StarSpectralType.Type.M
	if min_type > max_type:
		min_type = max_type
	return rng.randi_range(min_type, max_type)


# Create a remnant star body (WD, NS, or BH).
static func _create_remnant_body(rng: RandomNumberGenerator, system_id: StringName,
		star_index: int) -> PlanetDescriptor:
	var remnant_roll := rng.randf()
	var spectral_type: int
	if remnant_roll < 0.5:
		spectral_type = StarSpectralType.Type.WD
	elif remnant_roll < 0.85:
		spectral_type = StarSpectralType.Type.NS
	else:
		spectral_type = StarSpectralType.Type.BH

	var radius := StarSpectralType.random_radius(spectral_type, rng)
	var color := StarSpectralType.get_color(spectral_type)
	var light_energy := StarSpectralType.random_light_energy(spectral_type, rng)

	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"star_%d_sys_%s" % [star_index, String(system_id)])
	desc.display_name = StarSpectralType.get_display_name(spectral_type)
	desc.planet_radius = radius
	desc.local_center = Vector3(0.0, -radius, 0.0)
	desc.seed = rng.randi()
	desc.is_star = true
	desc.system_id = system_id
	desc.star_spectral_type = spectral_type
	desc.is_primary_star = true
	desc.star_color = color
	desc.star_light_energy = light_energy
	desc.atmosphere_color = Color(color.r, color.g, color.b, 1.0)
	desc.atmosphere_scale = 1.0
	desc.atmosphere_power = 1.0
	desc.atmosphere_intensity = 0.0
	return desc


# --- Companion star positioning ---

# Compute the universe position of a companion star.
# Companions orbit at a distance proportional to the primary star's radius,
# placed at a random angle in the orbital plane.
static func _compute_companion_position(center: Vector3,
		rng: RandomNumberGenerator, primary_radius: float) -> Vector3:
	var orbit_distance := primary_radius * COMPANION_ORBIT_MULT
	orbit_distance = maxf(orbit_distance, BASE_ORBIT_SPACING * 2.0)
	var angle := rng.randf_range(0.0, TAU)
	var offset_y := rng.randf_range(-ORBIT_Y_OFFSET, ORBIT_Y_OFFSET)
	return center + Vector3(cos(angle) * orbit_distance, offset_y, sin(angle) * orbit_distance)


# --- Planet generation ---

# Generate planets orbiting a center point (primary star or remnant).
# Planets are placed at increasing orbital distances with random angles.
# The habitable-zone distance is influenced by the primary star's spectral type.
static func _generate_planets(system: StarSystemDescriptor,
		rng: RandomNumberGenerator, planet_count: int,
		orbit_center: Vector3) -> void:
	var hz_mult := 1.0
	var primary := system.get_primary_star()
	if primary != null:
		hz_mult = StarSpectralType.get_hz_multiplier(primary.star_spectral_type)
	if hz_mult < 0.1:
		hz_mult = 1.0

	var orbit_distance := BASE_ORBIT_SPACING * hz_mult

	for i in range(planet_count):
		orbit_distance += rng.randf_range(
			BASE_ORBIT_SPACING * 0.8, BASE_ORBIT_SPACING * 1.5)
		var angle := rng.randf_range(0.0, TAU)
		var offset_y := rng.randf_range(-ORBIT_Y_OFFSET, ORBIT_Y_OFFSET)
		var pos := orbit_center + Vector3(
			cos(angle) * orbit_distance,
			offset_y,
			sin(angle) * orbit_distance)
		var planet := _create_planet_body(rng, system.system_id, i, pos)
		system.planets.append(planet)


# Generate rogue planets for a starless system.
# These are placed near the system barycenter with no central star.
static func _generate_starless_planets(system: StarSystemDescriptor,
		rng: RandomNumberGenerator, planet_count: int) -> void:
	var orbit_distance := BASE_ORBIT_SPACING * 0.5

	for i in range(planet_count):
		orbit_distance += rng.randf_range(
			BASE_ORBIT_SPACING * 0.5, BASE_ORBIT_SPACING * 1.0)
		var angle := rng.randf_range(0.0, TAU)
		var offset_y := rng.randf_range(-ORBIT_Y_OFFSET * 0.5, ORBIT_Y_OFFSET * 0.5)
		var pos := system.universe_position + Vector3(
			cos(angle) * orbit_distance,
			offset_y,
			sin(angle) * orbit_distance)
		var planet := _create_starless_planet_body(rng, system.system_id, i, pos)
		system.planets.append(planet)


# --- Planet body creation ---

# Create a standard planet body with randomized terrain and atmosphere.
static func _create_planet_body(rng: RandomNumberGenerator, system_id: StringName,
		index: int, universe_pos: Vector3) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"planet_%d_sys_%s" % [index, String(system_id)])
	desc.display_name = "Planet %d" % (index + 1)
	desc.universe_position = universe_pos
	desc.planet_radius = rng.randf_range(MIN_PLANET_RADIUS, MAX_PLANET_RADIUS)
	desc.local_center = Vector3(0.0, -desc.planet_radius, 0.0)
	desc.seed = rng.randi()
	desc.system_id = system_id
	desc.gravity_multiplier = rng.randf_range(MIN_GRAVITY_MULT, MAX_GRAVITY_MULT)

	# Terrain parameters.
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

	# Core / mantle ratios.
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

	# Clouds.
	desc.cloud_coverage = rng.randf_range(0.0, 0.95)
	desc.cloud_sharpness = rng.randf_range(1.0, 4.0)
	desc.cloud_color = Color(
		rng.randf_range(0.7, 1.0),
		rng.randf_range(0.7, 1.0),
		rng.randf_range(0.7, 1.0))
	desc.cloud_rotation_speed = rng.randf_range(0.01, 0.1)

	# Horizon fog.
	desc.horizon_fog_color = Color(
		desc.atmosphere_color.r * 0.8,
		desc.atmosphere_color.g * 0.8,
		desc.atmosphere_color.b * 0.8)
	desc.horizon_fog_max_density = desc.atmosphere_intensity * 0.04
	desc.horizon_fog_max_distance = 100.0 + desc.planet_radius * 0.2

	return desc


# Create a rogue (starless) planet body.
# These are colder, have no sea level, and dimmer atmospheres.
static func _create_starless_planet_body(rng: RandomNumberGenerator,
		system_id: StringName, index: int, universe_pos: Vector3) -> PlanetDescriptor:
	var desc := PlanetDescriptor.new()
	desc.dimension_id = StringName(&"planet_%d_sys_%s" % [index, String(system_id)])
	desc.display_name = "Rogue Planet %d" % (index + 1)
	desc.universe_position = universe_pos
	desc.planet_radius = rng.randf_range(MIN_PLANET_RADIUS, MAX_PLANET_RADIUS * 0.6)
	desc.local_center = Vector3(0.0, -desc.planet_radius, 0.0)
	desc.seed = rng.randi()
	desc.system_id = system_id
	desc.gravity_multiplier = rng.randf_range(MIN_GRAVITY_MULT, 1.5)

	# Terrain — more rugged, no water.
	desc.terrain_height_scale = rng.randf_range(8.0, MAX_TERRAIN_HEIGHT)
	desc.elevation_noise_scale = rng.randf_range(0.003, 0.015)
	desc.elevation_octaves = rng.randi_range(4, 7)
	desc.detail_noise_scale = rng.randf_range(0.01, 0.06)
	desc.detail_octaves = rng.randi_range(2, 5)
	desc.cave_noise_scale = rng.randf_range(0.02, 0.06)
	desc.cave_octaves = rng.randi_range(3, 6)
	desc.cave_threshold = rng.randf_range(0.25, 0.5)
	desc.sea_level_fraction = 0.0

	# Core / mantle.
	desc.core_radius_ratio = rng.randf_range(0.03, 0.08)
	desc.mantle_radius_ratio = rng.randf_range(0.4, 0.6)
	desc.core_boundary_noise_scale = rng.randf_range(0.01, 0.04)
	desc.core_boundary_noise_octaves = rng.randi_range(2, 4)
	desc.core_boundary_noise_amplitude = rng.randf_range(0.1, 0.25)

	# Atmosphere — very thin or absent, cold colors.
	desc.atmosphere_color = Color(
		rng.randf_range(0.1, 0.3),
		rng.randf_range(0.15, 0.35),
		rng.randf_range(0.3, 0.6),
		rng.randf_range(0.1, 0.4))
	desc.atmosphere_scale = rng.randf_range(1.0, 1.05)
	desc.atmosphere_power = rng.randf_range(3.0, 6.0)
	desc.atmosphere_intensity = rng.randf_range(0.05, 0.4)

	# Clouds — sparse or none.
	desc.cloud_coverage = rng.randf_range(0.0, 0.3)
	desc.cloud_sharpness = rng.randf_range(2.0, 5.0)
	desc.cloud_color = Color(0.6, 0.6, 0.7)
	desc.cloud_rotation_speed = rng.randf_range(0.005, 0.03)

	# Horizon fog — minimal.
	desc.horizon_fog_color = Color(
		desc.atmosphere_color.r * 0.7,
		desc.atmosphere_color.g * 0.7,
		desc.atmosphere_color.b * 0.7)
	desc.horizon_fog_max_density = desc.atmosphere_intensity * 0.02
	desc.horizon_fog_max_distance = 80.0 + desc.planet_radius * 0.15

	return desc


# --- System radius computation ---

# Recompute the system bounding radius from the outermost body position.
# This replaces the placeholder estimate with an exact value.
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


# --- Seed hashing ---

# Deterministic hash combining the universe seed with a system index
# to produce a unique per-system seed.
static func _hash_system_seed(universe_seed: int, system_index: int) -> int:
	var combined := String.num_uint64(uint64_t(universe_seed)) + ":sys:" + String.num_int64(system_index)
	return combined.hash()
