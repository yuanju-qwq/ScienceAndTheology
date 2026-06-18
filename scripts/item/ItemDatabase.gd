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

const ITEM_WORKBENCH   = K_NON_MAT_BASE + 52
const ITEM_FURNACE     = K_NON_MAT_BASE + 53
const ITEM_LADDER      = K_NON_MAT_BASE + 54
# Space station blueprint item.
const ITEM_STATION_BLUEPRINT = K_NON_MAT_BASE + 55

# Tree species items: log, plank, sapling, fruit per species.
# These are non-material items with unique colors per species.
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

# Source law creature drop items (V0.6 源律升华体系).
# These items are dropped by killing proxy creatures.
# Item keys use the "snt:" prefix to match C++ CreatureDropDef.item_key.
const ITEM_GLOW_DEER_ANTLER        = K_NON_MAT_BASE + 100
const ITEM_PURIFYING_POLLEN        = K_NON_MAT_BASE + 101
const ITEM_ROCK_LIZARD_SCALE       = K_NON_MAT_BASE + 102
const ITEM_CRYSTALLIZED_BONE_POWDER = K_NON_MAT_BASE + 103
const ITEM_THUNDERBIRD_FEATHER     = K_NON_MAT_BASE + 104
const ITEM_MAGNETIC_CRYSTAL_SHARD  = K_NON_MAT_BASE + 105
const ITEM_SEA_SERPENT_SCALE       = K_NON_MAT_BASE + 106
const ITEM_TIDAL_GLAND             = K_NON_MAT_BASE + 107
const ITEM_BLAZING_CORE            = K_NON_MAT_BASE + 108
const ITEM_MOLTEN_BLOOD_SAMPLE     = K_NON_MAT_BASE + 109
const ITEM_AETHER_FRAGMENT         = K_NON_MAT_BASE + 110
const ITEM_BLUEPRINT_SHARD         = K_NON_MAT_BASE + 111
const ITEM_ABERRANT_ORGAN          = K_NON_MAT_BASE + 112
const ITEM_POLLUTED_SOURCE_ESSENCE = K_NON_MAT_BASE + 113

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
			@warning_ignore("integer_division")
			var check := (x / 4 + y / 4) % 2 == 0
			var c := gray if border else (color if check else color.darkened(0.15))
			image.set_pixel(x, y, c)
	var tex := ImageTexture.create_from_image(image)
	return tex

var _items: Dictionary = {}   # item_id -> ItemDef
var _tool_stats: Dictionary = {}  # item_id -> ToolDef
var _key_to_id: Dictionary = {}   # item_key (String) -> item_id (int)
var _id_to_key: Dictionary = {}   # item_id (int) -> item_key (String)

func _ready() -> void:
	_register_material_items()
	_register_tool_items()
	_register_component_items()
	_register_survival_items()
	_register_tree_species_items()
	_register_source_law_drops()
	_register_non_material_keys()
	_register_material_item_keys()

func get_item(item_id: int) -> ItemDef:
	return _items.get(item_id)

func get_tool_stats(item_id: int) -> ToolDef:
	return _tool_stats.get(item_id)

func is_valid_item(item_id: int) -> bool:
	return _items.has(item_id)

# Look up an item_id by its item_key string (e.g. "snt:glow_deer_antler").
# Returns -1 if not found.
func get_item_id_by_key(item_key: String) -> int:
	return _key_to_id.get(item_key, -1)


# Look up an item_key by its item_id. Returns "" if not found.
func get_item_key_by_id(item_id: int) -> String:
	return _id_to_key.get(item_id, "")

func _load_icon(icon_file: String, fallback_color: Color) -> Texture2D:
	if icon_file.is_empty():
		return _make_placeholder_icon(fallback_color)

	var icon := load(ITEM_ASSET_DIR + icon_file) as Texture2D
	if icon == null:
		push_warning("ItemDatabase: missing item icon '%s'" % icon_file)
		return _make_placeholder_icon(fallback_color)
	return icon


func _register(item_id: int, title_key: String, icon_color: Color, max_stack: int = 64,
		tool: ToolDef = null, icon_file: String = "") -> void:
	var def := ItemDef.new()
	def.item_id = item_id
	def.title_key = title_key
	def.icon = _load_icon(icon_file, icon_color)
	def.max_stack = max_stack
	def.tool_stats = tool
	_items[item_id] = def
	if tool:
		_tool_stats[item_id] = tool
	# Populate _id_to_key with key from C++ convention if known.
	# Non-material items are populated in _register_drop() and
	# the _register_non_material_keys() helper.
	# Material item keys are computed as "{form_name}.{material_name}".
	# Material items use composite keys; they are handled separately
	# if needed. For now, register_drop items are the primary sources.

func _register_material_items() -> void:
	_register(mat_item(MATERIAL_WOOD, FORM_DUST), "item.wood_log", Color(0.55, 0.35, 0.15),
			64, null, "materials/wood_log_icon_32.png")
	_register(mat_item(MATERIAL_WOOD, FORM_PLATE), "item.wood_plank", Color(0.70, 0.50, 0.25),
			64, null, "materials/wood_plank_icon_32.png")
	_register(mat_item(MATERIAL_WOOD, FORM_ROD), "item.stick", Color(0.60, 0.40, 0.20),
			64, null, "materials/stick_icon_32.png")
	_register(mat_item(MATERIAL_STONE, FORM_DUST), "item.stone_dust", Color(0.50, 0.50, 0.50),
			64, null, "materials/stone_dust_icon_32.png")
	_register(mat_item(MATERIAL_STONE, FORM_TINY_DUST), "item.stone_tiny_dust", Color(0.42, 0.42, 0.42),
			64, null, "materials/stone_tiny_dust_icon_32.png")
	_register(mat_item(MATERIAL_COAL, FORM_GEM), "item.coal", Color(0.10, 0.10, 0.10),
			64, null, "materials/coal_icon_32.png")
	_register(mat_item(MATERIAL_COPPER, FORM_CRUSHED), "item.crushed_copper", Color(0.80, 0.40, 0.10),
			64, null, "materials/crushed_copper_icon_32.png")
	_register(mat_item(MATERIAL_IRON, FORM_CRUSHED), "item.crushed_iron", Color(0.70, 0.55, 0.45),
			64, null, "materials/crushed_iron_icon_32.png")
	_register(mat_item(MATERIAL_COPPER, FORM_DUST), "item.copper_dust", Color(0.80, 0.50, 0.15),
			64, null, "materials/copper_dust_icon_32.png")
	_register(mat_item(MATERIAL_IRON, FORM_DUST), "item.iron_dust", Color(0.65, 0.60, 0.55),
			64, null, "materials/iron_dust_icon_32.png")

	# --- Planetary rock dusts ---
	_register(mat_item(MATERIAL_GRANITE, FORM_DUST), "item.granite_dust", Color(0.63, 0.60, 0.56),
			64, null, "")
	_register(mat_item(MATERIAL_GRANITE, FORM_TINY_DUST), "item.granite_tiny_dust", Color(0.53, 0.50, 0.46),
			64, null, "")
	_register(mat_item(MATERIAL_BASALT, FORM_DUST), "item.basalt_dust", Color(0.31, 0.31, 0.31),
			64, null, "")
	_register(mat_item(MATERIAL_BASALT, FORM_TINY_DUST), "item.basalt_tiny_dust", Color(0.26, 0.26, 0.26),
			64, null, "")
	_register(mat_item(MATERIAL_MARBLE, FORM_DUST), "item.marble_dust", Color(0.91, 0.88, 0.85),
			64, null, "")
	_register(mat_item(MATERIAL_MARBLE, FORM_TINY_DUST), "item.marble_tiny_dust", Color(0.81, 0.78, 0.75),
			64, null, "")
	_register(mat_item(MATERIAL_SANDSTONE, FORM_DUST), "item.sandstone_dust", Color(0.78, 0.66, 0.44),
			64, null, "")
	_register(mat_item(MATERIAL_SANDSTONE, FORM_TINY_DUST), "item.sandstone_tiny_dust", Color(0.68, 0.56, 0.34),
			64, null, "")
	_register(mat_item(MATERIAL_SHALE, FORM_DUST), "item.shale_dust", Color(0.35, 0.38, 0.31),
			64, null, "")
	_register(mat_item(MATERIAL_SHALE, FORM_TINY_DUST), "item.shale_tiny_dust", Color(0.28, 0.30, 0.24),
			64, null, "")
	_register(mat_item(MATERIAL_KOMATIITE, FORM_DUST), "item.komatiite_dust", Color(0.23, 0.31, 0.19),
			64, null, "")
	_register(mat_item(MATERIAL_KOMATIITE, FORM_TINY_DUST), "item.komatiite_tiny_dust", Color(0.18, 0.24, 0.14),
			64, null, "")
	_register(mat_item(MATERIAL_REGOLITH, FORM_DUST), "item.regolith_dust", Color(0.63, 0.38, 0.25),
			64, null, "")
	_register(mat_item(MATERIAL_REGOLITH, FORM_TINY_DUST), "item.regolith_tiny_dust", Color(0.53, 0.30, 0.18),
			64, null, "")
	_register(mat_item(MATERIAL_ANORTHOSTIE, FORM_DUST), "item.anorthosite_dust", Color(0.75, 0.75, 0.78),
			64, null, "")
	_register(mat_item(MATERIAL_ANORTHOSTIE, FORM_TINY_DUST), "item.anorthosite_tiny_dust", Color(0.65, 0.65, 0.68),
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

	_register(ITEM_WOODEN_PICKAXE, "wooden_pickaxe", brown, 1,
			_make_tool_def(PICK, W, 0, 1.5, 60, 2.0), "tools/wooden_pickaxe_icon_32.png")
	_register(ITEM_STONE_PICKAXE, "stone_pickaxe", gray, 1,
			_make_tool_def(PICK, S, 1, 2.0, 130, 3.0), "tools/stone_pickaxe_icon_32.png")
	_register(ITEM_IRON_PICKAXE, "iron_pickaxe", silver, 1,
			_make_tool_def(PICK, I, 2, 3.0, 250, 4.0), "tools/iron_pickaxe_icon_32.png")
	_register(ITEM_WOODEN_AXE, "wooden_axe", brown, 1,
			_make_tool_def(AXE, W, 0, 1.5, 60, 3.0), "tools/wooden_axe_icon_32.png")
	_register(ITEM_STONE_AXE, "stone_axe", gray, 1,
			_make_tool_def(AXE, S, 1, 2.0, 130, 4.0), "tools/stone_axe_icon_32.png")
	_register(ITEM_IRON_AXE, "iron_axe", silver, 1,
			_make_tool_def(AXE, I, 2, 3.0, 250, 5.0), "tools/iron_axe_icon_32.png")
	_register(ITEM_WOODEN_SHOVEL, "wooden_shovel", brown, 1,
			_make_tool_def(SHOVEL, W, 0, 1.5, 60, 1.5), "tools/wooden_shovel_icon_32.png")
	_register(ITEM_STONE_SHOVEL, "stone_shovel", gray, 1,
			_make_tool_def(SHOVEL, S, 1, 2.0, 130, 2.5), "tools/stone_shovel_icon_32.png")
	_register(ITEM_IRON_SHOVEL, "iron_shovel", silver, 1,
			_make_tool_def(SHOVEL, I, 2, 3.0, 250, 3.5), "tools/iron_shovel_icon_32.png")
	_register(ITEM_WOODEN_SWORD, "wooden_sword", brown, 1,
			_make_tool_def(SWORD, W, 0, 1.0, 60, 4.0), "tools/wooden_sword_icon_32.png")
	_register(ITEM_STONE_SWORD, "stone_sword", gray, 1,
			_make_tool_def(SWORD, S, 1, 1.0, 130, 5.0), "tools/stone_sword_icon_32.png")
	_register(ITEM_IRON_SWORD, "iron_sword", silver, 1,
			_make_tool_def(SWORD, I, 2, 1.0, 250, 6.0), "tools/iron_sword_icon_32.png")


func _register_component_items() -> void:
	_register(ITEM_GT_HAMMER, "gt_hammer", Color(0.62, 0.62, 0.65), 1, null, "tools/gt_hammer_icon_32.png")
	_register(ITEM_GT_WRENCH, "gt_wrench", Color(0.58, 0.60, 0.62), 1, null, "tools/gt_wrench_icon_32.png")
	_register(ITEM_GT_FILE, "gt_file", Color(0.52, 0.52, 0.54), 1, null, "tools/gt_file_icon_32.png")
	_register(ITEM_GT_SCREWDRIVER, "gt_screwdriver", Color(0.62, 0.42, 0.20),
			1, null, "tools/gt_screwdriver_icon_32.png")
	_register(ITEM_GT_SAW, "gt_saw", Color(0.60, 0.58, 0.54), 1, null, "tools/gt_saw_icon_32.png")
	_register(ITEM_GT_WIRE_CUTTER, "gt_wire_cutter", Color(0.52, 0.54, 0.56),
			1, null, "tools/gt_wire_cutter_icon_32.png")
	_register(ITEM_GT_CROWBAR, "gt_crowbar", Color(0.45, 0.20, 0.15), 1, null, "tools/gt_crowbar_icon_32.png")
	_register(ITEM_GT_SOFT_MALLET, "gt_soft_mallet", Color(0.18, 0.18, 0.18),
			1, null, "tools/gt_soft_mallet_icon_32.png")
	_register(ITEM_GT_HARD_HAMMER, "gt_hard_hammer", Color(0.56, 0.56, 0.60),
			1, null, "tools/gt_hard_hammer_icon_32.png")

	_register(ITEM_MACHINE_HULL_BASIC, "machine_hull_basic", Color(0.34, 0.36, 0.38),
			64, null, "components/basic_machine_hull_icon_32.png")
	_register(ITEM_MACHINE_HULL_ADVANCED, "machine_hull_advanced", Color(0.30, 0.42, 0.46),
			64, null, "components/advanced_machine_hull_icon_32.png")
	_register(ITEM_ELECTRIC_MOTOR_LV, "electric_motor_lv", Color(0.55, 0.38, 0.20),
			64, null, "components/lv_electric_motor_icon_32.png")
	_register(ITEM_ELECTRIC_PISTON_LV, "electric_piston_lv", Color(0.48, 0.48, 0.50),
			64, null, "components/lv_electric_piston_icon_32.png")
	_register(ITEM_ROBOT_ARM_LV, "robot_arm_lv", Color(0.48, 0.42, 0.34),
			64, null, "components/lv_robot_arm_icon_32.png")
	_register(ITEM_CONVEYOR_MODULE_LV, "conveyor_module_lv", Color(0.22, 0.24, 0.24),
			64, null, "components/lv_conveyor_module_icon_32.png")
	_register(ITEM_PUMP_LV, "pump_lv", Color(0.38, 0.44, 0.46), 64, null, "components/lv_pump_icon_32.png")
	_register(ITEM_EMPTY_FLUID_CELL, "empty_fluid_cell", Color(0.62, 0.64, 0.68),
			64, null, "components/empty_fluid_cell_icon_32.png")

	_register(ITEM_VACUUM_TUBE, "vacuum_tube", Color(0.72, 0.45, 0.18),
			64, null, "circuits/vacuum_tube_icon_32.png")
	_register(ITEM_CIRCUIT_PRIMITIVE, "circuit_primitive", Color(0.42, 0.30, 0.18),
			64, null, "circuits/primitive_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_BASIC, "circuit_basic", Color(0.20, 0.46, 0.30),
			64, null, "circuits/basic_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_GOOD, "circuit_good", Color(0.16, 0.50, 0.48),
			64, null, "circuits/good_circuit_icon_32.png")
	_register(ITEM_CIRCUIT_ADVANCED, "circuit_advanced", Color(0.14, 0.34, 0.52),
			64, null, "circuits/advanced_circuit_icon_32.png")

	_register(ITEM_COAL_BLOCK, "coal_block", Color(0.10, 0.10, 0.11),
			64, null, "components/coal_block_icon_32.png")
	_register(ITEM_COKE_BRICK, "coke_brick", Color(0.18, 0.17, 0.16),
			64, null, "components/coke_brick_icon_32.png")
	_register(ITEM_FIREBRICK, "firebrick", Color(0.58, 0.24, 0.12),
			64, null, "components/firebrick_icon_32.png")
	_register(ITEM_STONE_PLATE, "stone_plate", Color(0.46, 0.46, 0.47),
			64, null, "components/stone_plate_icon_32.png")
	_register(ITEM_WOOD_PLATE, "wood_plate", Color(0.58, 0.38, 0.18),
			64, null, "components/wood_plate_icon_32.png")
	_register(ITEM_BLANK_PATTERN, "blank_pattern", Color(0.24, 0.34, 0.38),
			64, null, "components/blank_pattern_icon_32.png")

func _register_survival_items() -> void:
	_register(ITEM_WORKBENCH, "workbench", Color(0.50, 0.35, 0.20),
			64, null, "placeables/workbench_icon_32.png")
	_register(ITEM_FURNACE, "stone_furnace", Color(0.45, 0.35, 0.25),
			64, null, "placeables/stone_furnace_icon_32.png")
	_register(ITEM_LADDER, "ladder", Color(0.55, 0.30, 0.15),
			64, null, "placeables/ladder_icon_32.png")
	_register(ITEM_STATION_BLUEPRINT, "station_blueprint", Color(0.20, 0.50, 0.80),
			1, null, "")


func _register_tree_species_items() -> void:
	# Oak: brown wood, green leaves.
	_register(ITEM_OAK_LOG, "log.oak", Color(0.45, 0.27, 0.12), 64, null, "trees/log_oak_icon_64.png")
	_register(ITEM_OAK_PLANK, "plank.oak", Color(0.60, 0.40, 0.18), 64, null, "trees/plank_oak_icon_64.png")
	_register(ITEM_OAK_SAPLING, "sapling.oak", Color(0.30, 0.55, 0.15), 64, null, "trees/sapling_oak_icon_64.png")

	# Birch: white wood, light green leaves.
	_register(ITEM_BIRCH_LOG, "log.birch", Color(0.80, 0.78, 0.70), 64, null, "trees/log_birch_icon_64.png")
	_register(ITEM_BIRCH_PLANK, "plank.birch", Color(0.85, 0.82, 0.72), 64, null, "trees/plank_birch_icon_64.png")
	_register(ITEM_BIRCH_SAPLING, "sapling.birch", Color(0.35, 0.60, 0.20), 64, null, "trees/sapling_birch_icon_64.png")

	# Spruce: dark brown wood, dark green leaves.
	_register(ITEM_SPRUCE_LOG, "log.spruce", Color(0.35, 0.22, 0.10), 64, null, "trees/log_spruce_icon_64.png")
	_register(ITEM_SPRUCE_PLANK, "plank.spruce", Color(0.45, 0.30, 0.15), 64, null, "trees/plank_spruce_icon_64.png")
	_register(ITEM_SPRUCE_SAPLING, "sapling.spruce", Color(0.12, 0.35, 0.15), 64, null, "trees/sapling_spruce_icon_64.png")

	# Acacia: warm brown wood, olive green leaves.
	_register(ITEM_ACACIA_LOG, "log.acacia", Color(0.55, 0.35, 0.15), 64, null, "trees/log_acacia_icon_64.png")
	_register(ITEM_ACACIA_PLANK, "plank.acacia", Color(0.65, 0.42, 0.18), 64, null, "trees/plank_acacia_icon_64.png")
	_register(ITEM_ACACIA_SAPLING, "sapling.acacia", Color(0.35, 0.55, 0.15), 64, null, "trees/sapling_acacia_icon_64.png")

	# Maple: reddish brown wood, bright green leaves.
	_register(ITEM_MAPLE_LOG, "log.maple", Color(0.42, 0.25, 0.10), 64, null, "trees/log_maple_icon_64.png")
	_register(ITEM_MAPLE_PLANK, "plank.maple", Color(0.55, 0.32, 0.14), 64, null, "trees/plank_maple_icon_64.png")
	_register(ITEM_MAPLE_SAPLING, "sapling.maple", Color(0.28, 0.52, 0.18), 64, null, "trees/sapling_maple_icon_64.png")

	# Sequoia: deep red-brown wood, dark green leaves.
	_register(ITEM_SEQUOIA_LOG, "log.sequoia", Color(0.40, 0.23, 0.10), 64, null, "trees/log_sequoia_icon_64.png")
	_register(ITEM_SEQUOIA_PLANK, "plank.sequoia", Color(0.50, 0.28, 0.12), 64, null, "trees/plank_sequoia_icon_64.png")
	_register(ITEM_SEQUOIA_SAPLING, "sapling.sequoia", Color(0.15, 0.38, 0.12), 64, null, "trees/sapling_sequoia_icon_64.png")

	# Cherry: pinkish wood, pink leaves, fruit-bearing.
	_register(ITEM_CHERRY_LOG, "log.cherry", Color(0.50, 0.28, 0.22), 64, null, "trees/log_cherry_icon_64.png")
	_register(ITEM_CHERRY_PLANK, "plank.cherry", Color(0.60, 0.35, 0.28), 64, null, "trees/plank_cherry_icon_64.png")
	_register(ITEM_CHERRY_SAPLING, "sapling.cherry", Color(0.65, 0.30, 0.45), 64, null, "trees/sapling_cherry_icon_64.png")
	_register(ITEM_CHERRY_FRUIT, "fruit.cherry", Color(0.85, 0.15, 0.25), 64, null, "trees/fruit_cherry_icon_64.png")

	# Olive: grey-brown wood, silvery green leaves, fruit-bearing.
	_register(ITEM_OLIVE_LOG, "log.olive", Color(0.52, 0.40, 0.22), 64, null, "trees/log_olive_icon_64.png")
	_register(ITEM_OLIVE_PLANK, "plank.olive", Color(0.60, 0.48, 0.28), 64, null, "trees/plank_olive_icon_64.png")
	_register(ITEM_OLIVE_SAPLING, "sapling.olive", Color(0.28, 0.42, 0.18), 64, null, "trees/sapling_olive_icon_64.png")
	_register(ITEM_OLIVE_FRUIT, "fruit.olive", Color(0.55, 0.58, 0.20), 64, null, "trees/fruit_olive_icon_64.png")


# --- Source law creature drop items ---

func _register_source_law_drops() -> void:
	# Helper to register a drop item and map its item_key.
	var _register_drop := func(item_id: int, item_key: String, title_key: String, color: Color, max_stack: int = 64) -> void:
		_register(item_id, title_key, color, max_stack)
		_key_to_id[item_key] = item_id
		_id_to_key[item_id] = item_key

	# Glow Deer drops (Radiance path).
	_register_drop.call(ITEM_GLOW_DEER_ANTLER, "snt:glow_deer_antler", "snt:glow_deer_antler", Color(0.90, 0.85, 0.60))
	_register_drop.call(ITEM_PURIFYING_POLLEN, "snt:purifying_pollen", "snt:purifying_pollen", Color(0.70, 0.95, 0.70))

	# Rock Lizard drops (Sand Armor path).
	_register_drop.call(ITEM_ROCK_LIZARD_SCALE, "snt:rock_lizard_scale", "snt:rock_lizard_scale", Color(0.60, 0.55, 0.45))
	_register_drop.call(ITEM_CRYSTALLIZED_BONE_POWDER, "snt:crystallized_bone_powder", "snt:crystallized_bone_powder", Color(0.85, 0.82, 0.78))

	# Thunderbird drops (Storm path).
	_register_drop.call(ITEM_THUNDERBIRD_FEATHER, "snt:thunderbird_feather", "snt:thunderbird_feather", Color(0.50, 0.60, 0.90))
	_register_drop.call(ITEM_MAGNETIC_CRYSTAL_SHARD, "snt:magnetic_crystal_shard", "snt:magnetic_crystal_shard", Color(0.40, 0.50, 0.80))

	# Sea Serpent drops (Tidal path).
	_register_drop.call(ITEM_SEA_SERPENT_SCALE, "snt:sea_serpent_scale", "snt:sea_serpent_scale", Color(0.30, 0.60, 0.70))
	_register_drop.call(ITEM_TIDAL_GLAND, "snt:tidal_gland", "snt:tidal_gland", Color(0.25, 0.55, 0.75))

	# Blaze Beast drops (Furnace path).
	_register_drop.call(ITEM_BLAZING_CORE, "snt:blazing_core", "snt:blazing_core", Color(0.95, 0.50, 0.15))
	_register_drop.call(ITEM_MOLTEN_BLOOD_SAMPLE, "snt:molten_blood_sample", "snt:molten_blood_sample", Color(0.85, 0.30, 0.10))

	# Aether Wraith drops (Ruin guardian).
	_register_drop.call(ITEM_AETHER_FRAGMENT, "snt:aether_fragment", "snt:aether_fragment", Color(0.70, 0.60, 0.90))
	_register_drop.call(ITEM_BLUEPRINT_SHARD, "snt:blueprint_shard", "snt:blueprint_shard", Color(0.50, 0.70, 0.80))

	# Aberrant Ascended drops (High-risk enemy).
	_register_drop.call(ITEM_ABERRANT_ORGAN, "snt:aberrant_organ", "snt:aberrant_organ", Color(0.60, 0.20, 0.50))
	_register_drop.call(ITEM_POLLUTED_SOURCE_ESSENCE, "snt:polluted_source_essence", "snt:polluted_source_essence", Color(0.40, 0.15, 0.35))


# Register reverse mappings for non-material items (tools, components, etc.)
# Keys mirror the C++ kNonMaterialItemKeys array in tool_items.hpp.
func _register_non_material_keys() -> void:
	var entries := {
		ITEM_GT_HAMMER: "gt_hammer",
		ITEM_GT_WRENCH: "gt_wrench",
		ITEM_GT_FILE: "gt_file",
		ITEM_GT_SCREWDRIVER: "gt_screwdriver",
		ITEM_GT_SAW: "gt_saw",
		ITEM_GT_WIRE_CUTTER: "gt_wire_cutter",
		ITEM_GT_CROWBAR: "gt_crowbar",
		ITEM_GT_SOFT_MALLET: "gt_soft_mallet",
		ITEM_GT_HARD_HAMMER: "gt_hard_hammer",
		ITEM_MACHINE_HULL_BASIC: "machine_hull_basic",
		ITEM_MACHINE_HULL_ADVANCED: "machine_hull_advanced",
		ITEM_ELECTRIC_MOTOR_LV: "electric_motor_lv",
		ITEM_ELECTRIC_PISTON_LV: "electric_piston_lv",
		ITEM_ROBOT_ARM_LV: "robot_arm_lv",
		ITEM_CONVEYOR_MODULE_LV: "conveyor_module_lv",
		ITEM_PUMP_LV: "pump_lv",
		ITEM_EMPTY_FLUID_CELL: "empty_fluid_cell",
		ITEM_VACUUM_TUBE: "vacuum_tube",
		ITEM_CIRCUIT_PRIMITIVE: "circuit_primitive",
		ITEM_CIRCUIT_BASIC: "circuit_basic",
		ITEM_CIRCUIT_GOOD: "circuit_good",
		ITEM_CIRCUIT_ADVANCED: "circuit_advanced",
		ITEM_COAL_BLOCK: "coal_block",
		ITEM_COKE_BRICK: "coke_brick",
		ITEM_FIREBRICK: "firebrick",
		ITEM_STONE_PLATE: "stone_plate",
		ITEM_WOOD_PLATE: "wood_plate",
		ITEM_BLANK_PATTERN: "blank_pattern",
		ITEM_WOODEN_PICKAXE: "wooden_pickaxe",
		ITEM_STONE_PICKAXE: "stone_pickaxe",
		ITEM_IRON_PICKAXE: "iron_pickaxe",
		ITEM_WOODEN_AXE: "wooden_axe",
		ITEM_STONE_AXE: "stone_axe",
		ITEM_IRON_AXE: "iron_axe",
		ITEM_WOODEN_SHOVEL: "wooden_shovel",
		ITEM_STONE_SHOVEL: "stone_shovel",
		ITEM_IRON_SHOVEL: "iron_shovel",
		ITEM_WOODEN_SWORD: "wooden_sword",
		ITEM_STONE_SWORD: "stone_sword",
		ITEM_IRON_SWORD: "iron_sword",
		ITEM_WORKBENCH: "workbench",
		ITEM_FURNACE: "stone_furnace",
		ITEM_LADDER: "ladder",
		ITEM_OAK_LOG: "log.oak",
		ITEM_OAK_PLANK: "plank.oak",
		ITEM_OAK_SAPLING: "sapling.oak",
		ITEM_BIRCH_LOG: "log.birch",
		ITEM_BIRCH_PLANK: "plank.birch",
		ITEM_BIRCH_SAPLING: "sapling.birch",
		ITEM_SPRUCE_LOG: "log.spruce",
		ITEM_SPRUCE_PLANK: "plank.spruce",
		ITEM_SPRUCE_SAPLING: "sapling.spruce",
		ITEM_ACACIA_LOG: "log.acacia",
		ITEM_ACACIA_PLANK: "plank.acacia",
		ITEM_ACACIA_SAPLING: "sapling.acacia",
		ITEM_MAPLE_LOG: "log.maple",
		ITEM_MAPLE_PLANK: "plank.maple",
		ITEM_MAPLE_SAPLING: "sapling.maple",
		ITEM_SEQUOIA_LOG: "log.sequoia",
		ITEM_SEQUOIA_PLANK: "plank.sequoia",
		ITEM_SEQUOIA_SAPLING: "sapling.sequoia",
		ITEM_CHERRY_LOG: "log.cherry",
		ITEM_CHERRY_PLANK: "plank.cherry",
		ITEM_CHERRY_SAPLING: "sapling.cherry",
		ITEM_CHERRY_FRUIT: "fruit.cherry",
		ITEM_OLIVE_LOG: "log.olive",
		ITEM_OLIVE_PLANK: "plank.olive",
		ITEM_OLIVE_SAPLING: "sapling.olive",
		ITEM_OLIVE_FRUIT: "fruit.olive",
		ITEM_STATION_BLUEPRINT: "station_blueprint",
	}
	for id: int in entries:
		var key: String = entries[id]
		_key_to_id[key] = id
		_id_to_key[id] = key


# Register material item key mappings for items used in quest conditions.
# Material item keys follow the convention: "{form_name}.{material_name}".
# This mirrors the C++ MaterialItem key resolution in material_item.hpp.
func _register_material_item_keys() -> void:
	# Helper: compute material item ID.
	# This mirrors the C++ mat_item() macro: base + mat_id * form_count + form_id.
	var compute_mat_item := func(mat_id: int, form_id: int) -> int:
		return K_MAT_ITEM_BASE + mat_id * K_FORM_COUNT + form_id

	# Register specific material items used in quest conditions.
	var quest_material_items := {
		compute_mat_item.call(MATERIAL_STONE, FORM_DUST): "dust.stone",
		compute_mat_item.call(MATERIAL_COAL, FORM_GEM): "gem.coal",
		compute_mat_item.call(MATERIAL_COPPER, FORM_DUST): "dust.copper",
		compute_mat_item.call(MATERIAL_IRON, FORM_DUST): "dust.iron",
		compute_mat_item.call(MATERIAL_IRON, FORM_INGOT): "ingot.iron",
		compute_mat_item.call(MATERIAL_IRON, FORM_PLATE): "plate.iron",
		compute_mat_item.call(MATERIAL_IRON, FORM_ROD): "rod.iron",
		compute_mat_item.call(MATERIAL_WOOD, FORM_PLATE): "plate.wood",
		compute_mat_item.call(MATERIAL_WOOD, FORM_ROD): "rod.wood",
	}

	# Add bronze (alloy, not in the basic material list).
	# Bronze = Copper + Tin alloy. Item IDs may use a different base.
	# For now, register the key-only entries for item key lookup.
	var extra_keys := {
		"dust.tin": "dust.tin",
		"dust.coal": "dust.coal",
		"ingot.tin": "ingot.tin",
		"ingot.bronze": "ingot.bronze",
		"ingot.steel": "ingot.steel",
		"plate.bronze": "plate.bronze",
		"circuit_basic": "circuit_basic",
	}

	for id: int in quest_material_items:
		var key: String = quest_material_items[id]
		# Don't overwrite existing _id_to_key entries, only add new ones
		if not _id_to_key.has(id):
			_id_to_key[id] = key
		if not _key_to_id.has(key):
			_key_to_id[key] = id

	# Register extra key-only entries (without item_id resolution).
	# These are used by the quest system for string matching even if
	# the item_id lookup doesn't work.
	for key: String in extra_keys:
		if not _key_to_id.has(key):
			# Mark with a sentinel id so key lookup returns something non-negative.
			_key_to_id[key] = -2
