class_name BuiltinSublimationPaths
extends RefCounted

# Built-in sublimation path definitions, migrated from C++
# SublimationPathRegistry::register_builtin_paths().
# 5 paths + 1 organ skill (sand_armor only).

# SublimationPath: NONE=0, SAND_ARMOR=1, TIDAL=2, STORM=3, FURNACE=4, RADIANCE=5
# OrganSlot: HEART=0, BONE=1, BLOOD=2, LUNG=3, EYE=4, NERVE=5, SKIN=6
# RuneElement: FIRE=0, WATER=1, EARTH=2, AIR=3, LIGHT=4, DARK=5, ORDER=6, CHAOS=7

const PATH_SAND_ARMOR := 1
const PATH_TIDAL := 2
const PATH_STORM := 3
const PATH_FURNACE := 4
const PATH_RADIANCE := 5

const SLOT_HEART := 0
const SLOT_BONE := 1
const SLOT_LUNG := 3
const SLOT_EYE := 4
const SLOT_NERVE := 5

const ELEMENT_FIRE := 0
const ELEMENT_WATER := 1
const ELEMENT_EARTH := 2
const ELEMENT_AIR := 3
const ELEMENT_LIGHT := 4


static func register_all() -> void:
	_register_sand_armor()
	_register_tidal()
	_register_storm()
	_register_furnace()
	_register_radiance()


static func _register_sand_armor() -> void:
	GDSublimationPathRegistry.register_path({
		"path_id": PATH_SAND_ARMOR,
		"id": "sand_armor",
		"title_key": "path.sand_armor",
		"primary_element": ELEMENT_EARTH,
		"organ_stages": [
			{
				"slot": SLOT_BONE,
				"organ_name": "Sand Armor Rock Core",
				"element": ELEMENT_EARTH,
				"min_sublimation_level": 1,
				"sublimation_degree_granted": 1,
			},
		],
	})
	GDSublimationPathRegistry.register_skill({
		"id": "skill_rock_shield",
		"title_key": "skill.rock_shield",
		"required_slot": SLOT_BONE,
		"required_path": PATH_SAND_ARMOR,
		"min_organ_level": 0,
		"mana_cost": 10,
		"cooldown_ticks": 60,
		"effect_type": 1,
		"effect_param_1": 30.0,
		"effect_param_2": 5.0,
	})


static func _register_tidal() -> void:
	GDSublimationPathRegistry.register_path({
		"path_id": PATH_TIDAL,
		"id": "tidal",
		"title_key": "path.tidal",
		"primary_element": ELEMENT_WATER,
		"organ_stages": [
			{
				"slot": SLOT_LUNG,
				"organ_name": "Tidal Lung",
				"element": ELEMENT_WATER,
				"min_sublimation_level": 1,
				"sublimation_degree_granted": 1,
			},
		],
	})


static func _register_storm() -> void:
	GDSublimationPathRegistry.register_path({
		"path_id": PATH_STORM,
		"id": "storm",
		"title_key": "path.storm",
		"primary_element": ELEMENT_AIR,
		"organ_stages": [
			{
				"slot": SLOT_NERVE,
				"organ_name": "Thunder Nerve",
				"element": ELEMENT_AIR,
				"min_sublimation_level": 1,
				"sublimation_degree_granted": 1,
			},
		],
	})


static func _register_furnace() -> void:
	GDSublimationPathRegistry.register_path({
		"path_id": PATH_FURNACE,
		"id": "furnace",
		"title_key": "path.furnace",
		"primary_element": ELEMENT_FIRE,
		"organ_stages": [
			{
				"slot": SLOT_HEART,
				"organ_name": "Blazing Heart",
				"element": ELEMENT_FIRE,
				"min_sublimation_level": 1,
				"sublimation_degree_granted": 1,
			},
		],
	})


static func _register_radiance() -> void:
	GDSublimationPathRegistry.register_path({
		"path_id": PATH_RADIANCE,
		"id": "radiance",
		"title_key": "path.radiance",
		"primary_element": ELEMENT_LIGHT,
		"organ_stages": [
			{
				"slot": SLOT_EYE,
				"organ_name": "Radiance Eye",
				"element": ELEMENT_LIGHT,
				"min_sublimation_level": 1,
				"sublimation_degree_granted": 1,
			},
		],
	})
