# PlanetDescriptor — data class describing a single planet in the universe.
# Contains both universe-level placement (position, radius) and
# terrain-generation parameters. Each planet maps to a unique
# dimension_id used by the C++ WorldData / TerrainGenerator.
class_name PlanetDescriptor
extends Resource

# Unique dimension identifier used as the chunk storage key in C++ WorldData.
# Must match the dimension_id in PlanetConfig registered via GDTerrainContentRegistry.
@export var dimension_id: StringName = &"overworld"

# Human-readable display name for UI.
@export var display_name: String = "Planet"

# Position of the planet center in universe-space coordinates.
# This is the "big coordinate" used for inter-planet travel and LOD.
@export var universe_position: Vector3 = Vector3.ZERO

# Radius of the planet in voxel blocks.
# Determines the spherical clipping boundary and gravity range.
@export var planet_radius: float = 512.0

# Center of the planet in local voxel coordinates.
# The terrain generator uses this as the sphere center for chunk generation.
@export var local_center: Vector3 = Vector3(0.0, -512.0, 0.0)

# Per-planet seed for deterministic terrain generation.
# If 0, the universe seed will be hashed with dimension_id.
@export var seed: int = 0

# Gravity multiplier relative to the default GRAVITY_STRENGTH.
# 1.0 = standard gravity, 0.38 = Mars-like, 2.5 = super-Earth.
@export var gravity_multiplier: float = 1.0

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
	}


# Compute the gravity influence radius for this planet.
# Gravity extends beyond the surface; the default is 4x the planet radius
# (matching the existing V20 milestone).
func gravity_radius() -> float:
	return planet_radius * 4.0


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
