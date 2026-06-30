class_name BuiltinRitualRecipes
extends RefCounted

# Built-in ritual recipe definitions, migrated from C++
# RitualRecipeRegistry::register_builtin_recipes().
#
# 显式确定性 ID（P1: 热重载后 ID 不漂移）：
#   explicit_id = effect_type * 16 + sub_index + 1
# ID 0 保留给 invalid；每个 effect_type 预留 16 槽位。
# RitualRecipeId 是 uint8_t（上限 255），10 类 × 16 = 160 < 255。

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
	# --- Machine blessings (effect_type=1, id=17-20) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_speed",
		"explicit_id": _MACHINE_BLESSING * 16 + 0 + 1,
		"title_key": "ritual.machine_speed",
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
		"explicit_id": _MACHINE_BLESSING * 16 + 1 + 1,
		"title_key": "ritual.machine_cooling",
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
		"explicit_id": _MACHINE_BLESSING * 16 + 2 + 1,
		"title_key": "ritual.machine_output",
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

	# --- World events (effect_type=5, id=81) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_toggle_ruin_gate",
		"explicit_id": _WORLD_EVENT * 16 + 0 + 1,
		"title_key": "ritual.toggle_ruin_gate",
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

	# --- Player buffs (effect_type=4, id=65) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_player_speed",
		"explicit_id": _PLAYER_BUFF * 16 + 0 + 1,
		"title_key": "ritual.player_speed_boost",
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

	# --- Chaos combo (effect_type=1, id=20) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_machine_double",
		"explicit_id": _MACHINE_BLESSING * 16 + 3 + 1,
		"title_key": "ritual.machine_double_output",
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

	# --- Teleportation (effect_type=7, id=113) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_teleport_link",
		"explicit_id": _TELEPORTATION * 16 + 0 + 1,
		"title_key": "ritual.teleport_link",
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

	# --- Divination (effect_type=8, id=129) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_divination",
		"explicit_id": _DIVINATION * 16 + 0 + 1,
		"title_key": "ritual.divination",
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

	# --- Mana expansion (effect_type=9, id=145) ---
	GDRitualRecipeRegistry.register_recipe({
		"id": "ritual_mana_expand",
		"explicit_id": _MANA_EXPANSION * 16 + 0 + 1,
		"title_key": "ritual.mana_expansion",
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
