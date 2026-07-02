extends Node

# ============================================================
# QuestDatabase — Quest content registration
# ============================================================
#
# Registers all chapters and quests into the GDQuestSystem.
# Called at game startup after GDQuestSystem is ready.
#
# Condition types (QuestConditionType enum):
#   0 = HAS_ITEM       — player has item in inventory
#   1 = CRAFT_ITEM     — player has crafted item (tracked)
#   2 = MINE_BLOCK     — player has mined block type
#   3 = PLACE_MACHINE  — player has placed machine type
#   4 = REACH_TICK     — game tick threshold reached
#   255 = CUSTOM       — GDScript-evaluated condition
#
# Reward types (QuestRewardType enum):
#   0 = ITEM           — grant item(s)
#   1 = SELECT_ONE     — choose from multiple options
#   2 = UNLOCK_QUEST   — unlock a hidden quest

# Quest condition type constants for readability.
const CT_HAS_ITEM := 0
const CT_CRAFT_ITEM := 1
const CT_MINE_BLOCK := 2
const CT_PLACE_MACHINE := 3
const CT_REACH_TICK := 4
const CT_CUSTOM := 255

# Quest reward type constants.
const RT_ITEM := 0
const RT_SELECT_ONE := 1
const RT_UNLOCK_QUEST := 2

# Quest state constants (QuestState enum).
const QS_LOCKED := 0
const QS_AVAILABLE := 1
const QS_IN_PROGRESS := 2
const QS_COMPLETED := 3

var _quest_system: Node = null
var _loaded := false


func _ready() -> void:
	pass


# ============================================================
# Public API
# ============================================================

# Load all quest content into the given GDQuestSystem node.
# quest_system: a GDQuestSystem instance.
@warning_ignore("unsafe_method_access")
func load_content(quest_system: Node) -> void:
	if _loaded:
		return
	_quest_system = quest_system
	_loaded = true

	_register_chapters()
	_register_stone_age_quests()
	_register_bronze_age_quests()
	_register_steam_age_quests()

	_quest_system.initialize()


# ============================================================
# Helper constructors
# ============================================================

# Create a condition dictionary.
func _cond(type: int, target_key: String, target_count: int = 1,
		   condition_key: String = "") -> Dictionary:
	var d := {
		"type": type,
		"target_key": target_key,
		"target_count": target_count,
	}
	if not condition_key.is_empty():
		d["condition_key"] = condition_key
	return d


# Create an item reward dictionary.
func _reward_item(item_key: String, count: int = 1) -> Dictionary:
	return {
		"type": RT_ITEM,
		"item_key": item_key,
		"count": count,
	}


# Create a select-one reward dictionary.
func _reward_select_one(options: PackedStringArray, count: int = 1) -> Dictionary:
	return {
		"type": RT_SELECT_ONE,
		"options": options,
		"count": count,
	}


# Create an unlock-quest reward dictionary.
func _reward_unlock(quest_id: String) -> Dictionary:
	return {
		"type": RT_UNLOCK_QUEST,
		"unlock_quest_id": quest_id,
	}


# Create a quest dictionary.
func _quest(
		quest_id: String,
		chapter_id: String,
		title: String,
		description: String,
		icon_key: String,
		order_index: int,
		prerequisites: PackedStringArray = [],
		conditions: Array = [],
		rewards: Array = [],
		is_hidden: bool = false,
		can_repeat: bool = false,
		auto_start: bool = true) -> Dictionary:
	return {
		"quest_id": quest_id,
		"chapter_id": chapter_id,
		"title": title,
		"description": description,
		"icon_key": icon_key,
		"order_index": order_index,
		"prerequisites": prerequisites,
		"conditions": conditions,
		"rewards": rewards,
		"is_hidden": is_hidden,
		"can_repeat": can_repeat,
		"auto_start": auto_start,
	}


# ============================================================
# Chapter registration
# ============================================================

@warning_ignore("unsafe_method_access")
func _register_chapters() -> void:
	_quest_system.register_chapter("stone_age", "quest.chapter.stone_age", "pickaxe.stone", 0)
	_quest_system.register_chapter("bronze_age", "quest.chapter.bronze_age", "ingot.bronze", 1)
	_quest_system.register_chapter("steam_age", "quest.chapter.steam_age", "ingot.steel", 2)
	_quest_system.register_chapter("lv", "quest.chapter.lv", "circuit_basic", 3)
	_quest_system.register_chapter("mv", "quest.chapter.mv", "circuit_good", 4)
	_quest_system.register_chapter("magic", "quest.chapter.magic", "rune.fire", 10)


# ============================================================
# Stone Age quests
# ============================================================

@warning_ignore("unsafe_method_access")
func _register_stone_age_quests() -> void:
	var chapter := "stone_age"

	# --- First Steps ---
	_quest_system.register_quest(_quest(
		"stone_age.first_steps", chapter,
		"quest.stone_age.first_steps.title",
		"quest.stone_age.first_steps.desc",
		"log.oak", 0,
		[],
		[_cond(CT_MINE_BLOCK, "oak_wood", 1)],
		[_reward_item("rod.wood", 4)],
	))

	# --- Getting Stone ---
	_quest_system.register_quest(_quest(
		"stone_age.getting_stone", chapter,
		"quest.stone_age.getting_stone.title",
		"quest.stone_age.getting_stone.desc",
		"pickaxe.stone", 1,
		["stone_age.first_steps"],
		[_cond(CT_MINE_BLOCK, "stone", 8)],
		[_reward_item("dust.stone", 4)],
	))

	# --- Crafting Table ---
	_quest_system.register_quest(_quest(
		"stone_age.crafting_table", chapter,
		"quest.stone_age.crafting_table.title",
		"quest.stone_age.crafting_table.desc",
		"workbench", 2,
		["stone_age.getting_stone"],
		[
			_cond(CT_CRAFT_ITEM, "workbench", 1),
			_cond(CT_PLACE_MACHINE, "workbench", 1),
		],
		[_reward_item("plate.wood", 8)],
	))

	# --- Stone Furnace ---
	_quest_system.register_quest(_quest(
		"stone_age.stone_furnace", chapter,
		"quest.stone_age.stone_furnace.title",
		"quest.stone_age.stone_furnace.desc",
		"stone_furnace", 3,
		["stone_age.crafting_table"],
		[
			_cond(CT_CRAFT_ITEM, "stone_furnace", 1),
			_cond(CT_PLACE_MACHINE, "stone_furnace", 1),
		],
		[_reward_item("gem.coal", 16)],
	))

	# --- First Ingot ---
	_quest_system.register_quest(_quest(
		"stone_age.first_ingot", chapter,
		"quest.stone_age.first_ingot.title",
		"quest.stone_age.first_ingot.desc",
		"ingot.iron", 4,
		["stone_age.stone_furnace"],
		[_cond(CT_CRAFT_ITEM, "ingot.iron", 1)],
		[_reward_item("dust.copper", 8)],
	))

	# --- GT Hammer ---
	_quest_system.register_quest(_quest(
		"stone_age.gt_hammer", chapter,
		"quest.stone_age.gt_hammer.title",
		"quest.stone_age.gt_hammer.desc",
		"gt_hammer", 5,
		["stone_age.crafting_table"],
		[_cond(CT_CRAFT_ITEM, "gt_hammer", 1)],
		[_reward_item("plate.iron", 4)],
	))

	# --- Wrench ---
	_quest_system.register_quest(_quest(
		"stone_age.wrench", chapter,
		"quest.stone_age.wrench.title",
		"quest.stone_age.wrench.desc",
		"gt_wrench", 6,
		["stone_age.gt_hammer"],
		[_cond(CT_CRAFT_ITEM, "gt_wrench", 1)],
		[_reward_item("rod.iron", 4)],
	))

	# --- Basic Machine Hull (hidden, unlocks on first ingot) ---
	_quest_system.register_quest(_quest(
		"stone_age.machine_hull", chapter,
		"quest.stone_age.machine_hull.title",
		"quest.stone_age.machine_hull.desc",
		"machine_hull_basic", 10,
		["stone_age.first_ingot", "stone_age.gt_hammer"],
		[_cond(CT_CRAFT_ITEM, "machine_hull_basic", 1)],
		[
			_reward_item("electric_motor_lv", 1),
			_reward_unlock("stone_age.macerator_hint"),
		],
		true,  # is_hidden
	))

	# --- Macerator Hint (hidden, unlocked by machine hull reward) ---
	_quest_system.register_quest(_quest(
		"stone_age.macerator_hint", chapter,
		"quest.stone_age.macerator_hint.title",
		"quest.stone_age.macerator_hint.desc",
		"machine_hull_basic", 11,
		["stone_age.machine_hull"],
		[_cond(CT_CRAFT_ITEM, "macerator", 1)],
		[_reward_item("dust.iron", 16)],
		true,  # is_hidden
	))

	# --- Wild Foraging (hidden, discover by mining wild crops) ---
	_quest_system.register_quest(_quest(
		"stone_age.wild_foraging", chapter,
		"quest.stone_age.wild_foraging.title",
		"quest.stone_age.wild_foraging.desc",
		"crop.wheat", 12,
		[],
		[_cond(CT_MINE_BLOCK, "wheat_mature", 1)],
		[_reward_item("seed.wheat", 4)],
		true,  # is_hidden
	))

	# --- First Farm (till farmland + plant a crop) ---
	_quest_system.register_quest(_quest(
		"stone_age.first_farm", chapter,
		"quest.stone_age.first_farm.title",
		"quest.stone_age.first_farm.desc",
		"terrain.farmland", 13,
		["stone_age.wild_foraging"],
		[_cond(CT_MINE_BLOCK, "crop_planted", 1)],
		[_reward_item("bone_meal", 4)],
	))

	# --- Bone Meal (fertilize a crop to speed up growth) ---
	_quest_system.register_quest(_quest(
		"stone_age.bone_meal", chapter,
		"quest.stone_age.bone_meal.title",
		"quest.stone_age.bone_meal.desc",
		"item.bone_meal", 14,
		["stone_age.first_farm"],
		[_cond(CT_MINE_BLOCK, "crop_fertilized", 1)],
		[_reward_item("seed.potato", 4)],
	))

	# --- First Harvest (harvest a mature crop) ---
	_quest_system.register_quest(_quest(
		"stone_age.first_harvest", chapter,
		"quest.stone_age.first_harvest.title",
		"quest.stone_age.first_harvest.desc",
		"crop.wheat", 15,
		["stone_age.first_farm"],
		[_cond(CT_HAS_ITEM, "crop.wheat", 1)],
		[_reward_item("seed.carrot", 4)],
	))

	# --- Bread Making (craft bread from flour) ---
	_quest_system.register_quest(_quest(
		"stone_age.bread_making", chapter,
		"quest.stone_age.bread_making.title",
		"quest.stone_age.bread_making.desc",
		"item.bread", 16,
		["stone_age.first_harvest"],
		[_cond(CT_CRAFT_ITEM, "bread", 1)],
		[_reward_item("flour", 8)],
	))


# ============================================================
# Bronze Age quests
# ============================================================

@warning_ignore("unsafe_method_access")
func _register_bronze_age_quests() -> void:
	var chapter := "bronze_age"

	# --- Bronze Smelting ---
	_quest_system.register_quest(_quest(
		"bronze_age.bronze_smelting", chapter,
		"quest.bronze_age.bronze_smelting.title",
		"quest.bronze_age.bronze_smelting.desc",
		"ingot.bronze", 0,
		["stone_age.first_ingot"],
		[_cond(CT_CRAFT_ITEM, "ingot.bronze", 4)],
		[_reward_item("dust.tin", 8)],
	))

	# --- Bronze Machine Hull ---
	_quest_system.register_quest(_quest(
		"bronze_age.bronze_hull", chapter,
		"quest.bronze_age.bronze_hull.title",
		"quest.bronze_age.bronze_hull.desc",
		"machine_hull_basic", 1,
		["bronze_age.bronze_smelting", "stone_age.machine_hull"],
		[_cond(CT_CRAFT_ITEM, "machine_hull_advanced", 1)],
		[_reward_item("plate.bronze", 8)],
	))


# ============================================================
# Steam Age quests
# ============================================================

@warning_ignore("unsafe_method_access")
func _register_steam_age_quests() -> void:
	var chapter := "steam_age"

	# --- Steel ---
	_quest_system.register_quest(_quest(
		"steam_age.steel", chapter,
		"quest.steam_age.steel.title",
		"quest.steam_age.steel.desc",
		"ingot.steel", 0,
		["bronze_age.bronze_smelting"],
		[_cond(CT_CRAFT_ITEM, "ingot.steel", 4)],
		[_reward_item("dust.coal", 16)],
	))
