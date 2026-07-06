class_name FurnaceManager
extends GDFurnaceManager

# Server-side furnace state lives in GDFurnaceManager.
# This script keeps the existing scene path/class name stable for Godot nodes.


# Uniform interface for MachineCollisionBridge. Returns [{dimension, cell}, ...].
func get_machine_cells() -> Array:
	return get_all_furnaces()
