# StarSpectralType — stellar spectral classification and property tables.
# O/B/A/F/G/K/M are main-sequence stars ordered by temperature (hot → cool).
# WD/NS/BH are stellar remnants (white dwarf, neutron star, black hole).
# Each type defines color, radius range, luminosity, spawn weight, and
# habitable-zone distance multiplier for planet generation.
class_name StarSpectralType
extends RefCounted

enum Type {
	O,    # Blue supergiant — very rare, very hot, very luminous
	B,    # Blue-white — rare, hot
	A,    # White — uncommon
	F,    # Yellow-white — moderate
	G,    # Yellow (Sun-like) — common
	K,    # Orange — common, long-lived
	M,    # Red dwarf — most common, dim, very long-lived
	WD,   # White dwarf — remnant, small, dim
	NS,   # Neutron star — remnant, tiny, extreme gravity (visual + gravity only)
	BH,   # Black hole — remnant, no light, extreme gravity (visual + gravity only)
}

# Spectral type data: color, radius range, light energy, spawn weight,
# habitable-zone distance multiplier.
# radius_min/max in voxel blocks, light_energy for Godot Light3D,
# weight is relative probability (sum does not need to be 100),
# hz_mult multiplies the base orbital distance for habitable-zone planets.
struct SpectralData:
	var color: Color
	var radius_min: float
	var radius_max: float
	var light_energy_min: float
	var light_energy_max: float
	var weight: float
	var hz_mult: float

# Lookup table keyed by Type enum value.
# Entries are ordered O → M → WD → NS → BH.
const DATA: Dictionary = {
	Type.O: {
		"color": Color(0.6, 0.7, 1.0, 1.0),
		"radius_min": 600.0,
		"radius_max": 1200.0,
		"light_energy_min": 5.0,
		"light_energy_max": 10.0,
		"weight": 1.0,
		"hz_mult": 8.0,
	},
	Type.B: {
		"color": Color(0.65, 0.75, 1.0, 1.0),
		"radius_min": 400.0,
		"radius_max": 800.0,
		"light_energy_min": 3.0,
		"light_energy_max": 5.0,
		"weight": 3.0,
		"hz_mult": 5.0,
	},
	Type.A: {
		"color": Color(0.9, 0.9, 1.0, 1.0),
		"radius_min": 300.0,
		"radius_max": 500.0,
		"light_energy_min": 2.0,
		"light_energy_max": 3.0,
		"weight": 5.0,
		"hz_mult": 3.0,
	},
	Type.F: {
		"color": Color(1.0, 0.95, 0.85, 1.0),
		"radius_min": 250.0,
		"radius_max": 400.0,
		"light_energy_min": 1.5,
		"light_energy_max": 2.5,
		"weight": 10.0,
		"hz_mult": 2.0,
	},
	Type.G: {
		"color": Color(1.0, 0.95, 0.8, 1.0),
		"radius_min": 200.0,
		"radius_max": 350.0,
		"light_energy_min": 1.5,
		"light_energy_max": 2.5,
		"weight": 15.0,
		"hz_mult": 1.5,
	},
	Type.K: {
		"color": Color(1.0, 0.7, 0.4, 1.0),
		"radius_min": 150.0,
		"radius_max": 250.0,
		"light_energy_min": 1.0,
		"light_energy_max": 2.0,
		"weight": 25.0,
		"hz_mult": 1.0,
	},
	Type.M: {
		"color": Color(1.0, 0.4, 0.2, 1.0),
		"radius_min": 100.0,
		"radius_max": 200.0,
		"light_energy_min": 0.5,
		"light_energy_max": 1.5,
		"weight": 35.0,
		"hz_mult": 0.5,
	},
	Type.WD: {
		"color": Color(0.8, 0.8, 1.0, 1.0),
		"radius_min": 50.0,
		"radius_max": 100.0,
		"light_energy_min": 0.3,
		"light_energy_max": 0.8,
		"weight": 3.0,
		"hz_mult": 0.0,
	},
	Type.NS: {
		"color": Color(0.5, 0.5, 1.0, 1.0),
		"radius_min": 20.0,
		"radius_max": 50.0,
		"light_energy_min": 0.0,
		"light_energy_max": 0.0,
		"weight": 2.0,
		"hz_mult": 0.0,
	},
	Type.BH: {
		"color": Color(0.0, 0.0, 0.0, 1.0),
		"radius_min": 30.0,
		"radius_max": 80.0,
		"light_energy_min": 0.0,
		"light_energy_max": 0.0,
		"weight": 1.0,
		"hz_mult": 0.0,
	},
}

# Total weight for weighted-random selection.
static var _total_weight: float:
	get:
		var total := 0.0
		for key in DATA:
			total += DATA[key]["weight"]
		return total


# Pick a random spectral type using weighted probability.
# Main-sequence types (O-M) are much more likely than remnants (WD/NS/BH).
static func random_type(rng: RandomNumberGenerator) -> int:
	var roll := rng.randf() * _total_weight
	var accumulated := 0.0
	for key in DATA:
		accumulated += DATA[key]["weight"]
		if roll <= accumulated:
			return key
	return Type.M


# Get the color for a spectral type.
static func get_color(spectral_type: int) -> Color:
	if DATA.has(spectral_type):
		return DATA[spectral_type]["color"]
	return Color(1.0, 0.95, 0.8, 1.0)


# Get a random radius within the range for a spectral type.
static func random_radius(spectral_type: int, rng: RandomNumberGenerator) -> float:
	if not DATA.has(spectral_type):
		return rng.randf_range(200.0, 350.0)
	var entry: Dictionary = DATA[spectral_type]
	return rng.randf_range(entry["radius_min"], entry["radius_max"])


# Get a random light energy within the range for a spectral type.
static func random_light_energy(spectral_type: int, rng: RandomNumberGenerator) -> float:
	if not DATA.has(spectral_type):
		return rng.randf_range(1.5, 2.5)
	var entry: Dictionary = DATA[spectral_type]
	return rng.randf_range(entry["light_energy_min"], entry["light_energy_max"])


# Get the habitable-zone distance multiplier for a spectral type.
# Returns 0.0 for remnants (no habitable zone).
static func get_hz_multiplier(spectral_type: int) -> float:
	if DATA.has(spectral_type):
		return DATA[spectral_type]["hz_mult"]
	return 0.0


# Check whether the spectral type is a main-sequence star (O-M).
static func is_main_sequence(spectral_type: int) -> bool:
	return spectral_type >= Type.O and spectral_type <= Type.M


# Check whether the spectral type is a stellar remnant (WD/NS/BH).
static func is_remnant(spectral_type: int) -> bool:
	return spectral_type >= Type.WD and spectral_type <= Type.BH


# Check whether the spectral type emits visible light.
# Black holes and neutron stars do not.
static func emits_light(spectral_type: int) -> bool:
	return spectral_type != Type.NS and spectral_type != Type.BH


# Get the display name for a spectral type.
static func get_display_name(spectral_type: int) -> String:
	match spectral_type:
		Type.O: return "O-type Star"
		Type.B: return "B-type Star"
		Type.A: return "A-type Star"
		Type.F: return "F-type Star"
		Type.G: return "G-type Star"
		Type.K: return "K-type Star"
		Type.M: return "M-type Star"
		Type.WD: return "White Dwarf"
		Type.NS: return "Neutron Star"
		Type.BH: return "Black Hole"
		_: return "Unknown Star"
