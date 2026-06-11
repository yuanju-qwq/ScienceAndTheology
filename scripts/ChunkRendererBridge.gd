class_name ChunkRendererBridge
extends Node

## Main integration bridge between C++ chunk data and the Godot rendering system.
##
## Three-tier chunk management for factory games:
##   - loaded_radius:  chunks kept in memory for machine/network simulation
##   - view_radius:    chunks with visual GDChunkView (TileMapLayer)
##   - force_loaded:   specific chunks always kept loaded (factory installations)
##
## Usage:
##   Add as a child of the WorldMap root. Configure properties in the inspector.
##
##   _process(delta):
##       chunk_bridge.process_frame(get_player_chunk_layer(), get_player_chunk_pos())
##
##   # Force-load a factory chunk (keeps machines running when player is far):
##   chunk_bridge.force_load_factory_chunk("surface", 5, 3)

signal chunk_bridge_ready

@export var world_data: GDWorldData = null
@export var chunk_manager: GDChunkManager = null

@export var surface_container_path: NodePath = ^"Layers/SurfaceLayer"
@export var underground_container_path: NodePath = ^"Layers/UndergroundLayer"

@export var seed: int = 0:
	set(value):
		seed = value
		if world_data:
			world_data.seed = value

@export var loaded_radius: int = 10:
	set(value):
		loaded_radius = value
		if chunk_manager:
			chunk_manager.loaded_radius = value

@export var view_radius: int = 6:
	set(value):
		view_radius = value
		if chunk_manager:
			chunk_manager.view_radius = value

@export var tile_size: int = 32:
	set(value):
		tile_size = value
		if chunk_manager:
			chunk_manager.tile_size = value

@export var auto_generate_start_chunks := true
@export var start_chunk_radius := 2
@export var player_node_path: NodePath = ^"../Player"
@export var auto_update := true

var is_initialized := false


func _ready() -> void:
	_initialize_if_needed()


func _process(delta: float) -> void:
	if not auto_update or not is_initialized:
		return

	if world_data:
		world_data.process_async_results()

	var map_layer: Node = _get_map_layer_controller()
	if map_layer == null:
		return
	var layer: StringName = map_layer.current_layer

	var player := get_node_or_null(player_node_path) as Node2D
	if player == null:
		return

	var chunk_pixel_size := 32.0 * tile_size
	var cx := floori(player.global_position.x / chunk_pixel_size)
	var cy := floori(player.global_position.y / chunk_pixel_size)

	if chunk_manager:
		chunk_manager.set_player_chunk(layer, cx, cy)
		chunk_manager.refresh_chunks()


func initialize() -> void:
	if is_initialized:
		return

	# Create GDWorldData if not assigned.
	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = seed if seed != 0 else randi()

	# Create GDChunkManager if not assigned.
	if chunk_manager == null:
		chunk_manager = GDChunkManager.new()
		chunk_manager.world_data = world_data
		chunk_manager.loaded_radius = loaded_radius
		chunk_manager.view_radius = view_radius
		chunk_manager.tile_size = tile_size
		chunk_manager.surface_container_path = surface_container_path
		chunk_manager.underground_container_path = underground_container_path
		add_child(chunk_manager)

	# Connect signal: chunk_ready from world_data -> chunk_manager.
	if not world_data.chunk_ready.is_connected(chunk_manager.on_chunk_ready):
		world_data.chunk_ready.connect(chunk_manager.on_chunk_ready)

	# Generate initial chunks around origin.
	if auto_generate_start_chunks:
		_generate_initial_chunks()

	is_initialized = true
	chunk_bridge_ready.emit()


func process_frame(player_layer: StringName, player_chunk_pos: Vector2i) -> void:
	if not is_initialized:
		return

	# Process async generation results (emits chunk_ready signals).
	if world_data:
		world_data.process_async_results()

	# Update player chunk position and refresh all three tiers.
	if chunk_manager:
		chunk_manager.set_player_chunk(player_layer, player_chunk_pos.x, player_chunk_pos.y)
		chunk_manager.refresh_chunks()


func force_load_factory_chunk(layer: StringName, cx: int, cy: int) -> void:
	## Force-loads a chunk for factory simulation.
	## The chunk stays in memory and is simulated even when player is far away.
	## Call this when a machine/network is placed in a chunk.
	if chunk_manager:
		chunk_manager.force_load_chunk(layer, cx, cy)


func release_factory_chunk(layer: StringName, cx: int, cy: int) -> void:
	## Releases a force-loaded chunk.
	## The chunk may still be kept if within loaded_radius.
	if chunk_manager:
		chunk_manager.force_unload_chunk(layer, cx, cy)


func get_world_data() -> GDWorldData:
	return world_data


func get_chunk_manager() -> GDChunkManager:
	return chunk_manager


func on_terrain_cell_synced(layer: StringName, chunk: Vector2i, _local: Vector2i, _old_material: int, _new_material: int) -> void:
	if chunk_manager == null:
		return
	if not _is_chunk_visible(layer, chunk):
		return
	chunk_manager.hide_chunk(layer, chunk.x, chunk.y)
	chunk_manager.show_chunk(layer, chunk.x, chunk.y)


func _is_chunk_visible(layer: StringName, chunk: Vector2i) -> bool:
	if chunk_manager == null:
		return false
	for key in chunk_manager.get_visible_chunks():
		if StringName(key.get("layer", "")) == layer \
				and int(key.get("chunk_x", 0)) == chunk.x \
				and int(key.get("chunk_y", 0)) == chunk.y:
			return true
	return false


func unload_all() -> void:
	if chunk_manager:
		chunk_manager.unload_all_chunks()


func _get_map_layer_controller() -> Node:
	var parent: Node = get_parent()
	while parent:
		if parent.has_method(&"is_current_layer") and "current_layer" in parent:
			return parent
		parent = parent.get_parent()
	return null


func _initialize_if_needed() -> void:
	call_deferred("initialize")


func _generate_initial_chunks() -> void:
	var radius := start_chunk_radius
	for cx in range(-radius, radius + 1):
		for cy in range(-radius, radius + 1):
			world_data.request_chunk_async("surface", cx, cy)
			world_data.request_chunk_async("underground", cx, cy)
