# PlanetDescriptor — data class describing a single planet in the universe.
# Contains both universe-level placement (position, radius) and
# terrain-generation parameters. Each planet maps to a unique
# dimension_id used by the C++ WorldData / TerrainGenerator.
class_name PlanetDescriptor
extends Resource

# --- Atmosphere type enum ---

# Determines the gameplay effects of a planet's atmosphere.
enum AtmosphereType {
	NONE = 0,       # Vacuum — no atmosphere (e.g., Mercury, asteroids).
	THIN = 1,       # Thin atmosphere — oxygen mask required (e.g., Mars).
	BREATHABLE = 2, # Breathable — safe without equipment (e.g., Earth).
	TOXIC = 3,      # Toxic — continuous damage without suit (e.g., Venus).
	CORROSIVE = 4,  # Corrosive — damage + equipment degradation (e.g., acid world).
}

# Unique dimension identifier used as the chunk storage key in C++ WorldData.
# Must match the dimension_id in PlanetConfig registered via GDTerrainContentRegistry.
@export var dimension_id: StringName = &"overworld"

# Human-readable display name for UI.
@export var display_name: String = "Planet"

# Position of the planet center in universe-space coordinates.
# This is the "big coordinate" used for inter-planet travel and LOD.
@export var universe_position: Vector3 = Vector3.ZERO

# Radius from the planet center to the average surface in voxel blocks.
# This is the real gameplay/topology radius: it defines surface curvature,
# around-the-world circumference, spherical terrain clipping, and the
# center-to-surface mining distance. Do not use this value as the distance
# from surface to space; surface altitude bands below define that separately.
@export var planet_radius: float = 512.0

# Center of the planet in local voxel coordinates.
# The terrain generator uses this as the sphere center for chunk generation.
@export var local_center: Vector3 = Vector3(0.0, -512.0, 0.0)

# Surface-relative atmosphere height. This is the visual/environmental air band
# above the average surface, not the planet's center-to-surface radius.
@export var atmosphere_height: float = 512.0

# Surface-relative altitude where gameplay should switch to space rules/visuals.
# Players can build upward into this band without travelling another planet_radius.
@export var space_start_altitude: float = 2048.0

# Surface-relative altitude where the planet's gravity influence ends.
# gravity_radius() returns planet_radius + this value.
@export var gravity_influence_altitude: float = 2048.0

# Active shell streaming hints. These are intentionally surface-relative depths;
# they describe the real-time loaded band, not the planet's total mineable depth.
@export var active_shell_above: float = 128.0
@export var active_shell_below: float = 256.0

# Per-planet seed for deterministic terrain generation.
# If 0, the universe seed will be hashed with dimension_id.
@export var seed: int = 0

# Gravity multiplier relative to the default GRAVITY_STRENGTH.
# 1.0 = standard gravity, 0.38 = Mars-like, 2.5 = super-Earth.
@export var gravity_multiplier: float = 1.0

# Atmosphere gameplay type — determines environmental hazards.
# See AtmosphereType enum for details.
@export var atmosphere_type: AtmosphereType = AtmosphereType.BREATHABLE

# --- Atmosphere hazard damage rates ---

# Damage per second when in a toxic atmosphere without protection.
@export var toxic_damage_per_sec: float = 5.0
# Damage per second when in a corrosive atmosphere without protection.
@export var corrosive_damage_per_sec: float = 8.0
# Damage per second when in a vacuum/thin atmosphere without oxygen supply.
@export var vacuum_damage_per_sec: float = 3.0

# Atmosphere visual properties.
@export var atmosphere_color := Color(0.3, 0.6, 1.0, 1.0)
@export var atmosphere_scale := 1.08
@export var atmosphere_power := 3.5
@export var atmosphere_intensity := 1.2

# Cloud visual properties.
@export var cloud_scale := 1.03
@export var cloud_coverage := 0.45
@export var cloud_sharpness := 2.5
@export var cloud_color := Color(0.95, 0.95, 0.97, 1.0)
@export var cloud_rotation_speed := 0.02

# Terrain generation parameters (passed to PlanetConfig in C++).
@export var terrain_height_scale: float = 16.0
@export var elevation_noise_scale: float = 0.008
@export var elevation_octaves: int = 5
@export var detail_noise_scale: float = 0.03
@export var detail_octaves: int = 3
@export var cave_noise_scale: float = 0.04
@export var cave_octaves: int = 4
@export var cave_threshold: float = 0.35
@export var sea_level_fraction: float = 0.3
@export var core_radius_ratio: float = 0.05
@export var mantle_radius_ratio: float = 0.5
@export var core_boundary_noise_scale: float = 0.02
@export var core_boundary_noise_octaves: int = 3
@export var core_boundary_noise_amplitude: float = 0.15

# Horizon fog properties (per-planet atmosphere density).
@export var horizon_fog_color := Color(0.55, 0.70, 0.90, 1.0)
@export var horizon_fog_max_density: float = 0.04
@export var horizon_fog_max_distance: float = 200.0

# Whether this planet is a star (emissive, no terrain, no landing).
@export var is_star: bool = false

# Star emissive color (only used when is_star = true).
@export var star_color := Color(1.0, 0.95, 0.8)

# Star light energy (only used when is_star = true).
@export var star_light_energy: float = 2.2

# The star system this body belongs to.
# Matches StarSystemDescriptor.system_id.
@export var system_id: StringName = &""

# Spectral type of this star (StarSpectralType.Type enum value).
# Only used when is_star = true. Default is G (Sun-like).
@export var star_spectral_type: int = 5

# Whether this star is the primary star in its system.
# Only used when is_star = true.
@export var is_primary_star: bool = false


# Convert this descriptor to a Dictionary suitable for
# GDTerrainContentRegistry.register_planet_config().
func to_planet_config_dict() -> Dictionary:
	return {
		"dimension": String(dimension_id),
		"planet_radius": planet_radius,
		"center_x": local_center.x,
		"center_y": local_center.y,
		"center_z": local_center.z,
		"terrain_height_scale": terrain_height_scale,
		"elevation_noise_scale": elevation_noise_scale,
		"elevation_octaves": elevation_octaves,
		"detail_noise_scale": detail_noise_scale,
		"detail_octaves": detail_octaves,
		"cave_noise_scale": cave_noise_scale,
		"cave_octaves": cave_octaves,
		"cave_threshold": cave_threshold,
		"sea_level_fraction": sea_level_fraction,
		"core_radius_ratio": core_radius_ratio,
		"mantle_radius_ratio": mantle_radius_ratio,
		"core_boundary_noise_scale": core_boundary_noise_scale,
		"core_boundary_noise_octaves": core_boundary_noise_octaves,
		"core_boundary_noise_amplitude": core_boundary_noise_amplitude,
		"atmosphere_type": atmosphere_type,
	}


# Compute the radius of the visual/environmental atmosphere shell.
func atmosphere_radius() -> float:
	return planet_radius + atmosphere_height


# Compute the radius where surface gameplay should switch to space rules.
func space_start_radius() -> float:
	return planet_radius + space_start_altitude


# Compute the gravity influence radius for this planet.
# Gravity extends beyond the surface by a fixed altitude band instead of a
# planet-radius multiplier, so large planets do not require flying hundreds of
# thousands of blocks before reaching space.
func gravity_radius() -> float:
	return planet_radius + gravity_influence_altitude


# Compute surface-relative altitude for a position against the given center.
func surface_altitude_from_center(position: Vector3, center: Vector3) -> float:
	return position.distance_to(center) - planet_radius


# Compute surface-relative altitude for an active-planet local-space position.
func local_surface_altitude_at(position: Vector3) -> float:
	return surface_altitude_from_center(position, local_center)


# Compute surface-relative altitude for a universe-space position.
func universe_surface_altitude_at(position: Vector3) -> float:
	return surface_altitude_from_center(position, universe_position)


# Check whether a local-space position is inside the active shell streaming band.
# Deep underground outside this band should be generated on demand.
func is_in_active_shell_local(position: Vector3) -> bool:
	var altitude := local_surface_altitude_at(position)
	return altitude >= -active_shell_below and altitude <= active_shell_above


# Check whether a given universe-space position is within the
# gravity influence zone of this body.
# Both planets and stars exert gravity; stars are not landable
# but still pull the player (e.g. black hole gravity slingshot).
func is_in_gravity_range(position: Vector3) -> bool:
	return position.distance_to(universe_position) <= gravity_radius()


# Compute the gravity direction at a given universe-space position.
# Returns Vector3.ZERO if the position is outside gravity range.
func gravity_direction_at(position: Vector3) -> Vector3:
	var dist := position.distance_to(universe_position)
	if dist > gravity_radius() or dist < 0.001:
		return Vector3.ZERO
	return (universe_position - position).normalized()


# Compute the LOD level at a given universe-space position
# using the C++ GDPlanetLod helper.
func lod_level_at(position: Vector3) -> int:
	return GDPlanetLod.compute_lod_level(position, universe_position, planet_radius)


# Compute the surface distance (distance from position to planet surface).
# Negative means the position is inside the planet.
func surface_distance_at(position: Vector3) -> float:
	return GDPlanetLod.compute_surface_distance(position, universe_position, planet_radius)
