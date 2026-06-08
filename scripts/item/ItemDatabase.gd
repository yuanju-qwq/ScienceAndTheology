extends Node

const FORM_DUST        = 0
const FORM_TINY_DUST   = 1
const FORM_CRUSHED     = 5
const FORM_GEM         = 8
const FORM_INGOT       = 12
const FORM_NUGGET      = 14
const FORM_BLOCK       = 15
const FORM_PLATE       = 16
const FORM_ROD         = 19
const FORM_WIRE        = 28

const MATERIAL_STONE   = 0
const MATERIAL_COAL    = 2
const MATERIAL_COPPER  = 5
const MATERIAL_IRON    = 7
const MATERIAL_WOOD    = 112

const K_FORM_COUNT     = 31
const K_MAT_ITEM_BASE  = 1
const K_NON_MAT_BASE   = K_MAT_ITEM_BASE + 113 * K_FORM_COUNT + 1

const ITEM_WORKBENCH   = K_NON_MAT_BASE + 52
const ITEM_FURNACE     = K_NON_MAT_BASE + 53
const ITEM_LADDER      = K_NON_MAT_BASE + 54
const ITEM_WOODEN_PICKAXE = K_NON_MAT_BASE + 36
const ITEM_STONE_PICKAXE  = K_NON_MAT_BASE + 37
const ITEM_IRON_PICKAXE   = K_NON_MAT_BASE + 38
const ITEM_WOODEN_AXE     = K_NON_MAT_BASE + 40
const ITEM_STONE_AXE      = K_NON_MAT_BASE + 41
const ITEM_IRON_AXE       = K_NON_MAT_BASE + 42
const ITEM_WOODEN_SHOVEL  = K_NON_MAT_BASE + 44
const ITEM_STONE_SHOVEL   = K_NON_MAT_BASE + 45
const ITEM_IRON_SHOVEL    = K_NON_MAT_BASE + 46
const ITEM_WOODEN_SWORD   = K_NON_MAT_BASE + 48
const ITEM_STONE_SWORD    = K_NON_MAT_BASE + 49
const ITEM_IRON_SWORD     = K_NON_MAT_BASE + 50
const ITEM_GT_HAMMER      = K_NON_MAT_BASE + 0
const ITEM_GT_SAW         = K_NON_MAT_BASE + 4

static func mat_item(mat_id: int, form: int) -> int:
	return K_MAT_ITEM_BASE + mat_id * K_FORM_COUNT + form

static func wood_log() -> int:    return mat_item(MATERIAL_WOOD, FORM_DUST)
static func wood_plank() -> int:  return mat_item(MATERIAL_WOOD, FORM_PLATE)
static func stick() -> int:       return mat_item(MATERIAL_WOOD, FORM_ROD)
static func stone_dust() -> int:  return mat_item(MATERIAL_STONE, FORM_DUST)
static func coal_gem() -> int:    return mat_item(MATERIAL_COAL, FORM_GEM)
static func copper_crushed() -> int: return mat_item(MATERIAL_COPPER, FORM_CRUSHED)
static func iron_crushed() -> int:   return mat_item(MATERIAL_IRON, FORM_CRUSHED)

static func _make_placeholder_icon(color: Color, size: int = 16) -> ImageTexture:
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var gray := Color(color.r * 0.7, color.g * 0.7, color.b * 0.7, 1.0)
	for y in size:
		for x in size:
			var border := x == 0 or y == 0 or x == size - 1 or y == size - 1
			var check := (x / 4 + y / 4) % 2 == 0
			var c := gray if border else (color if check else color.darkened(0.15))
			image.set_pixel(x, y, c)
	var tex := ImageTexture.create_from_image(image)
	return tex

var _items: Dictionary = {}   # item_id -> ItemDef
var _tool_stats: Dictionary = {}  # item_id -> ToolDef

func _ready() -> void:
	_register_material_items()
	_register_tool_items()
	_register_survival_items()

func get_item(item_id: int) -> ItemDef:
	return _items.get(item_id)

func get_tool_stats(item_id: int) -> ToolDef:
	return _tool_stats.get(item_id)

func is_valid_item(item_id: int) -> bool:
	return _items.has(item_id)

func _register(item_id: int, name: String, icon_color: Color, max_stack: int = 64, tool: ToolDef = null) -> void:
	var def := ItemDef.new()
	def.item_id = item_id
	def.display_name = name
	def.icon = _make_placeholder_icon(icon_color)
	def.max_stack = max_stack
	def.tool_stats = tool
	_items[item_id] = def
	if tool:
		_tool_stats[item_id] = tool

func _register_material_items() -> void:
	_register(mat_item(MATERIAL_WOOD, FORM_DUST),       "Wood Log",      Color(0.55, 0.35, 0.15))
	_register(mat_item(MATERIAL_WOOD, FORM_PLATE),      "Wood Plank",    Color(0.70, 0.50, 0.25))
	_register(mat_item(MATERIAL_WOOD, FORM_ROD),        "Stick",         Color(0.60, 0.40, 0.20))
	_register(mat_item(MATERIAL_STONE, FORM_DUST),      "Stone Dust",    Color(0.50, 0.50, 0.50))
	_register(mat_item(MATERIAL_COAL, FORM_GEM),        "Coal",          Color(0.10, 0.10, 0.10))
	_register(mat_item(MATERIAL_COPPER, FORM_CRUSHED),  "Crushed Copper", Color(0.80, 0.40, 0.10))
	_register(mat_item(MATERIAL_IRON, FORM_CRUSHED),    "Crushed Iron",   Color(0.70, 0.55, 0.45))
	_register(mat_item(MATERIAL_COPPER, FORM_DUST),     "Copper Dust",   Color(0.80, 0.50, 0.15))
	_register(mat_item(MATERIAL_IRON, FORM_DUST),       "Iron Dust",     Color(0.65, 0.60, 0.55))

func _make_tool_def(tool_type: int, tier: int, mining_level: int, speed: float, durability: int, attack: float) -> ToolDef:
	var t := ToolDef.new()
	t.tool_type = tool_type
	t.tier = tier
	t.mining_level = mining_level
	t.speed = speed
	t.durability = durability
	t.attack_damage = attack
	return t

func _register_tool_items() -> void:
	var mk := _make_tool_def
	var PICK := ToolDef.ToolType.PICKAXE
	var AXE := ToolDef.ToolType.AXE
	var SHOVEL := ToolDef.ToolType.SHOVEL
	var SWORD := ToolDef.ToolType.SWORD
	var W := ToolDef.Tier.WOOD
	var S := ToolDef.Tier.STONE
	var I := ToolDef.Tier.IRON

	var brown := Color(0.55, 0.35, 0.15)
	var gray := Color(0.60, 0.60, 0.60)
	var silver := Color(0.75, 0.75, 0.80)

	_register(ITEM_WOODEN_PICKAXE, "Wooden Pickaxe", brown, 1, mk.call(PICK, W, 0, 1.5, 60, 2.0))
	_register(ITEM_STONE_PICKAXE,  "Stone Pickaxe",  gray,  1, mk.call(PICK, S, 1, 2.0, 130, 3.0))
	_register(ITEM_IRON_PICKAXE,   "Iron Pickaxe",   silver, 1, mk.call(PICK, I, 2, 3.0, 250, 4.0))
	_register(ITEM_WOODEN_AXE,     "Wooden Axe",     brown,  1, mk.call(AXE, W, 0, 1.5, 60, 3.0))
	_register(ITEM_STONE_AXE,      "Stone Axe",      gray,   1, mk.call(AXE, S, 1, 2.0, 130, 4.0))
	_register(ITEM_IRON_AXE,       "Iron Axe",       silver, 1, mk.call(AXE, I, 2, 3.0, 250, 5.0))
	_register(ITEM_WOODEN_SHOVEL,  "Wooden Shovel",  brown,  1, mk.call(SHOVEL, W, 0, 1.5, 60, 1.5))
	_register(ITEM_STONE_SHOVEL,   "Stone Shovel",   gray,   1, mk.call(SHOVEL, S, 1, 2.0, 130, 2.5))
	_register(ITEM_IRON_SHOVEL,    "Iron Shovel",    silver, 1, mk.call(SHOVEL, I, 2, 3.0, 250, 3.5))
	_register(ITEM_WOODEN_SWORD,   "Wooden Sword",   brown,  1, mk.call(SWORD, W, 0, 1.0, 60, 4.0))
	_register(ITEM_STONE_SWORD,    "Stone Sword",    gray,   1, mk.call(SWORD, S, 1, 1.0, 130, 5.0))
	_register(ITEM_IRON_SWORD,     "Iron Sword",     silver, 1, mk.call(SWORD, I, 2, 1.0, 250, 6.0))

func _register_survival_items() -> void:
	_register(ITEM_WORKBENCH, "Workbench", Color(0.50, 0.35, 0.20), 64)
	_register(ITEM_FURNACE, "Stone Furnace", Color(0.45, 0.35, 0.25), 64)
	_register(ITEM_LADDER, "Ladder", Color(0.55, 0.30, 0.15), 64)
