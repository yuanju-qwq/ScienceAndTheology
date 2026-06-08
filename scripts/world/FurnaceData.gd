class_name FurnaceData
extends Resource

var input_item_id: int = 0
var input_count: int = 0
var fuel_item_id: int = 0
var fuel_burn_remaining: float = 0.0
var fuel_burn_max: float = 0.0
var output_item_id: int = 0
var output_count: int = 0
var smelt_progress: float = 0.0
var smelt_target: float = 5.0

func is_burning() -> bool:
	return fuel_burn_remaining > 0.0

func get_progress_ratio() -> float:
	if smelt_target <= 0.0:
		return 0.0
	return clampf(smelt_progress / smelt_target, 0.0, 1.0)

func get_fuel_ratio() -> float:
	if fuel_burn_max <= 0.0:
		return 0.0
	return clampf(fuel_burn_remaining / fuel_burn_max, 0.0, 1.0)
