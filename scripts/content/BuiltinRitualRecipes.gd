class_name BuiltinRitualRecipes
extends RefCounted

# Built-in ritual recipe definitions, migrated from C++
# RitualRecipeRegistry::register_builtin_recipes().

# RitualEffectType: NONE=0, MACHINE_BLESSING=1, TOOL_ENCHANTMENT=2,
#   TERRAIN_ALTERATION=3, PLAYER_BUFF=4, WORLD_EVENT=5, CURSE=6,
#   TELEPORTATION=7, DIVINATION=8, MANA_EXPANSION=9
# RuneElement: FIRE=0, WATER=1, EARTH=2, AIR=3, LIGHT=4, DARK=5,
#   ORDER=6, CHAOS=7
# RuneTier: COMMON=0, REFINED=1, SUPERIOR=2, LEGENDARY=3

const _FIRE := 0
const _WATER := 1
const _EARTH := 2
const _AIR := 3
const _LIGHT := 4
const _DARK := 5
const _ORDER := 6
const _CHAOS := 7

const _MACHINE_BLESSING := 1
const _WORLD_EVENT := 5
const _PLAYER_BUFF := 4
const _TELEPORTATION := 7
const _DIVINATION := 8
const _MANA_EXPANSION := 9


# Register all built-in ritual recipes with the C++ RitualRecipeRegistry
# via GDRitualRecipeRegistry.
static func register_all() -> void:
	# --- Machine blessings ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_speed",
		"title_key": "Machine Speed I",
		"pedestals": [
			_ped(_FIRE), _ped(_ORDER), _ped(_FIRE), _ped(_ORDER),
		],
		"mana_cost": 40,
		"duration_ticks": 100,
		"consume_runes": false,
		"effect": {
			"type": _MACHINE_BLESSING,
			"param_json": '{"boost":"speed","mult":1.2}',
			"duration_ticks": 72000,
		},
	})

	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_cooling",
		"title_key": "Machine Cooling",
		"pedestals": [
			_ped(_WATER), _ped(_WATER), _ped(_WATER), _ped(_WATER),
		],
		"mana_cost": 50,
		"duration_ticks": 120,
		"consume_runes": false,
		"effect": {
			"type": _MACHINE_BLESSING,
			"param_json": '{"boost":"no_maintenance"}',
			"duration_ticks": 144000,
		},
	})

	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_boost",
		"title_key": "Machine Output I",
		"pedestals": [
			_ped(_EARTH), _ped(_ORDER), _ped(_LIGHT), _ped(_ORDER),
		],
		"mana_cost": 55,
		"duration_ticks": 100,
		"consume_runes": false,
		"effect": {
			"type": _MACHINE_BLESSING,
			"param_json": '{"boost":"output","mult":1.15}',
			"duration_ticks": 72000,
		},
	})

	# --- World events ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_toggle_ruin_gate",
		"title_key": "Toggle Ruin Gate",
		"pedestals": [
			_ped(_LIGHT), _ped(_LIGHT), _ped(_DARK), _ped(_DARK),
		],
		"mana_cost": 30,
		"duration_ticks": 80,
		"consume_runes": true,
		"effect": {
			"type": _WORLD_EVENT,
			"param_json": '{"event":"toggle_ruin_gate"}',
			"duration_ticks": 0,
		},
	})

	# --- Player buffs ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_player_speed",
		"title_key": "Player Speed Boost",
		"pedestals": [
			_ped(_AIR), _ped(_AIR), _ped(_AIR), _ped(_AIR),
		],
		"mana_cost": 25,
		"duration_ticks": 100,
		"consume_runes": true,
		"effect": {
			"type": _PLAYER_BUFF,
			"param_json": '{"buff":"speed","mult":1.5}',
			"duration_ticks": 36000,
		},
	})

	# --- Chaos combo ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_double",
		"title_key": "Machine Random Double Output",
		"pedestals": [
			_ped(_ORDER), _ped(_CHAOS), _ped(_ORDER), _ped(_CHAOS),
		],
		"mana_cost": 70,
		"duration_ticks": 150,
		"consume_runes": true,
		"effect": {
			"type": _MACHINE_BLESSING,
			"param_json": '{"boost":"random_double","chance":0.2}',
			"duration_ticks": 36000,
		},
	})

	# --- Teleportation ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_teleport_link",
		"title_key": "Altar Teleport Link",
		"pedestals": [
			_ped(_FIRE), _ped(_EARTH), _ped(_WATER), _ped(_AIR),
		],
		"mana_cost": 80,
		"duration_ticks": 200,
		"consume_runes": true,
		"effect": {
			"type": _TELEPORTATION,
			"param_json": "{}",
			"duration_ticks": 0,
		},
	})

	# --- Divination ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_divination",
		"title_key": "Divination",
		"pedestals": [
			_ped(_LIGHT), _ped(_EARTH), _ped(_LIGHT), _ped(_EARTH),
		],
		"mana_cost": 20,
		"duration_ticks": 60,
		"consume_runes": false,
		"effect": {
			"type": _DIVINATION,
			"param_json": "{}",
			"duration_ticks": 0,
		},
	})

	# --- Mana expansion ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_mana_expand",
		"title_key": "Mana Expansion +25",
		"pedestals": [
			_ped(_ORDER), _ped(_ORDER), _ped(_ORDER), _ped(_ORDER),
		],
		"mana_cost": 60,
		"duration_ticks": 150,
		"consume_runes": true,
		"effect": {
			"type": _MANA_EXPANSION,
			"param_json": '{"amount":25}',
			"duration_ticks": 0,
		},
	})


# Helper: build a strict-element pedestal slot at the given tier (COMMON by default).
static func _ped(element: int, tier: int = 0) -> Dictionary:
	return {
		"element": element,
		"min_tier": tier,
		"strict_element": true,
	}
