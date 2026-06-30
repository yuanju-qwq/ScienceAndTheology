class_name BuiltinElixirs
extends RefCounted

# Built-in elixir recipe definitions, migrated from
# C++ ElixirRegistry::register_builtin_recipes().
#
# 显式确定性 ID（P1: 热重载后 ID 不漂移）：
#   explicit_id = type * 64 + target_path * 8 + target_slot + 1
# ID 0 保留给 invalid；每个 type 预留 64 位空间（8 path × 8 slot）。
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
		"explicit_id": 0 * 64 + 1 * 8 + 1 + 1,
		"title_key": "elixir.sand_armor_initiation",
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
		"explicit_id": 0 * 64 + 2 * 8 + 3 + 1,
		"title_key": "elixir.tidal_initiation",
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
		"explicit_id": 0 * 64 + 3 * 8 + 5 + 1,
		"title_key": "elixir.storm_initiation",
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
		"explicit_id": 0 * 64 + 4 * 8 + 0 + 1,
		"title_key": "elixir.furnace_initiation",
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
		"explicit_id": 0 * 64 + 5 * 8 + 4 + 1,
		"title_key": "elixir.radiance_initiation",
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
		"explicit_id": 3 * 64 + 0 * 8 + 7 + 1,
		"title_key": "elixir.basic_tuning",
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
		"explicit_id": 4 * 64 + 0 * 8 + 7 + 1,
		"title_key": "elixir.basic_purification",
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
