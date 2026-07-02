class_name GameCommandServer
extends GDGameCommandServer

# Single-player local player id. Multi-player assigns higher ids to
# remote clients; the local host is always 1.
const LOCAL_PLAYER_HANDLE := 1

const COMMAND_MINE_BLOCK: StringName = &"mine_block"
const COMMAND_PLACE_BLOCK: StringName = &"place_block"
const COMMAND_ADD_INVENTORY_ITEM: StringName = &"add_inventory_item"
const COMMAND_REMOVE_INVENTORY_ITEM: StringName = &"remove_inventory_item"
const COMMAND_CRAFT_RECIPE: StringName = &"craft_recipe"
const COMMAND_PLACE_OBJECT: StringName = &"place_object"
const COMMAND_REMOVE_OBJECT: StringName = &"remove_object"
const COMMAND_FURNACE_TAKE_OUTPUT: StringName = &"furnace_take_output"
const COMMAND_FURNACE_INSERT_INPUT: StringName = &"furnace_insert_input"
const COMMAND_FURNACE_INSERT_FUEL: StringName = &"furnace_insert_fuel"
const COMMAND_TILL_FARMLAND: StringName = &"till_farmland"
const COMMAND_PLANT_CROP: StringName = &"plant_crop"
const COMMAND_HARVEST_CROP: StringName = &"harvest_crop"
const COMMAND_FERTILIZE: StringName = &"fertilize"

# TFC expansion commands
const COMMAND_FORAGE_WILD: StringName = &"forage_wild"
const COMMAND_KNAPPING_PLACE: StringName = &"knapping_place"
const COMMAND_KNAPPING_PICKUP: StringName = &"knapping_pickup"
const COMMAND_PLACE_CHARCOAL_PIT: StringName = &"place_charcoal_pit"
const COMMAND_ADD_LOG: StringName = &"add_log"
const COMMAND_COVER_PIT: StringName = &"cover_pit"
const COMMAND_LIGHT_PIT: StringName = &"light_pit"
const COMMAND_COLLECT_CHARCOAL: StringName = &"collect_charcoal"
const COMMAND_PLACE_PIT_KILN: StringName = &"place_pit_kiln"
const COMMAND_ADD_POTTERY: StringName = &"add_pottery"
const COMMAND_COLLECT_POTTERY: StringName = &"collect_pottery"
const COMMAND_PLACE_BLOOMERY: StringName = &"place_bloomery"
const COMMAND_ADD_BLOOMERY_INPUT: StringName = &"add_bloomery_input"
const COMMAND_USE_BELLOWS: StringName = &"use_bellows"
const COMMAND_BREAK_BLOOMERY: StringName = &"break_bloomery"
const COMMAND_PLACE_ANVIL: StringName = &"place_anvil"
const COMMAND_ANVIL_WELD: StringName = &"anvil_weld"

const OBJECT_WORKBENCH: StringName = &"workbench"
const OBJECT_FURNACE: StringName = &"furnace"
const OBJECT_LADDER: StringName = &"ladder"
const OBJECT_FENCE: StringName = &"fence"

const SECONDARY_NONE := -1

@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var charcoal_pit_manager_path: NodePath = ^"../CharcoalPitManager"
@export var pit_kiln_manager_path: NodePath = ^"../PitKilnManager"
@export var bloomery_manager_path: NodePath = ^"../BloomeryManager"
@export var anvil_manager_path: NodePath = ^"../AnvilManager"

var _chunk_bridge: ChunkRendererBridge


func _ready() -> void:
	_configure_server()
	call_deferred(&"_configure_server")


func _configure_server() -> void:
	_chunk_bridge = get_node_or_null(chunk_bridge_path)
	set_furnace_manager(get_node_or_null(furnace_manager_path))
	if _chunk_bridge != null:
		set_world_data(_chunk_bridge.get_world_data())
		_inject_bloomery_material_id()


# Inject the bloomery material id (resolved from runtime_ids) into the
# BloomeryManager so has_valid_structure can check block materials.
func _inject_bloomery_material_id() -> void:
	var bloomery_mgr: Node = get_node_or_null(bloomery_manager_path)
	if bloomery_mgr == null or _chunk_bridge == null:
		return
	if _chunk_bridge.worldgen_config == null:
		return
	var runtime_ids: Dictionary = _chunk_bridge.worldgen_config.get_runtime_material_ids()
	bloomery_mgr.set_bloomery_material_id(int(runtime_ids.get("bloomery", 0)))


# TFC commands are now handled directly in C++ GDGameCommandServer.
# GDScript override is no longer needed - all dispatch goes through
# the C++ submit_command() which has been extended with new handlers.
