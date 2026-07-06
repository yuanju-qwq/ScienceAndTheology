class_name MachineCollisionBridge
extends Node

# Bridges FurnaceManager (and future machine managers) to the C++ machine
# collision overlay on WorldData, and triggers a chunk collision rebuild so
# the furnace cell gets collision coverage from the chunk-level collision
# mesh instead of a per-object Godot StaticBody3D.
#
# Design: docs/专用引擎性能优化方向.md (physics layer):
#   "只给玩家附近 chunk 生成碰撞"
#   "机器/管线不用 Godot 物理节点表示"
#
# Routing:
#   furnace_placed  -> world_data.set_machine_collision(true)
#                  -> chunk_bridge.refresh_cell(chunk, local)
#                  -> ChunkRendererBridge rebuilds the section's collision,
#                     calling GDChunkHelper.build_collision_faces with the
#                     machine_collision_mask parameter.
#   furnace_removed -> world_data.set_machine_collision(false)
#                  -> chunk_bridge.refresh_cell(chunk, local)
#
# Re-applying overlay on dimension load:
#   FurnaceManager's authoritative state already survives save/load (its own
#   save system). On a load, the FurnaceManager emits furnace_placed for each
#   restored furnace (via _sync_existing_objects consumers), so this bridge
#   re-applies the overlay without persisting it separately.

@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"

var _furnace_manager: FurnaceManager = null
var _world_data: GDWorldData = null
var _chunk_bridge: ChunkRendererBridge = null


func _ready() -> void:
	call_deferred(&"_connect_signals")


# Resolve node references and connect signals. Deferred so sibling nodes
# added by the scene loader are ready before we touch them.
func _connect_signals() -> void:
	_furnace_manager = get_node_or_null(furnace_manager_path) as FurnaceManager
	# ChunkRendererBridge owns the GDWorldData; expose it via the world_data
	# property rather than looking for a separate node.
	_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	if _chunk_bridge != null:
		_world_data = _chunk_bridge.get_world_data()
	if _furnace_manager == null or _world_data == null or _chunk_bridge == null:
		push_warning("MachineCollisionBridge: missing dependencies; furnace collision will not sync.")
		return
	_furnace_manager.furnace_placed.connect(_on_furnace_placed)
	_furnace_manager.furnace_removed.connect(_on_furnace_removed)


func _on_furnace_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)


func _on_furnace_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)


# Update the overlay and request a chunk collision rebuild for the cell.
func _mark_machine_cell(dimension: StringName, cell: Vector3i, occupied: bool) -> void:
	if _world_data == null:
		return
	_world_data.set_machine_collision(
		String(dimension), cell.x, cell.y, cell.z, occupied)
	# Refresh the chunk so ChunkRendererBridge rebuilds the affected section's
	# collision mesh. refresh_cell is a no-op when the chunk is not visible,
	# which means distant furnaces won't trigger expensive rebuilds until the
	# player approaches.
	if _chunk_bridge != null and dimension == _chunk_bridge.active_dimension:
		var chunk := _chunk_bridge.cell_to_chunk(cell)
		var local := _chunk_bridge.cell_to_local(cell)
		_chunk_bridge.refresh_cell(dimension, chunk, local)
