class_name GameCommandServer
extends GDGameCommandServer

const COMMAND_MINE_BLOCK: StringName = &"mine_block"
const COMMAND_ADD_INVENTORY_ITEM: StringName = &"add_inventory_item"
const COMMAND_REMOVE_INVENTORY_ITEM: StringName = &"remove_inventory_item"
const COMMAND_CRAFT_RECIPE: StringName = &"craft_recipe"
const COMMAND_PLACE_OBJECT: StringName = &"place_object"
const COMMAND_FURNACE_TAKE_OUTPUT: StringName = &"furnace_take_output"
const COMMAND_FURNACE_INSERT_INPUT: StringName = &"furnace_insert_input"
const COMMAND_FURNACE_INSERT_FUEL: StringName = &"furnace_insert_fuel"
const COMMAND_TILL_FARMLAND: StringName = &"till_farmland"
const COMMAND_PLANT_CROP: StringName = &"plant_crop"
const COMMAND_HARVEST_CROP: StringName = &"harvest_crop"
const COMMAND_FERTILIZE: StringName = &"fertilize"

const OBJECT_WORKBENCH: StringName = &"workbench"
const OBJECT_FURNACE: StringName = &"furnace"
const OBJECT_LADDER: StringName = &"ladder"
const OBJECT_FENCE: StringName = &"fence"

const SECONDARY_NONE := -1

@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"

var _chunk_bridge: ChunkRendererBridge


func _ready() -> void:
	_configure_server()
	call_deferred(&"_configure_server")


func _configure_server() -> void:
	_chunk_bridge = get_node_or_null(chunk_bridge_path)
	set_furnace_manager(get_node_or_null(furnace_manager_path))
	if _chunk_bridge != null:
		set_world_data(_chunk_bridge.get_world_data())
