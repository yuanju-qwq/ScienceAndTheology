class_name BuiltinDroppedOrgans
extends RefCounted

# Built-in dropped organ definitions, migrated from C++
# DroppedOrganRegistry::register_builtin_organs().
#
# When a creature dies, it may drop a source organ. A player can devour
# the organ to transform their own organ in the same slot into a bloodline
# organ. Aberration-sourced organs imitate a sublimation path organ but
# weaker.
#
# 显式确定性 ID（P1: 热重载后 ID 不漂移）：
#   explicit_id = source * 64 + imitated_path * 8 + target_slot + 1
# ID 0 保留给 invalid；每个 source 预留 64 位空间（8 path × 8 slot）。

# OrganSlot
const HEART := 0
const BONE := 1
const BLOOD := 2
const LUNG := 3
const EYE := 4
const NERVE := 5
const SKIN := 6

# BloodlineSource
const SOURCE_NONE := 0
const CREATURE := 1
const ABERRATION := 2

# RuneElement
const FIRE := 0
const WATER := 1
const EARTH := 2
const AIR := 3
const LIGHT := 4
const DARK := 5
const ORDER := 6
const CHAOS := 7

# SublimationPath
const PATH_NONE := 0
const SAND_ARMOR := 1
const TIDAL := 2
const STORM := 3
const FURNACE := 4
const RADIANCE := 5

# OrganQuality
const FLAWED := 0
const COMMON := 1
const GOOD := 2
const PURE := 3
const ANCIENT := 4
const PERFECT := 5


# Register all built-in dropped organs with the C++ DroppedOrganRegistry
# via GDDroppedOrganRegistry.
static func register_all() -> void:
	# --- Creature-sourced organs ---

	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_rock_lizard_heart",
		"explicit_id": CREATURE * 64 + PATH_NONE * 8 + HEART + 1,
		"title_key": "organ.rock_lizard_heart",
		"target_slot": HEART,
		"source": CREATURE,
		"source_creature_id": "rock_lizard",
		"primary_element": EARTH,
		"secondary_elements": [],
		"imitated_path": PATH_NONE,
		"source_cost": 30,
		"stability_modifier": -2.0,
		"mutation_modifier": 1.0,
		"result_quality": COMMON,
		"result_power_multiplier": 0.6,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_rock_lizard_bone",
		"explicit_id": CREATURE * 64 + PATH_NONE * 8 + BONE + 1,
		"title_key": "organ.rock_lizard_bone",
		"target_slot": BONE,
		"source": CREATURE,
		"source_creature_id": "rock_lizard",
		"primary_element": EARTH,
		"secondary_elements": [],
		"imitated_path": PATH_NONE,
		"source_cost": 30,
		"stability_modifier": -2.0,
		"mutation_modifier": 1.0,
		"result_quality": COMMON,
		"result_power_multiplier": 0.6,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_sea_serpent_lung",
		"explicit_id": CREATURE * 64 + PATH_NONE * 8 + LUNG + 1,
		"title_key": "organ.sea_serpent_lung",
		"target_slot": LUNG,
		"source": CREATURE,
		"source_creature_id": "sea_serpent",
		"primary_element": WATER,
		"secondary_elements": [],
		"imitated_path": PATH_NONE,
		"source_cost": 30,
		"stability_modifier": -2.0,
		"mutation_modifier": 1.0,
		"result_quality": COMMON,
		"result_power_multiplier": 0.6,
	})

	# --- Aberration-sourced organs (imitate sublimation paths) ---

	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_aberrant_sand_armor_bone",
		"explicit_id": ABERRATION * 64 + SAND_ARMOR * 8 + BONE + 1,
		"title_key": "organ.aberrant_sand_armor_bone",
		"target_slot": BONE,
		"source": ABERRATION,
		"source_creature_id": "sand_armor_aberrant",
		"primary_element": EARTH,
		"secondary_elements": [ORDER],
		"imitated_path": SAND_ARMOR,
		"source_cost": 40,
		"stability_modifier": -3.0,
		"mutation_modifier": 2.0,
		"result_quality": FLAWED,
		"result_power_multiplier": 0.5,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_aberrant_tidal_lung",
		"explicit_id": ABERRATION * 64 + TIDAL * 8 + LUNG + 1,
		"title_key": "organ.aberrant_tidal_lung",
		"target_slot": LUNG,
		"source": ABERRATION,
		"source_creature_id": "tidal_aberrant",
		"primary_element": WATER,
		"secondary_elements": [LIGHT],
		"imitated_path": TIDAL,
		"source_cost": 40,
		"stability_modifier": -3.0,
		"mutation_modifier": 2.0,
		"result_quality": FLAWED,
		"result_power_multiplier": 0.5,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_aberrant_storm_nerve",
		"explicit_id": ABERRATION * 64 + STORM * 8 + NERVE + 1,
		"title_key": "organ.aberrant_storm_nerve",
		"target_slot": NERVE,
		"source": ABERRATION,
		"source_creature_id": "storm_aberrant",
		"primary_element": AIR,
		"secondary_elements": [FIRE],
		"imitated_path": STORM,
		"source_cost": 40,
		"stability_modifier": -3.0,
		"mutation_modifier": 2.0,
		"result_quality": FLAWED,
		"result_power_multiplier": 0.5,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_aberrant_furnace_heart",
		"explicit_id": ABERRATION * 64 + FURNACE * 8 + HEART + 1,
		"title_key": "organ.aberrant_furnace_heart",
		"target_slot": HEART,
		"source": ABERRATION,
		"source_creature_id": "furnace_aberrant",
		"primary_element": FIRE,
		"secondary_elements": [CHAOS],
		"imitated_path": FURNACE,
		"source_cost": 40,
		"stability_modifier": -3.0,
		"mutation_modifier": 2.0,
		"result_quality": FLAWED,
		"result_power_multiplier": 0.5,
	})
	GDDroppedOrganRegistry.register_organ({
		"id": "dropped_aberrant_radiance_eye",
		"explicit_id": ABERRATION * 64 + RADIANCE * 8 + EYE + 1,
		"title_key": "organ.aberrant_radiance_eye",
		"target_slot": EYE,
		"source": ABERRATION,
		"source_creature_id": "radiance_aberrant",
		"primary_element": LIGHT,
		"secondary_elements": [ORDER],
		"imitated_path": RADIANCE,
		"source_cost": 40,
		"stability_modifier": -3.0,
		"mutation_modifier": 2.0,
		"result_quality": FLAWED,
		"result_power_multiplier": 0.5,
	})
