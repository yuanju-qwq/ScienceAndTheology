class_name WorldObjectRenderer
extends Node

const WORKBENCH_TEXTURE := preload("res://resource/world/objects/workbench_world_64.png")
const FURNACE_TEXTURE := preload("res://resource/world/objects/stone_furnace_world_64.png")
const LADDER_TEXTURE := preload("res://resource/world/objects/ladder_entrance_world_64.png")

@export var workbench_manager_path: NodePath = ^"../WorkbenchManager"
@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var ladder_manager_path: NodePath = ^"../LadderManager"
@export var surface_layer_path: NodePath = ^"../Layers/SurfaceLayer"
@export var underground_layer_path: NodePath = ^"../Layers/UndergroundLayer"

@onready var _workbench_manager = get_node_or_null(workbench_manager_path)
@onready var _furnace_manager = get_node_or_null(furnace_manager_path)
@onready var _ladder_manager = get_node_or_null(ladder_manager_path)
@onready var _surface_layer := get_node_or_null(surface_layer_path) as TileMapLayer
@onready var _underground_layer := get_node_or_null(underground_layer_path) as TileMapLayer

var _sprites: Dictionary = {}


func _ready() -> void:
	_connect_manager_signals()
	_sync_existing_objects()


func _connect_manager_signals() -> void:
	if _workbench_manager != null:
		_workbench_manager.workbench_placed.connect(_on_workbench_placed)
		_workbench_manager.workbench_removed.connect(_on_workbench_removed)
	if _furnace_manager != null:
		_furnace_manager.furnace_placed.connect(_on_furnace_placed)
		_furnace_manager.furnace_removed.connect(_on_furnace_removed)
	if _ladder_manager != null:
		_ladder_manager.ladder_placed.connect(_on_ladder_placed)
		_ladder_manager.ladder_removed.connect(_on_ladder_removed)


func _sync_existing_objects() -> void:
	if _workbench_manager != null:
		for entry in _workbench_manager.get_all_workbenches():
			_on_workbench_placed(entry.get("layer", &""), entry.get("cell", Vector2i.ZERO))
	if _furnace_manager != null:
		for entry in _furnace_manager.get_all_furnaces():
			_on_furnace_placed(entry.get("layer", &""), entry.get("cell", Vector2i.ZERO))
	if _ladder_manager != null and _ladder_manager.has_method(&"get_all_ladders"):
		for entry in _ladder_manager.get_all_ladders():
			_on_ladder_placed(entry.get("layer", &""), entry.get("cell", Vector2i.ZERO))


func _on_workbench_placed(layer: StringName, cell: Vector2i) -> void:
	_create_sprite(&"workbench", layer, cell, WORKBENCH_TEXTURE)


func _on_workbench_removed(layer: StringName, cell: Vector2i) -> void:
	_remove_sprite(&"workbench", layer, cell)


func _on_furnace_placed(layer: StringName, cell: Vector2i) -> void:
	_create_sprite(&"furnace", layer, cell, FURNACE_TEXTURE)


func _on_furnace_removed(layer: StringName, cell: Vector2i) -> void:
	_remove_sprite(&"furnace", layer, cell)


func _on_ladder_placed(layer: StringName, cell: Vector2i) -> void:
	_create_sprite(&"ladder", layer, cell, LADDER_TEXTURE)


func _on_ladder_removed(layer: StringName, cell: Vector2i) -> void:
	_remove_sprite(&"ladder", layer, cell)


func _create_sprite(object_type: StringName, layer: StringName, cell: Vector2i,
		texture: Texture2D) -> void:
	var key := _make_key(object_type, layer, cell)
	if _sprites.has(key):
		return

	var tile_layer := _get_tile_layer(layer)
	if tile_layer == null:
		push_warning("WorldObjectRenderer: unknown layer '%s'" % layer)
		return

	var sprite := Sprite2D.new()
	sprite.name = "%s_%d_%d" % [object_type, cell.x, cell.y]
	sprite.texture = texture
	sprite.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	sprite.position = tile_layer.map_to_local(cell)
	sprite.z_index = 5
	tile_layer.add_child(sprite)
	_sprites[key] = sprite


func _remove_sprite(object_type: StringName, layer: StringName, cell: Vector2i) -> void:
	var key := _make_key(object_type, layer, cell)
	var sprite := _sprites.get(key) as Sprite2D
	if sprite == null:
		return
	_sprites.erase(key)
	sprite.queue_free()


func _get_tile_layer(layer: StringName) -> TileMapLayer:
	if layer == WorldLayers.SURFACE:
		return _surface_layer
	if layer == WorldLayers.UNDERGROUND:
		return _underground_layer
	return null


func _make_key(object_type: StringName, layer: StringName, cell: Vector2i) -> String:
	return "%s,%s,%d,%d" % [object_type, layer, cell.x, cell.y]
