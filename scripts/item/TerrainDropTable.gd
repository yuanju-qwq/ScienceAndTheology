extends Node

const MAT_AIR       = 0
const MAT_STONE     = 1
const MAT_DIRT      = 2
const MAT_SAND      = 3
const MAT_WATER     = 4
const MAT_LAVA      = 5
const MAT_ORE_IRON  = 6
const MAT_ORE_COPPER = 7
const MAT_ORE_COAL  = 8
const MAT_WOOD      = 9
const MAT_LEAVES    = 10

static func get_drops(terrain_material: int) -> Array:
	match terrain_material:
		MAT_STONE:
			return [{item_id = ItemDatabase.mat_item(ItemDatabase.MATERIAL_STONE, ItemDatabase.FORM_DUST), count = 1}]
		MAT_DIRT:
			return [{item_id = ItemDatabase.mat_item(ItemDatabase.MATERIAL_STONE, ItemDatabase.FORM_TINY_DUST), count = 1}]
		MAT_SAND:
			return [{item_id = ItemDatabase.mat_item(ItemDatabase.MATERIAL_STONE, ItemDatabase.FORM_TINY_DUST), count = 1}]
		MAT_WOOD:
			return [{item_id = ItemDatabase.wood_log(), count = 1}]
		MAT_LEAVES:
			return []
		MAT_ORE_COAL:
			return [{item_id = ItemDatabase.coal_gem(), count = 1}]
		MAT_ORE_COPPER:
			return [{item_id = ItemDatabase.copper_crushed(), count = 1 + randi() % 2}]
		MAT_ORE_IRON:
			return [{item_id = ItemDatabase.iron_crushed(), count = 1}]
		_:
			return []

static func get_required_tool_type(terrain_material: int) -> int:
	match terrain_material:
		MAT_WOOD, MAT_LEAVES:
			return ItemDatabase.ITEM_WOODEN_AXE  # any axe
		MAT_STONE, MAT_ORE_IRON, MAT_ORE_COPPER, MAT_ORE_COAL:
			return ItemDatabase.ITEM_WOODEN_PICKAXE  # any pickaxe
		MAT_DIRT, MAT_SAND:
			return ItemDatabase.ITEM_WOODEN_SHOVEL  # any shovel
		_:
			return -1

static func get_required_mining_level(terrain_material: int) -> int:
	match terrain_material:
		MAT_DIRT, MAT_SAND, MAT_WOOD, MAT_LEAVES, MAT_ORE_COAL:
			return 0
		MAT_STONE, MAT_ORE_COPPER:
			return 1
		MAT_ORE_IRON:
			return 2
		_:
			return 0
