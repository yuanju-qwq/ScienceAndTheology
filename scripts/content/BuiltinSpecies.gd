class_name BuiltinSpecies
extends RefCounted

const HERBIVORE := 0
const PREDATOR := 1

# Biome constants (match ecosystem_biome in population_cell.hpp)
const BIOME_PLAINS := 0
const BIOME_DESERT := 1
const BIOME_ROCKY := 2
const BIOME_OCEAN := 3
const BIOME_BARREN := 4

static func register_all() -> void:
	# Herbivores
	GDSpeciesRegistry.register_species({
		"species_key": "glow_deer",
		"title_key": "creature.glow_deer",
		"role": HERBIVORE,
		"model_key": "glow_deer",
		"move_speed": 0.12,
		"base_health": 0.8,
		"flee_detection_radius": 12.0,
		"wander_radius": 10.0,
		"model_scale": 1.2,
		"biomes": [BIOME_PLAINS],
		"drops": [
			{"item_key": "snt:glow_deer_antler", "chance": 0.7, "min_count": 1, "max_count": 2},
			{"item_key": "snt:purifying_pollen", "chance": 0.5, "min_count": 1, "max_count": 1},
			{"item_key": "snt:raw_meat.glow_deer", "chance": 1.0, "min_count": 1, "max_count": 2},
		],
	})
	GDSpeciesRegistry.register_species({
		"species_key": "rock_lizard",
		"title_key": "creature.rock_lizard",
		"role": HERBIVORE,
		"model_key": "rock_lizard",
		"move_speed": 0.08,
		"base_health": 1.0,
		"flee_detection_radius": 8.0,
		"wander_radius": 6.0,
		"model_scale": 0.7,
		"biomes": [BIOME_PLAINS, BIOME_DESERT, BIOME_ROCKY],
		"drops": [
			{"item_key": "snt:rock_lizard_scale", "chance": 0.8, "min_count": 1, "max_count": 3},
			{"item_key": "snt:crystallized_bone_powder", "chance": 0.4, "min_count": 1, "max_count": 1},
			{"item_key": "snt:raw_meat.rock_lizard", "chance": 1.0, "min_count": 1, "max_count": 2},
		],
	})

	# Predators
	GDSpeciesRegistry.register_species({
		"species_key": "thunderbird",
		"title_key": "creature.thunderbird",
		"role": PREDATOR,
		"model_key": "thunderbird",
		"move_speed": 0.18,
		"base_health": 0.9,
		"wander_radius": 14.0,
		"model_scale": 1.0,
		"biomes": [BIOME_PLAINS, BIOME_DESERT, BIOME_ROCKY],
		"drops": [
			{"item_key": "snt:thunderbird_feather", "chance": 0.7, "min_count": 1, "max_count": 2},
			{"item_key": "snt:magnetic_crystal_shard", "chance": 0.3, "min_count": 1, "max_count": 1},
			{"item_key": "snt:raw_meat.thunderbird", "chance": 1.0, "min_count": 1, "max_count": 1},
		],
	})
	GDSpeciesRegistry.register_species({
		"species_key": "sea_serpent",
		"title_key": "creature.sea_serpent",
		"role": PREDATOR,
		"model_key": "sea_serpent",
		"move_speed": 0.14,
		"base_health": 1.0,
		"wander_radius": 10.0,
		"model_scale": 1.3,
		"biomes": [BIOME_OCEAN],
		"drops": [
			{"item_key": "snt:sea_serpent_scale", "chance": 0.7, "min_count": 1, "max_count": 2},
			{"item_key": "snt:tidal_gland", "chance": 0.3, "min_count": 1, "max_count": 1},
			{"item_key": "snt:raw_meat.sea_serpent", "chance": 1.0, "min_count": 1, "max_count": 3},
		],
	})
	GDSpeciesRegistry.register_species({
		"species_key": "blaze_beast",
		"title_key": "creature.blaze_beast",
		"role": PREDATOR,
		"model_key": "blaze_beast",
		"move_speed": 0.10,
		"base_health": 1.2,
		"wander_radius": 8.0,
		"model_scale": 1.4,
		"biomes": [BIOME_PLAINS, BIOME_ROCKY],
		"drops": [
			{"item_key": "snt:blazing_core", "chance": 0.5, "min_count": 1, "max_count": 1},
			{"item_key": "snt:molten_blood_sample", "chance": 0.4, "min_count": 1, "max_count": 1},
			{"item_key": "snt:raw_meat.blaze_beast", "chance": 1.0, "min_count": 1, "max_count": 2},
		],
	})

	# Special / Boss-tier
	GDSpeciesRegistry.register_species({
		"species_key": "aether_wraith",
		"title_key": "creature.aether_wraith",
		"role": PREDATOR,
		"model_key": "aether_wraith",
		"move_speed": 0.15,
		"base_health": 1.5,
		"wander_radius": 12.0,
		"model_scale": 1.0,
		"drops": [
			{"item_key": "snt:aether_fragment", "chance": 0.6, "min_count": 1, "max_count": 2},
			{"item_key": "snt:blueprint_shard", "chance": 0.2, "min_count": 1, "max_count": 1},
		],
	})
	GDSpeciesRegistry.register_species({
		"species_key": "aberrant_ascended",
		"title_key": "creature.aberrant_ascended",
		"role": PREDATOR,
		"model_key": "aberrant_ascended",
		"move_speed": 0.13,
		"base_health": 2.0,
		"wander_radius": 10.0,
		"model_scale": 1.2,
		"drops": [
			{"item_key": "snt:aberrant_organ", "chance": 0.5, "min_count": 1, "max_count": 2},
			{"item_key": "snt:polluted_source_essence", "chance": 0.4, "min_count": 1, "max_count": 1},
		],
	})
