# PhysicsTerrainSyncBridge — bridges C++ BlockPhysicsSystem terrain events to chunk rendering.
#
# GameCommandServer already emits terrain_cell_synced for direct player commands.
# BlockPhysicsSystem, however, mutates terrain later during simulation ticks
# through EventBus::TERRAIN_CHANGED. This node converts those tick-system
# signals into ChunkRendererBridge refresh calls so cave-ins and falling blocks
# become visible immediately.
class_name PhysicsTerrainSyncBridge
extends Node

@export var tick_system_path: NodePath = ^"../GDTickSystem"
@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var chunk_size := 32

var _tick_system: GDTickSystem = null
var _chunk_bridge: ChunkRendererBridge = null


func _ready() -> void:
	call_deferred(&"_connect_signals")


func _connect_signals() -> void:
	_tick_system = get_node_or_null(tick_system_path) as GDTickSystem
	_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	if _tick_system == null or _chunk_bridge == null:
		return
	if not _tick_system.terrain_changed.is_connected(_on_terrain_changed):
		_tick_system.terrain_changed.connect(_on_terrain_changed)


func _on_terrain_changed(dimension: String, cx: int, cy: int, cz: int,
		x: int, y: int, z: int, old_material: int, new_material: int) -> void:
	if _chunk_bridge == null:
		return
	var dim := StringName(dimension)
	var chunk := Vector3i(cx, cy, cz)
	var local := Vector3i(x, y, z)
	_chunk_bridge.on_terrain_cell_synced(
		dim, chunk, local, old_material, new_material)
	_refresh_boundary_neighbors(dim, chunk, local, old_material, new_material)


func _refresh_boundary_neighbors(dimension: StringName, chunk: Vector3i,
		local: Vector3i, old_material: int, new_material: int) -> void:
	# Greedy mesh face culling depends on neighbor chunk boundary cells. When a
	# physics update touches a border cell, rebuild the adjacent visible chunk too.
	if local.x == 0:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(-1, 0, 0),
			Vector3i(chunk_size - 1, local.y, local.z))
	elif local.x == chunk_size - 1:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(1, 0, 0),
			Vector3i(0, local.y, local.z))

	if local.y == 0:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(0, -1, 0),
			Vector3i(local.x, chunk_size - 1, local.z))
	elif local.y == chunk_size - 1:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(0, 1, 0),
			Vector3i(local.x, 0, local.z))

	if local.z == 0:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(0, 0, -1),
			Vector3i(local.x, local.y, chunk_size - 1))
	elif local.z == chunk_size - 1:
		_chunk_bridge.refresh_cell(
			dimension, chunk + Vector3i(0, 0, 1),
			Vector3i(local.x, local.y, 0))
