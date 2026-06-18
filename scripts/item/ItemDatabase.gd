extends Node

const ITEM_ASSET_DIR := "res://resource/items/"

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

# Planetary rock material IDs — must match C++ materials:: enum.
const MATERIAL_GRANITE     = 113
const MATERIAL_BASALT      = 114
const MATERIAL_MARBLE      = 115
const MATERIAL_SANDSTONE   = 116
const MATERIAL_SHALE       = 117
const MATERIAL_KOMATIITE   = 118
const MATERIAL_REGOLITH    = 119
const MATERIAL_ANORTHOSTIE = 120

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
const ITEM_GT_WRENCH      = K_NON_MAT_BASE + 1
const ITEM_GT_FILE        = K_NON_MAT_BASE + 2
const ITEM_GT_SCREWDRIVER = K_NON_MAT_BASE + 3
const ITEM_GT_SAW         = K_NON_MAT_BASE + 4
const ITEM_GT_WIRE_CUTTER = K_NON_MAT_BASE + 5
const ITEM_GT_CROWBAR     = K_NON_MAT_BASE + 6
const ITEM_GT_SOFT_MALLET = K_NON_MAT_BASE + 7
const ITEM_GT_HARD_HAMMER = K_NON_MAT_BASE + 8

const ITEM_MACHINE_HULL_BASIC    = K_NON_MAT_BASE + 10
const ITEM_MACHINE_HULL_ADVANCED = K_NON_MAT_BASE + 11
const ITEM_ELECTRIC_MOTOR_LV     = K_NON_MAT_BASE + 12
const ITEM_ELECTRIC_PISTON_LV    = K_NON_MAT_BASE + 13
const ITEM_ROBOT_ARM_LV          = K_NON_MAT_BASE + 14
const ITEM_CONVEYOR_MODULE_LV    = K_NON_MAT_BASE + 15
const ITEM_PUMP_LV               = K_NON_MAT_BASE + 16
const ITEM_EMPTY_FLUID_CELL      = K_NON_MAT_BASE + 17

const ITEM_VACUUM_TUBE       = K_NON_MAT_BASE + 20
const ITEM_CIRCUIT_PRIMITIVE = K_NON_MAT_BASE + 21
const ITEM_CIRCUIT_BASIC     = K_NON_MAT_BASE + 22
const ITEM_CIRCUIT_GOOD      = K_NON_MAT_BASE + 23
const ITEM_CIRCUIT_ADVANCED  = K_NON_MAT_BASE + 24

const ITEM_COAL_BLOCK    = K_NON_MAT_BASE + 30
const ITEM_COKE_BRICK    = K_NON_MAT_BASE + 31
const ITEM_FIREBRICK     = K_NON_MAT_BASE + 32
const ITEM_STONE_PLATE   = K_NON_MAT_BASE + 33
const ITEM_WOOD_PLATE    = K_NON_MAT_BASE + 34
const ITEM_BLANK_PATTERN = K_NON_MAT_BASE + 35

// Space station blueprint item.
const ITEM_STATION_BLUEPRINT = K_NON_MAT_BASE + 55

// Tree species items: log, plank, sapling, fruit per species.
// These are non-material items with unique colors per species.
const ITEM_OAK_LOG      = K_NON_MAT_BASE + 60
const ITEM_OAK_PLANK    = K_NON_MAT_BASE + 61
const ITEM_OAK_SAPLING  = K_NON_MAT_BASE + 62
const ITEM_BIRCH_LOG      = K_NON_MAT_BASE + 63
const ITEM_BIRCH_PLANK    = K_NON_MAT_BASE + 64
const ITEM_BIRCH_SAPLING  = K_NON_MAT_BASE + 65
const ITEM_SPRUCE_LOG      = K_NON_MAT_BASE + 66
const ITEM_SPRUCE_PLANK    = K_NON_MAT_BASE + 67
const ITEM_SPRUCE_SAPLING  = K_NON_MAT_BASE + 68
const ITEM_ACACIA_LOG      = K_NON_MAT_BASE + 69
const ITEM_ACACIA_PLANK    = K_NON_MAT_BASE + 70
const ITEM_ACACIA_SAPLING  = K_NON_MAT_BASE + 71
const ITEM_MAPLE_LOG      = K_NON_MAT_BASE + 72
const ITEM_MAPLE_PLANK    = K_NON_MAT_BASE + 73
const ITEM_MAPLE_SAPLING  = K_NON_MAT_BASE + 74
const ITEM_SEQUOIA_LOG      = K_NON_MAT_BASE + 75
const ITEM_SEQUOIA_PLANK    = K_NON_MAT_BASE + 76
const ITEM_SEQUOIA_SAPLING  = K_NON_MAT_BASE + 77
const ITEM_CHERRY_LOG      = K_NON_MAT_BASE + 78
const ITEM_CHERRY_PLANK    = K_NON_MAT_BASE + 79
const ITEM_CHERRY_SAPLING  = K_NON_MAT_BASE + 80
const ITEM_CHERRY_FRUIT    = K_NON_MAT_BASE + 81
const ITEM_OLIVE_LOG      = K_NON_MAT_BASE + 82
const ITEM_OLIVE_PLANK    = K_NON_MAT_BASE + 83
const ITEM_OLIVE_SAPLING  = K_NON_MAT_BASE + 84
const ITEM_OLIVE_FRUIT    = K_NON_MAT_BASE + 85

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
	_register_component_items()
	_register_survival_items()
	_register_tree_species_items()

func get_item(item_id: int) -> ItemDef:
	return _items.get(item_id)

func get_tool_stats(item_id: int) -> ToolDef:
	return _tool_stats.get(item_id)

func is_valid_item(item_id: int) -> bool:
	return _items.has(item_id)

func _load_icon(icon_file: String, fallback_color: Color) -> Texture2D:
	if icon_file.is_empty():
		return _make_placeholder_icon(fallback_color)

	var icon := load(ITEM_ASSET_DIR + icon_file) as Texture2D
	if icon == null:
		push_warning("ItemDatabase: missing item icon '%s'" % icon_file)
		return _make_placeholder_icon(fallback_color)
	return icon


func _register(item_id: int, name: String, icon_color: Color, max_stack: int = 64,
		tool: ToolDef = null, icon_file: String = "") -> void:
	var def := ItemDef.new()
	def.item_id = item_id
	def.display_name = name
	def.icon = _load_icon(icon_file, icon_color)
	def.max_stack = max_stack
	def.tool_stats = tool
	_items[item_id] = def
	if tool:
		_tool_stats[item_id] = tool

func _register_material_items() -> void:
	_register(mat_item(MATERIAL_WOOD, FORM_DUST), "Wood Log", Color(0.55, 0.35, 0.15),
			64, null, "materials/wood_log_icon_32.png")
	_register(mat_item(MATERIAL_WOOD, FORM_PLATE), "Wood Plank", Color(0.70, 0.50, 0.25),
			64, null, "materials/wood_plank_icon_32.png")
	_register(mat_item(MATERIAL_WOOD, FORM_ROD), "Stick", Color(0.60, 0.40, 0.20),
			64, null, "materials/stick_icon_32.png")
	_register(mat_item(MATERIAL_STONE, FORM_DUST), "Stone Dust", Color(0.50, 0.50, 0.50),
			64, null, "materials/stone_dust_icon_32.png")
	_register(mat_item(MATERIAL_STONE, FORM_TINY_DUST), "Stone Tiny Dust", Color(0.42, 0.42, 0.42),
			64, null, "materials/stone_tiny_dust_icon_32.png")
	_register(mat_item(MATERIAL_COAL, FORM_GEM), "Coal", Color(0.10, 0.10, 0.10),
			64, null, "materials/coal_icon_32.png")
	_register(mat_item(MATERIAL_COPPER, FORM_CRUSHED), "Crushed Copper", Color(0.80, 0.40, 0.10),
			64, null, "materials/crushed_copper_icon_32.png")
	_register(mat_item(MATERIAL_IRON, FORM_CRUSHED), "Crushed Iron", Color(0.70, 0.55, 0.45),
			64, null, "materials/crushed_iron_icon_32.png")
	_register(mat_item(MATERIAL_COPPER, FORM_DUST), "Copper Dust", Color(0.80, 0.50, 0.15),
			64, null, "materials/copper_dust_icon_32.png")
	_register(mat_item(MATERIAL_IRON, FORM_DUST), "Iron Dust", Color(0.65, 0.60, 0.55),
			64, null, "materials/iron_dust_icon_32.png")

	# --- Planetary rock dusts ---
	_register(mat_item(MATERIAL_GRANITE, FORM_DUST), "Granite Dust", Color(0.63, 0.60, 0.56),
			64, null, "")
	_register(mat_item(MATERIAL_GRANITE, FORM_TINY_DUST), "Granite Tiny Dust", Color(0.53, 0.50, 0.46),
			64, null, "")
	_register(mat_item(MATERIAL_BASALT, FORM_DUST), "Basalt Dust", Color(0.31, 0.31, 0.31),
			64, null, "")
	_register(mat_item(MATERIAL_BASALT, FORM_TINY_DUST), "Basalt Tiny Dust", Color(0.26, 0.26, 0.26),
			64, null, "")
	_register(mat_item(MATERIAL_MARBLE, FORM_DUST), "Marble Dust", Color(0.91, 0.88, 0.85),
			64, null, "")
	_register(mat_item(MATERIAL_MARBLE, FORM_TINY_DUST), "Marble Tiny Dust", Color(0.81, 0.78, 0.75),
			64, null, "")
	_register(mat_item(MATERIAL_SANDSTONE, FORM_DUST), "Sandstone Dust", Color(0.78, 0.66, 0.44),
			64, null, "")
	_register(mat_item(MATERIAL_SANDSTONE, FORM_TINY_DUST), "Sandstone Tiny Dust", Color(0.68, 0.56, 0.34),
			64, null, "")
	_register(mat_item(MATERIAL_SHALE, FORM_DUST), "Shale Dust", Color(0.35, 0.38, 0.31),
			64, null, "")
	_register(mat_item(MATERIAL_SHALE, FORM_TINY_DUST), "Shale Tiny Dust", Color(0.28, 0.30, 0.24),
			64, null, "")
	_register(mat_item(MATERIAL_KOMATIITE, FORM_DUST), "Komatiite Dust", Color(0.23, 0.31, 0.19),
			64, null, "")
	_register(mat_item(MATERIAL_KOMATIITE, FORM_TINY_DUST), "Komatiite Tiny Dust", Color(0.18, 0.24, 0.14),
			64, null, "")
	_register(mat_item(MATERIAL_REGOLITH, FORM_DUST), "Regolith Dust", Color(0.63, 0.38, 0.25),
			64, null, "")
	_register(mat_item(MATERIAL_REGOLITH, FORM_TINY_DUST), "Regolith Tiny Dust", Color(0.53, 0.30, 0.18),
			64, null, "")
	_register(mat_item(MATERIAL_ANORTHOSTIE, FORM_DUST), "Anorthosite Dust", Color(0.75, 0.75, 0.78),
			64, null, "")
	_register(mat_item(MATERIAL_ANORTHOSTIE, FORM_TINY_DUST), "Anorthosite Tiny Dust", Color(0.65, 0.65, 0.68),
			64, null, "")

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

	_register(ITEM_WOODEN_PICKAXE, "Wooden Pickaxe", brown, 1,
			mk.call(PICK, W, 0, 1.5, 60, 2.0), "tools/wooden_pickaxe_icon_32.png")
	_register(ITEM_STONE_PICKAXE, "Stone Pickaxe", gray, 1,
			mk.call(PICK, S, 1, 2.0, 130, 3.0), "tools/stone_pickaxe_icon_32.png")
	_register(ITEM_IRON_PICKAXE, "Iron Pickaxe", silver, 1,
			mk.call(PICK, I, 2, 3.0, 250, 4.0), "tools/iron_pickaxe_icon_32.png")
	_register(ITEM_WOODEN_AXE, "Wooden Axe", brown, 1,
			mk.call(AXE, W, 0, 1.5, 60, 3.0), "tools/wooden_axe_icon_32.png")
	_register(ITEM_STONE_AXE, "Stone Axe", gray, 1,
			mk.call(AXE, S, 1, 2.0, 130, 4.0), "tools/stone_axe_icon_32.png")
	_register(ITEM_IRON_AXE, "Iron Axe", silver, 1,
			mk.call(AXE, I, 2, 3.0, 250, 5.0), "tools/iron_axe_icon_32.png")
	_register(ITEM_WOODEN_SHOVEL, "Wooden Shovel", brown, 1,
			mk.call(SHOVEL, W, 0, 1.5, 60, 1.5), "tools/wooden_shovel_icon_32.png")
	_register(ITEM_STONE_SHOVEL, "Stone Shovel", gray, 1,
			mk.call(SHOVEL, S, 1, 2.0, 130, 2.5), "tools/stone_shovel_icon_32.png")
	_register(ITEM_IRON_SHOVEL, "Iron Shovel", silver, 1,
			mk.call(SHOVEL, I, 2, 3.0, 250, 3.5), "tools/iron_shovel_icon_32.png")
	_register(ITEM_WOODEN_SWORD, "Wooden Sword", brown, 1,
			mk.call(SWORD, W, 0, 1.0, 60, 4.0), "tools/wooden_sword_icon_32.png")
	_register(ITEM_STONE_SWORD, "Stone Sword", gray, 1,
			mk.call(SWORD, S, 1, 1.0, 130, 5.0), "tools/stone_sword_icon_32.png")
	_register(ITEM_IRON_SWORD, "Iron Sword", silver, 1,
			mk.call(SWORD, I, 2, 1.0, 250, 6.0), "tools/iron_sword_icon_32.png")


func _register_component_items() -> void:
	_register(ITEM_GT_HAMMER, "Hammer", Color(0.62, 0.62, 0.65), 1, null, "tools/gt_hammer_icon_32.png")
	_register(ITEM_GT_WRENCH, "Wrench", Color(0.58, 0.60, 0.62), 1, null, "tools/gt_wrench_icon_32.png")
	_register(ITEM_GT_FILE, "File", Color(0.52, 0.52, 0.54), 1, null, "tools/gt_file_icon_32.png")
	_register(ITEM_GT_SCREWDRIVER, "Screwdriver", Color(0.62, 0.42, 0.20),
			1, null, "tools/gt_screwdriver_icon_32.png")
	_register(ITEM_GT_SAW, "Saw", Color(0.60, 0.58, 0.54), 1, null, "tools/gt_saw_icon_32.png")
	_register(ITEM_GT_WIRE_CUTTER, "Wire Cutter", Color(0.52, 0.54, 0.56),
			1, null, "tools/gt_wire_cutter_icon_32.png")
	_register(ITEM_GT_CROWBAR, "Crowbar", Color(0.45, 0.20, 0.15), 1, null, "tools/gt_crowbar_icon_32.png")
	_register(ITEM_GT_SOFT_MALLET, "Soft Mallet", Color(0.18, 0.18, 0.18),
			1, null, "tools/gt_soft_mallet_icon_32.png")
	_register(ITEM_GT_HARD_HAMMER, "Hard Hammer", Color(0.56, 0.56, 0.60),
			1, null, "tools/gt_hard_hammer_icon_32.png")

	_register(ITEM_MACHINE_HULL_BASIC, "Basic Machine Hull", Color(0.34, 0.36, 0.38),
			64, null, "components/basic_machine_hull_icon_32.png")
	_register(ITEM_MACHINE_HULL_ADVANCED, "Advanced Machine Hull", Color(0.30, 0.42, 0.46),
			64, null, "components/advanced_machine_hull_icon_32.png")
	_register(ITEM_ELECTRIC_MOTOR_LV, "LV Electric Motor", Color(0.55, 0.38, 0.20),
			64, null, "components/lv_electric_motor_icon_32.png")
	_register(ITEM_ELECTRIC_PISTON_LV, "LV Electric Piston", Color(0.48, 0.48, 0.50),
			64, null, "components/lv_electric_piston_icon_32.png")
	_register(ITEM_ROBOT_ARM_LV, "LV Robot Arm", Color(0.48, 0.42, 0.34),
			64, null, "components/lv_robot_arm_icon_32.png")
	_register(ITEM_CONVEYOR_MODULE_LV, "LV Conveyor Module", Color(0.22, 0.24, 0.24),
			64, null, "components/lv_conveyor_module_icon_32.png")
	_register(ITEM_PUMP_LV, "LV Pump", Color(0.38, 0.44, 0.46), 64, null, "components/lv_pump_icon_32.png")
	_register(ITEM_EMPTY_FLUID_CELL, "Empty Fluid Cell", Color(0.62, 0.64, 0.68),
			64, null, "components/empty_fluid_cell_icon_32.png")

	_register(ITEM_VACUUM_TUBE, "Vacuum Tube", Color(0.72, 0.45, 0.18),
			64, null, "circuits/vacuum_tube_icon_32.png")
	_register(ITEM_CIRCUIT_PRIMITIVE, "Primitive Circuit", Color(0.42, 0.30, 0.18),
			64, null, "circuits/primitive_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_BASIC, "Basic Circuit", Color(0.20, 0.46, 0.30),
			64, null, "circuits/basic_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_GOOD, "Good Circuit", Color(0.16, 0.50, 0.48),
			64, null, "circuits/good_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_ADVANCED, "Advanced Circuit", Color(0.14, 0.34, 0.52),
			64, null, "circuits/advanced_circuit_icon_32.png")

	_register(ITEM_COAL_BLOCK, "Coal Block", Color(0.10, 0.10, 0.11),
			64, null, "components/coal_block_icon_32.png")
	_register(ITEM_COKE_BRICK, "Coke Brick", Color(0.18, 0.17, 0.16),
			64, null, "components/coke_brick_icon_32.png")
	_register(ITEM_FIREBRICK, "Firebrick", Color(0.58, 0.24, 0.12),
			64, null, "components/firebrick_icon_32.png")
	_register(ITEM_STONE_PLATE, "Stone Plate", Color(0.46, 0.46, 0.47),
			64, null, "components/stone_plate_icon_32.png")
	_register(ITEM_WOOD_PLATE, "Wood Plate", Color(0.58, 0.38, 0.18),
			64, null, "components/wood_plate_icon_32.png")
	_register(ITEM_BLANK_PATTERN, "Blank Pattern", Color(0.24, 0.34, 0.38),
			64, null, "components/blank_pattern_icon_32.png")

func _register_survival_items() -> void:
	_register(ITEM_WORKBENCH, "Workbench", Color(0.50, 0.35, 0.20),
			64, null, "placeables/workbench_icon_32.png")
	_register(ITEM_FURNACE, "Stone Furnace", Color(0.45, 0.35, 0.25),
			64, null, "placeables/stone_furnace_icon_32.png")
	_register(ITEM_LADDER, "Ladder", Color(0.55, 0.30, 0.15),
			64, null, "placeables/ladder_icon_32.png")
	_register(ITEM_STATION_BLUEPRINT, "Station Blueprint", Color(0.20, 0.50, 0.80),
			1, null, "")


func _register_tree_species_items() -> void:
	// Oak: brown wood, green leaves.
	_register(ITEM_OAK_LOG, "Oak Log", Color(0.45, 0.27, 0.12), 64, null, "trees/log_oak_icon_64.png")
	_register(ITEM_OAK_PLANK, "Oak Plank", Color(0.60, 0.40, 0.18), 64, null, "trees/plank_oak_icon_64.png")
	_register(ITEM_OAK_SAPLING, "Oak Sapling", Color(0.30, 0.55, 0.15), 64, null, "trees/sapling_oak_icon_64.png")

	// Birch: white wood, light green leaves.
	_register(ITEM_BIRCH_LOG, "Birch Log", Color(0.80, 0.78, 0.70), 64, null, "trees/log_birch_icon_64.png")
	_register(ITEM_BIRCH_PLANK, "Birch Plank", Color(0.85, 0.82, 0.72), 64, null, "trees/plank_birch_icon_64.png")
	_register(ITEM_BIRCH_SAPLING, "Birch Sapling", Color(0.35, 0.60, 0.20), 64, null, "trees/sapling_birch_icon_64.png")

	// Spruce: dark brown wood, dark green leaves.
	_register(ITEM_SPRUCE_LOG, "Spruce Log", Color(0.35, 0.22, 0.10), 64, null, "trees/log_spruce_icon_64.png")
	_register(ITEM_SPRUCE_PLANK, "Spruce Plank", Color(0.45, 0.30, 0.15), 64, null, "trees/plank_spruce_icon_64.png")
	_register(ITEM_SPRUCE_SAPLING, "Spruce Sapling", Color(0.12, 0.35, 0.15), 64, null, "trees/sapling_spruce_icon_64.png")

	// Acacia: warm brown wood, olive green leaves.
	_register(ITEM_ACACIA_LOG, "Acacia Log", Color(0.55, 0.35, 0.15), 64, null, "trees/log_acacia_icon_64.png")
	_register(ITEM_ACACIA_PLANK, "Acacia Plank", Color(0.65, 0.42, 0.18), 64, null, "trees/plank_acacia_icon_64.png")
	_register(ITEM_ACACIA_SAPLING, "Acacia Sapling", Color(0.35, 0.55, 0.15), 64, null, "trees/sapling_acacia_icon_64.png")

	// Maple: reddish brown wood, bright green leaves.
	_register(ITEM_MAPLE_LOG, "Maple Log", Color(0.42, 0.25, 0.10), 64, null, "trees/log_maple_icon_64.png")
	_register(ITEM_MAPLE_PLANK, "Maple Plank", Color(0.55, 0.32, 0.14), 64, null, "trees/plank_maple_icon_64.png")
	_register(ITEM_MAPLE_SAPLING, "Maple Sapling", Color(0.28, 0.52, 0.18), 64, null, "trees/sapling_maple_icon_64.png")

	// Sequoia: deep red-brown wood, dark green leaves.
	_register(ITEM_SEQUOIA_LOG, "Sequoia Log", Color(0.40, 0.23, 0.10), 64, null, "trees/log_sequoia_icon_64.png")
	_register(ITEM_SEQUOIA_PLANK, "Sequoia Plank", Color(0.50, 0.28, 0.12), 64, null, "trees/plank_sequoia_icon_64.png")
	_register(ITEM_SEQUOIA_SAPLING, "Sequoia Sapling", Color(0.15, 0.38, 0.12), 64, null, "trees/sapling_sequoia_icon_64.png")

	// Cherry: pinkish wood, pink leaves, fruit-bearing.
	_register(ITEM_CHERRY_LOG, "Cherry Log", Color(0.50, 0.28, 0.22), 64, null, "trees/log_cherry_icon_64.png")
	_register(ITEM_CHERRY_PLANK, "Cherry Plank", Color(0.60, 0.35, 0.28), 64, null, "trees/plank_cherry_icon_64.png")
	_register(ITEM_CHERRY_SAPLING, "Cherry Sapling", Color(0.65, 0.30, 0.45), 64, null, "trees/sapling_cherry_icon_64.png")
	_register(ITEM_CHERRY_FRUIT, "Cherry", Color(0.85, 0.15, 0.25), 64, null, "trees/fruit_cherry_icon_64.png")

	// Olive: grey-brown wood, silvery green leaves, fruit-bearing.
	_register(ITEM_OLIVE_LOG, "Olive Log", Color(0.52, 0.40, 0.22), 64, null, "trees/log_olive_icon_64.png")
	_register(ITEM_OLIVE_PLANK, "Olive Plank", Color(0.60, 0.48, 0.28), 64, null, "trees/plank_olive_icon_64.png")
	_register(ITEM_OLIVE_SAPLING, "Olive Sapling", Color(0.28, 0.42, 0.18), 64, null, "trees/sapling_olive_icon_64.png")
	_register(ITEM_OLIVE_FRUIT, "Olive", Color(0.55, 0.58, 0.20), 64, null, "trees/fruit_olive_icon_64.png")
