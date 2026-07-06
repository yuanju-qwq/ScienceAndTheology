class_name MachineCollisionBridge
extends Node

# Bridges all machine managers (Furnace, Bloomery, PitKiln, CharcoalPit, Anvil)
# to the C++ machine collision overlay on WorldData, and triggers a chunk
# collision rebuild so each machine cell gets collision coverage from the
# chunk-level collision mesh instead of a per-object Godot StaticBody3D.
#
# Design: docs/专用引擎性能优化方向.md (physics layer):
#   "只给玩家附近 chunk 生成碰撞"
#   "机器/管线不用 Godot 物理节点表示"
#
# Routing (same for every manager):
#   <machine>_placed   -> world_data.set_machine_collision(true)
#                     -> chunk_bridge.refresh_cell(chunk, local)
#                     -> ChunkRendererBridge rebuilds the section's collision,
#                        calling GDChunkHelper.build_collision_faces with the
#                        machine_collision_mask parameter.
#   <machine>_removed  -> world_data.set_machine_collision(false)
#                     -> chunk_bridge.refresh_cell(chunk, local)
#
# Re-applying overlay on load / dimension switch:
#   The overlay is in-memory C++ state. On scene ready (and after an active
#   planet change), _sync_existing_objects iterates each manager's get_all_*()
#   and re-applies set_machine_collision(true) for every cell. This keeps the
#   overlay consistent regardless of whether signals fired before this node
#   connected. State is NOT persisted here — the authoritative source is each
#   manager's own data.

@export var furnace_manager_path: NodePath = ^"../FurnaceManager"
@export var bloomery_manager_path: NodePath = ^"../BloomeryManager"
@export var pit_kiln_manager_path: NodePath = ^"../PitKilnManager"
@export var charcoal_pit_manager_path: NodePath = ^"../CharcoalPitManager"
@export var anvil_manager_path: NodePath = ^"../AnvilManager"
@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var universe_manager_path: NodePath = ^"../UniverseManager"

var _furnace_manager: FurnaceManager = null
var _bloomery_manager: BloomeryManager = null
var _pit_kiln_manager: PitKilnManager = null
var _charcoal_pit_manager: CharcoalPitManager = null
var _anvil_manager: AnvilManager = null
var _world_data: GDWorldData = null
var _chunk_bridge: ChunkRendererBridge = null
var _universe_manager: UniverseManager = null


func _ready() -> void:
	call_deferred(&"_connect_signals")


# Resolve node references, connect signals, and seed the overlay from
# existing manager state. Deferred so sibling nodes added by the scene
# loader are ready before we touch them.
func _connect_signals() -> void:
	_furnace_manager = get_node_or_null(furnace_manager_path) as FurnaceManager
	_bloomery_manager = get_node_or_null(bloomery_manager_path) as BloomeryManager
	_pit_kiln_manager = get_node_or_null(pit_kiln_manager_path) as PitKilnManager
	_charcoal_pit_manager = get_node_or_null(charcoal_pit_manager_path) as CharcoalPitManager
	_anvil_manager = get_node_or_null(anvil_manager_path) as AnvilManager
	_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	if _chunk_bridge != null:
		_world_data = _chunk_bridge.get_world_data()
	if _world_data == null:
		push_warning("MachineCollisionBridge: missing WorldData; machine collision will not sync.")
		return
	# Wire up every manager.
	if _furnace_manager != null:
		_furnace_manager.furnace_placed.connect(_on_furnace_placed)
		_furnace_manager.furnace_removed.connect(_on_furnace_removed)
	if _bloomery_manager != null:
		_bloomery_manager.bloomery_placed.connect(_on_bloomery_placed)
		_bloomery_manager.bloomery_removed.connect(_on_bloomery_removed)
	if _pit_kiln_manager != null:
		_pit_kiln_manager.kiln_placed.connect(_on_kiln_placed)
		_pit_kiln_manager.kiln_removed.connect(_on_kiln_removed)
	if _charcoal_pit_manager != null:
		_charcoal_pit_manager.pit_placed.connect(_on_pit_placed)
		_charcoal_pit_manager.pit_removed.connect(_on_pit_removed)
	if _anvil_manager != null:
		_anvil_manager.anvil_placed.connect(_on_anvil_placed)
		_anvil_manager.anvil_removed.connect(_on_anvil_removed)
	# Re-sync on active planet changes so the new dimension's overlay is
	# populated for all managers at once.
	if _universe_manager != null:
		_universe_manager.active_planet_changed.connect(_on_active_planet_changed)
	# Seed overlay from existing manager state.
	_sync_existing_objects()


# --- Signal handlers (one per manager type) ---

func _on_furnace_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)

func _on_furnace_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)

func _on_bloomery_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)

func _on_bloomery_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)

func _on_kiln_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)

func _on_kiln_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)

func _on_pit_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)

func _on_pit_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)

func _on_anvil_placed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, true)

func _on_anvil_removed(dimension: StringName, cell: Vector3i) -> void:
	_mark_machine_cell(dimension, cell, false)


# Update the overlay and request a chunk collision rebuild for the cell.
func _mark_machine_cell(dimension: StringName, cell: Vector3i, occupied: bool) -> void:
	if _world_data == null:
		return
	_world_data.set_machine_collision(
		String(dimension), cell.x, cell.y, cell.z, occupied)
	# Refresh the chunk so ChunkRendererBridge rebuilds the affected section's
	# collision mesh. refresh_cell is a no-op when the chunk is not visible,
	# which means distant machines won't trigger expensive rebuilds until the
	# player approaches.
	if _chunk_bridge != null and dimension == _chunk_bridge.active_dimension:
		var chunk := _chunk_bridge.cell_to_chunk(cell)
		var local := _chunk_bridge.cell_to_local(cell)
		_chunk_bridge.refresh_cell(dimension, chunk, local)


# Re-populate the overlay from every manager's authoritative state. Called
# on ready (after _connect_signals) and on active planet change. Only marks
# cells (no erasure) — managers that lost a machine will have emitted the
# removed signal before the switch. Uses the uniform get_machine_cells()
# interface so adding a new machine type only requires implementing that
# method on the new manager.
func _sync_existing_objects() -> void:
	if _world_data == null:
		return
	for manager in _get_machine_managers():
		if manager == null:
			continue
		for entry in manager.get_machine_cells():
			_apply_entry(entry)


# Collect all machine managers into one list. Adding a new machine type only
# requires appending it here.
func _get_machine_managers() -> Array:
	return [_furnace_manager, _bloomery_manager, _pit_kiln_manager,
			_charcoal_pit_manager, _anvil_manager]


# Helper: read {dimension, cell} and mark overlay without triggering a chunk
# rebuild (initial sync rebuilds happen naturally when chunks load).
func _apply_entry(entry: Dictionary) -> void:
	var dimension := StringName(entry.get("dimension", ""))
	var cell: Vector3i = entry.get("cell", Vector3i.ZERO)
	if dimension == &"":
		return
	_world_data.set_machine_collision(
		String(dimension), cell.x, cell.y, cell.z, true)


# Active planet changed: re-seed overlay for the new dimension. The overlay
# is keyed by (dimension, cell) so old entries are harmless; we just need to
# ensure the new dimension's machines are present.
func _on_active_planet_changed(_planet: PlanetDescriptor) -> void:
	_sync_existing_objects()
