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
##   chunk_bridge.force_load_factory_chunk(WorldLayers.SURFACE, 5, 3)

signal chunk_bridge_ready

const BuiltinTerrainContentScript := preload("res://scripts/worldgen/BuiltinTerrainContent.gd")

@export var world_data: GDWorldData = null
@export var chunk_manager: GDChunkManager = null
@export var worldgen_config: GDWorldGenConfig = null

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

@export var max_async_results_per_frame: int = 4:
	set(value):
		max_async_results_per_frame = max(0, value)
		if world_data:
			world_data.max_async_results_per_frame = max_async_results_per_frame

@export var max_chunk_load_requests_per_frame: int = 12:
	set(value):
		max_chunk_load_requests_per_frame = max(0, value)
		if chunk_manager:
			chunk_manager.max_chunk_load_requests_per_frame = max_chunk_load_requests_per_frame

@export var max_chunk_views_per_frame: int = 2:
	set(value):
		max_chunk_views_per_frame = max(0, value)
		if chunk_manager:
			chunk_manager.max_chunk_views_per_frame = max_chunk_views_per_frame

@export var tile_size: int = 32:
	set(value):
		tile_size = value
		if chunk_manager:
			chunk_manager.tile_size = value

@export var auto_generate_start_chunks := true
@export var start_chunk_radius := 2
@export var player_node_path: NodePath = ^"../Player"
@export var auto_update := true
@export var connector_manager_path: NodePath = ^"../ConnectorManager"
@export var mechanism_manager_path: NodePath = ^"../MechanismManager"
@export var debug_chunk_streaming := false
@export var debug_chunk_streaming_interval := 2.0

var is_initialized := false
var _connector_manager: Node = null
var _mechanism_manager: Node = null
var _debug_chunk_streaming_elapsed := 0.0
var _debug_generated_connector_count := 0
var _debug_generated_connector_chunk_count := 0
var _debug_generated_mechanism_count := 0
var _debug_generated_mechanism_chunk_count := 0


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

	_maybe_log_chunk_streaming(delta)


func initialize() -> void:
	if is_initialized:
		return

	_connector_manager = get_node_or_null(connector_manager_path)
	_mechanism_manager = get_node_or_null(mechanism_manager_path)

	# Create GDWorldData if not assigned.
	if world_data == null:
		world_data = GDWorldData.new()
		world_data.seed = seed if seed != 0 else randi()
	world_data.max_async_results_per_frame = max_async_results_per_frame

	if worldgen_config == null:
		worldgen_config = BuiltinTerrainContentScript.create_default_config()
	world_data.worldgen_config = worldgen_config

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
	chunk_manager.max_chunk_load_requests_per_frame = max_chunk_load_requests_per_frame
	chunk_manager.max_chunk_views_per_frame = max_chunk_views_per_frame

	# Connect signal: chunk_ready from world_data -> chunk_manager.
	if not world_data.chunk_ready.is_connected(chunk_manager.on_chunk_ready):
		world_data.chunk_ready.connect(chunk_manager.on_chunk_ready)
	if not world_data.chunk_ready.is_connected(_on_chunk_ready):
		world_data.chunk_ready.connect(_on_chunk_ready)

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

	_maybe_log_chunk_streaming(0.0)


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


func _on_chunk_ready(layer: String, chunk_x: int, chunk_y: int) -> void:
	_sync_generated_connectors(StringName(layer), chunk_x, chunk_y)
	_sync_generated_mechanisms(StringName(layer), chunk_x, chunk_y)


func _sync_generated_connectors(layer: StringName, chunk_x: int, chunk_y: int) -> void:
	if world_data == null or _connector_manager == null:
		return
	if not _connector_manager.has_method(&"add_generated_connectors"):
		return

	var generated_connectors: Array = world_data.get_chunk_connectors(layer, chunk_x, chunk_y)
	if generated_connectors.is_empty():
		return

	var added: int = _connector_manager.add_generated_connectors(generated_connectors)
	if added > 0:
		_record_generated_connectors(added)


func _sync_generated_mechanisms(layer: StringName, chunk_x: int, chunk_y: int) -> void:
	if world_data == null or _mechanism_manager == null:
		return
	if not _mechanism_manager.has_method(&"add_generated_mechanisms"):
		return

	var generated_mechanisms: Array = world_data.get_chunk_mechanisms(layer, chunk_x, chunk_y)
	if generated_mechanisms.is_empty():
		return

	var added: int = _mechanism_manager.add_generated_mechanisms(generated_mechanisms)
	if added > 0:
		_record_generated_mechanisms(added)


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
			world_data.request_chunk_async(WorldLayers.SURFACE, cx, cy)
			world_data.request_chunk_async(WorldLayers.UNDERGROUND, cx, cy)


func _maybe_log_chunk_streaming(delta: float) -> void:
	if not debug_chunk_streaming or world_data == null or chunk_manager == null:
		return
	_debug_chunk_streaming_elapsed += delta
	if _debug_chunk_streaming_elapsed < debug_chunk_streaming_interval:
		return
	_debug_chunk_streaming_elapsed = 0.0

	print(
		"ChunkRendererBridge: streaming pending=%d ready=%d visible_queue=%d loaded=%d visible=%d connectors=%d/%d_chunks mechanisms=%d/%d_chunks" % [
			world_data.get_async_pending_count(),
			world_data.get_async_result_queue_size(),
			chunk_manager.get_pending_visible_chunk_count(),
			chunk_manager.get_loaded_chunk_count(),
			chunk_manager.get_visible_chunk_count(),
			_debug_generated_connector_count,
			_debug_generated_connector_chunk_count,
			_debug_generated_mechanism_count,
			_debug_generated_mechanism_chunk_count,
		]
	)
	_debug_generated_connector_count = 0
	_debug_generated_connector_chunk_count = 0
	_debug_generated_mechanism_count = 0
	_debug_generated_mechanism_chunk_count = 0


func _record_generated_connectors(count: int) -> void:
	if not debug_chunk_streaming:
		return
	_debug_generated_connector_count += count
	_debug_generated_connector_chunk_count += 1


func _record_generated_mechanisms(count: int) -> void:
	if not debug_chunk_streaming:
		return
	_debug_generated_mechanism_count += count
	_debug_generated_mechanism_chunk_count += 1
