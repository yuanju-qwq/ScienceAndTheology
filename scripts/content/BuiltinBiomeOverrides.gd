class_name BuiltinBiomeOverrides
extends RefCounted

# Built-in biome overrides, migrated from C++
# EcosystemSystem::register_default_biome_overrides().
# Only numeric parameters; species-biome association is self-described
# by CreatureSpeciesDef::biomes (see BuiltinSpecies.gd).

# Biome constants (match ecosystem_biome in population_cell.hpp)
const PLAINS := 0
const DESERT := 1
const ROCKY := 2
const OCEAN := 3
const BARREN := 4


static func register_all() -> void:
	# Plains: temperate, balanced ecosystem.
	GDBiomeConfigRegistry.register_biome_override({
		"biome_type": PLAINS,
		"base_water": 0.5,
		"base_fertility": 0.5,
		"veg_growth_multiplier": 1.0,
		"max_vegetation": 1.0,
		"max_herbivore": 1.0,
		"max_predator": 1.0,
	})

	# Desert: low water, sparse vegetation.
	GDBiomeConfigRegistry.register_biome_override({
		"biome_type": DESERT,
		"base_water": 0.1,
		"base_fertility": 0.2,
		"veg_growth_multiplier": 0.4,
		"max_vegetation": 0.4,
		"max_herbivore": 0.5,
		"max_predator": 0.4,
	})

	# Rocky: low fertility, hardy species only.
	GDBiomeConfigRegistry.register_biome_override({
		"biome_type": ROCKY,
		"base_water": 0.3,
		"base_fertility": 0.3,
		"veg_growth_multiplier": 0.5,
		"max_vegetation": 0.5,
		"max_herbivore": 0.4,
		"max_predator": 0.5,
	})

	# Ocean: water-dominated, aquatic species only.
	GDBiomeConfigRegistry.register_biome_override({
		"biome_type": OCEAN,
		"base_water": 1.0,
		"base_fertility": 0.1,
		"veg_growth_multiplier": 0.2,
		"max_vegetation": 0.2,
		"max_herbivore": 0.1,
		"max_predator": 0.6,
	})

	# Barren: toxic/corrosive, no natural fauna.
	# Note: kBarren is never returned by infer_biome_type() currently,
	# so this override is effectively dormant until a mechanism sets
	# cell.biome_type = BARREN.
	GDBiomeConfigRegistry.register_biome_override({
		"biome_type": BARREN,
		"base_water": 0.05,
		"base_fertility": 0.05,
		"veg_growth_multiplier": 0.1,
		"max_vegetation": 0.1,
		"max_herbivore": 0.0,
		"max_predator": 0.0,
	})
