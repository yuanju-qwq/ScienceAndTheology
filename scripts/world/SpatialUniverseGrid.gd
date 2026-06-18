# SpatialUniverseGrid — deterministic spatial hash grid for infinite universe generation.
# Divides the XZ plane into cells of CELL_SIZE × CELL_SIZE. Each cell is
# assigned a deterministic hash value from its grid coordinates and the
# universe seed. If the hash value falls below the density threshold, a
# star system placeholder is placed in that cell.
#
# This enables on-demand generation of system placeholders around the
# player's position without pre-generating the entire universe. The same
# seed always produces the same layout, so the universe is reproducible
# across sessions and save/load cycles.
#
# Usage:
#   var grid := SpatialUniverseGrid.new()
#   grid.configure(universe_seed, density)
#   var cells := grid.get_cells_around(player_position, radius)
#   for cell in cells:
#       var sys := grid.create_placeholder_for_cell(cell)
#       systems.append(sys)
class_name SpatialUniverseGrid
extends RefCounted

# Size of each grid cell in universe-space units (XZ plane).
# Should be >= MIN_SYSTEM_SPACING to guarantee minimum distance between systems.
const CELL_SIZE := 30000.0

# Maximum Y offset for system positions (slight vertical spread).
const MAX_SYSTEM_Y_OFFSET := 5000.0

# Default density: fraction of cells that contain a star system (0.0 - 1.0).
const DEFAULT_DENSITY := 0.3

# Minimum density to ensure at least some systems exist.
const MIN_DENSITY := 0.05

# Maximum density to avoid overcrowding.
const MAX_DENSITY := 0.8

# The universe seed used for deterministic hashing.
var _universe_seed: int = 0

# Fraction of cells that contain a star system.
var _density: float = DEFAULT_DENSITY


# --- Configuration ---

# Configure the grid with a universe seed and optional density.
# density: fraction of cells that contain a star system (clamped to [MIN_DENSITY, MAX_DENSITY]).
func configure(universe_seed: int, density: float = DEFAULT_DENSITY) -> void:
	_universe_seed = universe_seed
	_density = clampf(density, MIN_DENSITY, MAX_DENSITY)


# --- Cell queries ---

# Convert a universe-space XZ position to grid cell coordinates.
func position_to_cell(pos: Vector3) -> Vector2i:
	return Vector2i(
		 int(floorf(pos.x / CELL_SIZE)),
		 int(floorf(pos.z / CELL_SIZE))
	)


# Get the center position of a cell in universe-space.
func cell_center(cell: Vector2i) -> Vector3:
	return Vector3(
		(float(cell.x) + 0.5) * CELL_SIZE,
		0.0,
		(float(cell.y) + 0.5) * CELL_SIZE
	)


# Check whether a cell contains a star system (deterministic).
func cell_has_system(cell: Vector2i) -> bool:
	return _cell_hash(cell) < _density


# Get the system type for a cell (deterministic).
# Returns one of the StarSystemDescriptor TYPE_* constants.
func cell_system_type(cell: Vector2i) -> String:
	var rng := _cell_rng(cell, 1)
	return _random_system_type(rng)


# Get the estimated system radius for a cell (deterministic).
func cell_system_radius(cell: Vector2i) -> float:
	var rng := _cell_rng(cell, 2)
	var sys_type := cell_system_type(cell)
	return _estimate_system_radius(sys_type, rng)


# Get the precise universe position for a system in a cell (deterministic).
# The position is the cell center plus a small offset to avoid grid alignment.
func cell_system_position(cell: Vector2i) -> Vector3:
	var rng := _cell_rng(cell, 3)
	var center := cell_center(cell)
	var offset_x := rng.randf_range(-CELL_SIZE * 0.2, CELL_SIZE * 0.2)
	var offset_z := rng.randf_range(-CELL_SIZE * 0.2, CELL_SIZE * 0.2)
	var offset_y := rng.randf_range(-MAX_SYSTEM_Y_OFFSET, MAX_SYSTEM_Y_OFFSET)
	return Vector3(center.x + offset_x, offset_y, center.z + offset_z)


# Get the per-system seed for a cell (deterministic).
func cell_system_seed(cell: Vector2i) -> int:
	var combined := _cell_key(cell) + ":seed"
	return combined.hash()


# --- Cell enumeration ---

# Get all cells within a given radius of a universe-space position.
# Returns an array of Vector2i cell coordinates that contain star systems.
# Only cells whose hash value is below the density threshold are included.
func get_cells_around(pos: Vector3, radius: float) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	var center_cell := position_to_cell(pos)
	var cell_radius := int(ceilf(radius / CELL_SIZE)) + 1

	for cx in range(center_cell.x - cell_radius, center_cell.x + cell_radius + 1):
		for cz in range(center_cell.y - cell_radius, center_cell.y + cell_radius + 1):
			var cell := Vector2i(cx, cz)
			if not cell_has_system(cell):
				continue
			var sys_pos := cell_system_position(cell)
			var dist_xz := Vector2(
				sys_pos.x - pos.x,
				sys_pos.z - pos.z
			).length()
			if dist_xz <= radius:
				result.append(cell)

	return result


# --- Placeholder creation ---

# Create a StarSystemDescriptor placeholder for a given cell.
# The system_id is derived from the cell coordinates for uniqueness.
func create_placeholder_for_cell(cell: Vector2i) -> StarSystemDescriptor:
	var sys := StarSystemDescriptor.new()
	sys.system_id = StringName(&"sys_%d_%d" % [cell.x, cell.y])
	sys.universe_position = cell_system_position(cell)
	sys.system_seed = cell_system_seed(cell)
	sys.generation_state = StarSystemDescriptor.STATE_PLACEHOLDER
	sys.system_type = cell_system_type(cell)
	sys.system_radius = cell_system_radius(cell)
	return sys


# --- Hashing internals ---

# Generate a deterministic string key for a cell.
func _cell_key(cell: Vector2i) -> String:
	return String.num_uint64(uint64_t(_universe_seed)) + \
		":grid:" + String.num_int64(cell.x) + ":" + String.num_int64(cell.y)


# Compute a deterministic hash value in [0.0, 1.0) for a cell.
# Used to decide whether a cell contains a star system.
func _cell_hash(cell: Vector2i) -> float:
	var key := _cell_key(cell)
	var h := key.hash()
	# Map to [0.0, 1.0) using the lower bits.
	return float(uint64_t(h) % 1000000u) / 1000000.0


# Create a deterministic RandomNumberGenerator for a cell.
# salt: different salts produce different RNG streams for the same cell,
# allowing independent randomization of type, radius, position, etc.
func _cell_rng(cell: Vector2i, salt: int) -> RandomNumberGenerator:
	var key := _cell_key(cell) + ":salt:" + String.num_int64(salt)
	var rng := RandomNumberGenerator.new()
	rng.seed = key.hash()
	return rng


# --- System type selection (mirrors StarSystemGenerator weights) ---

# System type generation weights.
const TYPE_WEIGHTS: Dictionary = {
	StarSystemDescriptor.TYPE_SINGLE_STAR: 55.0,
	StarSystemDescriptor.TYPE_BINARY: 25.0,
	StarSystemDescriptor.TYPE_TRINARY: 8.0,
	StarSystemDescriptor.TYPE_STARLESS: 7.0,
	StarSystemDescriptor.TYPE_REMNANT: 5.0,
}


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


# --- System radius estimation (mirrors StarSystemGenerator) ---

# Estimate system bounding radius for a placeholder system.
static func _estimate_system_radius(system_type: String, rng: RandomNumberGenerator) -> float:
	var max_planets := 12
	match system_type:
		StarSystemDescriptor.TYPE_BINARY:
			max_planets = 10
		StarSystemDescriptor.TYPE_TRINARY:
			max_planets = 8
		StarSystemDescriptor.TYPE_STARLESS:
			max_planets = 3
		StarSystemDescriptor.TYPE_REMNANT:
			max_planets = 4

	var base_orbit_spacing := 2000.0
	var companion_orbit_mult := 3.0
	var outermost_orbit := base_orbit_spacing * (1.0 + float(max_planets) * 1.2)
	match system_type:
		StarSystemDescriptor.TYPE_BINARY:
			outermost_orbit *= companion_orbit_mult * 0.5
		StarSystemDescriptor.TYPE_TRINARY:
			outermost_orbit *= companion_orbit_mult * 0.7
	return outermost_orbit
