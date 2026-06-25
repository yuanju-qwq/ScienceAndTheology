class_name BuiltinElixirs
extends RefCounted

# Built-in elixir recipe definitions, migrated from
# C++ ElixirRegistry::register_builtin_recipes().
#
# ElixirType: INITIATION=0, ENHANCEMENT=1, PROMOTION=2, TUNING=3, PURIFICATION=4
# SublimationPath: NONE=0, SAND_ARMOR=1, TIDAL=2, STORM=3, FURNACE=4, RADIANCE=5
# OrganSlot: HEART=0, BONE=1, BLOOD=2, LUNG=3, EYE=4, NERVE=5, SKIN=6, COUNT=7
# RuneElement: FIRE=0, WATER=1, EARTH=2, AIR=3, LIGHT=4, DARK=5, ORDER=6, CHAOS=7


static func register_all() -> void:
	# --- Initiation elixirs (one per sublimation path) ---

	# Sand Armor Initiation Elixir
	GDElixirRegistry.register_recipe({
		"id": "elixir_sand_armor_initiation",
		"title_key": "Sand Armor Initiation Elixir",
		"type": 0,          # INITIATION
		"target_path": 1,   # SAND_ARMOR
		"target_slot": 1,   # BONE
		"primary_element": 2,  # EARTH
		"source_cost": 50,
		"stability_modifier": 5.0,
		"mutation_modifier": 2.0,
		"tuning_degree": 0,
		"required_rune_elements": [2, 6],  # EARTH, ORDER
	})

	# Tidal Initiation Elixir
	GDElixirRegistry.register_recipe({
		"id": "elixir_tidal_initiation",
		"title_key": "Tidal Initiation Elixir",
		"type": 0,          # INITIATION
		"target_path": 2,   # TIDAL
		"target_slot": 3,   # LUNG
		"primary_element": 1,  # WATER
		"source_cost": 50,
		"stability_modifier": 5.0,
		"mutation_modifier": 2.0,
		"tuning_degree": 0,
		"required_rune_elements": [1, 4],  # WATER, LIGHT
	})

	# Storm Initiation Elixir
	GDElixirRegistry.register_recipe({
		"id": "elixir_storm_initiation",
		"title_key": "Storm Initiation Elixir",
		"type": 0,          # INITIATION
		"target_path": 3,   # STORM
		"target_slot": 5,   # NERVE
		"primary_element": 3,  # AIR
		"source_cost": 50,
		"stability_modifier": 5.0,
		"mutation_modifier": 2.0,
		"tuning_degree": 0,
		"required_rune_elements": [3, 0],  # AIR, FIRE
	})

	# Furnace Initiation Elixir
	GDElixirRegistry.register_recipe({
		"id": "elixir_furnace_initiation",
		"title_key": "Furnace Initiation Elixir",
		"type": 0,          # INITIATION
		"target_path": 4,   # FURNACE
		"target_slot": 0,   # HEART
		"primary_element": 0,  # FIRE
		"source_cost": 50,
		"stability_modifier": 5.0,
		"mutation_modifier": 2.0,
		"tuning_degree": 0,
		"required_rune_elements": [0, 7],  # FIRE, CHAOS
	})

	# Radiance Initiation Elixir
	GDElixirRegistry.register_recipe({
		"id": "elixir_radiance_initiation",
		"title_key": "Radiance Initiation Elixir",
		"type": 0,          # INITIATION
		"target_path": 5,   # RADIANCE
		"target_slot": 4,   # EYE
		"primary_element": 4,  # LIGHT
		"source_cost": 50,
		"stability_modifier": 5.0,
		"mutation_modifier": 2.0,
		"tuning_degree": 0,
		"required_rune_elements": [4, 6],  # LIGHT, ORDER
	})

	# --- Utility potions ---

	# Basic Tuning Potion
	GDElixirRegistry.register_recipe({
		"id": "elixir_basic_tuning",
		"title_key": "Basic Tuning Potion",
		"type": 3,          # TUNING
		"target_path": 0,   # NONE
		"target_slot": 7,   # COUNT (any)
		"primary_element": 6,  # ORDER
		"source_cost": 0,
		"stability_modifier": 2.0,
		"mutation_modifier": -1.0,
		"tuning_degree": 1,
		"required_rune_elements": [6],  # ORDER
	})

	# Basic Purification Potion
	GDElixirRegistry.register_recipe({
		"id": "elixir_basic_purification",
		"title_key": "Basic Purification Potion",
		"type": 4,          # PURIFICATION
		"target_path": 0,   # NONE
		"target_slot": 7,   # COUNT (any)
		"primary_element": 4,  # LIGHT
		"source_cost": 0,
		"stability_modifier": 5.0,
		"mutation_modifier": -3.0,
		"tuning_degree": 0,
		"required_rune_elements": [4, 1],  # LIGHT, WATER
	})
