class_name PitKilnManager
extends GDPitKilnManager


# Uniform interface for MachineCollisionBridge. Returns [{dimension, cell}, ...].
func get_machine_cells() -> Array:
	return get_all_kilns()
