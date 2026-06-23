# ShipCommandBridge — GDScript wrapper around GDShipCommandBridge.
#
# Responsibilities:
# - Binds the native bridge to GDWorldData from ChunkRendererBridge.
# - Forwards terrain deltas to ChunkRendererBridge so static blocks disappear / reappear.
# - Provides a single scene node for PlayerInteraction and DynamicStructureRenderer.
class_name ShipCommandBridge
extends GDShipCommandBridge

@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var default_max_ship_blocks := 4096

var _chunk_bridge: ChunkRendererBridge = null


func _ready() -> void:
	call_deferred(&"_configure_bridge")


func _configure_bridge() -> void:
	_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	if _chunk_bridge != null:
		set_world_data(_chunk_bridge.get_world_data())
	if not terrain_cell_synced.is_connected(_on_terrain_cell_synced):
		terrain_cell_synced.connect(_on_terrain_cell_synced)


func assemble_from_cell(dimension: StringName, seed_cell: Vector3i,
		allowed_material_ids: Array = [], max_blocks: int = -1) -> Dictionary:
	if max_blocks <= 0:
		max_blocks = default_max_ship_blocks
	return assemble_ship(dimension, seed_cell, allowed_material_ids, max_blocks)


func _on_terrain_cell_synced(dimension: StringName, chunk: Vector3i, local: Vector3i,
		old_material: int, new_material: int) -> void:
	if _chunk_bridge == null:
		return
	_chunk_bridge.on_terrain_cell_synced(dimension, chunk, local, old_material, new_material)
