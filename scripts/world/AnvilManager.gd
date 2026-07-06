class_name AnvilManager
extends GDAnvilManager


# Uniform interface for MachineCollisionBridge. Returns [{dimension, cell}, ...].
func get_machine_cells() -> Array:
	return get_all_anvils()
