# ContainerAdapter — GDScript-side adapter bridging game containers
# to the C++ IContainerAccess interface via Callable.
#
# The C++ CallableContainerAccess calls methods on this object by name.
# This adapter queries GDWorldData / block entities to fulfill those
# calls, allowing the flow executor to interact with any in-game
# container (furnace, chest, machine, tank, ...).
#
# Usage:
#   var adapter := ContainerAdapter.new()
#   adapter.setup(world_data, block_pos, "Furnace")
#   flow_manager.register_scripted_container("Furnace", Callable(adapter, "_call"))
extends RefCounted
class_name ContainerAdapter

var world_data: GDWorldData = null
var block_pos: Vector3i = Vector3i.ZERO
var display_name: String = "Container"
var container_type: String = ""

# Cached capability flags (set during setup).
var _has_items: bool = false
var _has_fluids: bool = false
var _has_energy: bool = false
var _has_redstone: bool = false


## Configure the adapter for a specific container.
func setup(p_world_data: GDWorldData, p_block_pos: Vector3i, p_name: String) -> void:
	world_data = p_world_data
	block_pos = p_block_pos
	display_name = p_name
	# Determine capabilities based on block type.
	# In a full implementation, this would query the block entity registry.
	_has_items = true
	_has_fluids = false
	_has_energy = false
	_has_redstone = false


## Unified callback entry point — called by CallableContainerAccess.
##
## The C++ side calls this with a method name string and optional args.
## We dispatch to the appropriate method below.
func _call(method: String, ...args) -> Variant:
	if not has_method(method):
		push_warning("ContainerAdapter: unknown method '%s'" % method)
		return null
	return callv(method, args)


# ============================================================
# IContainerAccess method implementations
# ============================================================

func has_items() -> bool:
	return _has_items


func has_fluids() -> bool:
	return _has_fluids


func has_energy() -> bool:
	return _has_energy


func has_redstone() -> bool:
	return _has_redstone


func count_item(item_id: int) -> int:
	# Query the block entity's inventory for this item.
	# In a full implementation, this would call world_data.get_furnace_manager()
	# or similar to get the actual inventory.
	if world_data == null:
		return 0
	# Placeholder: return 0 until full inventory integration is done.
	return 0


func count_total_items() -> int:
	if world_data == null:
		return 0
	return 0


func extract_item(item_id: int, count: int) -> int:
	# Extract up to 'count' items from the container.
	# Returns the actual amount extracted.
	if world_data == null:
		return 0
	# Placeholder: full implementation will call the container's extract API.
	return 0


func insert_item(item_id: int, count: int) -> int:
	# Insert up to 'count' items into the container.
	# Returns the actual amount inserted.
	if world_data == null:
		return 0
	# Placeholder: full implementation will call the container's insert API.
	return 0


func list_items() -> Array:
	# Returns an Array of Dictionaries: [{item_id, count}, ...]
	if world_data == null:
		return []
	return []


func count_fluid(fluid_id: int) -> int:
	return 0


func extract_fluid(fluid_id: int, amount_mb: int) -> int:
	return 0


func insert_fluid(fluid_id: int, amount_mb: int) -> int:
	return 0


func list_fluids() -> Array:
	return []


func get_energy_stored() -> int:
	return 0


func get_energy_capacity() -> int:
	return 0


func extract_energy(amount: int) -> int:
	return 0


func insert_energy(amount: int) -> int:
	return 0


func get_redstone_signal() -> int:
	return 0


func set_redstone_signal(signal_val: int) -> void:
	pass
