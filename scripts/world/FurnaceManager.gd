class_name FurnaceManager extends Node

const FurnaceDataResource := preload("res://scripts/world/FurnaceData.gd")

signal furnace_placed(layer: StringName, cell: Vector2i)
signal furnace_removed(layer: StringName, cell: Vector2i)

var _furnaces: Dictionary = {}
var _recipes: Dictionary = {}


func _ready() -> void:
	_init_recipes()


func _init_recipes() -> void:
	var db := ItemDatabase
	var copper_ingot := db.mat_item(db.MATERIAL_COPPER, db.FORM_INGOT)
	var iron_ingot := db.mat_item(db.MATERIAL_IRON, db.FORM_INGOT)
	var stone_ingot := db.mat_item(db.MATERIAL_STONE, db.FORM_INGOT)

	_recipes[db.copper_crushed()] = { "output_id": copper_ingot, "output_count": 1, "time": 5.0 }
	_recipes[db.iron_crushed()] = { "output_id": iron_ingot, "output_count": 1, "time": 5.0 }
	_recipes[db.stone_dust()] = { "output_id": stone_ingot, "output_count": 1, "time": 3.0 }


func _process(delta: float) -> void:
	for key in _furnaces.keys():
		var data: FurnaceDataResource = _furnaces[key]
		_tick_furnace(data, delta)


func _tick_furnace(data: FurnaceDataResource, delta: float) -> void:
	if data.input_item_id == 0 or data.input_count <= 0:
		return

	var recipe: Dictionary = _recipes.get(data.input_item_id, {})
	if recipe.is_empty():
		return

	if data.output_item_id != 0 and data.output_item_id != recipe.output_id:
		return

	if data.output_item_id == recipe.output_id and data.output_count >= 64:
		return

	if data.fuel_burn_remaining <= 0.0:
		if not _try_consume_fuel(data):
			return

	data.fuel_burn_remaining -= delta
	data.smelt_progress += delta

	if data.smelt_progress >= data.smelt_target:
		data.smelt_progress = 0.0
		data.input_count -= 1
		if data.input_count <= 0:
			data.input_item_id = 0

		if data.output_item_id == 0:
			data.output_item_id = recipe.output_id
			data.output_count = recipe.output_count
		else:
			data.output_count += recipe.output_count


func _try_consume_fuel(data: FurnaceDataResource) -> bool:
	var burn_ticks := GDFuelRegistry.get_burn_ticks(data.fuel_item_id)
	if burn_ticks <= 0:
		return false
	var burn_time := burn_ticks / 20.0
	data.fuel_burn_remaining = burn_time
	data.fuel_burn_max = burn_time
	data.fuel_item_id = 0
	return true


func place_furnace(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if _furnaces.has(key):
		return false
	_furnaces[key] = FurnaceDataResource.new()
	furnace_placed.emit(layer, cell)
	return true


func remove_furnace(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	if not _furnaces.erase(key):
		return false
	furnace_removed.emit(layer, cell)
	return true


func get_furnace(layer: StringName, cell: Vector2i) -> FurnaceDataResource:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	return _furnaces.get(key, null)


func has_furnace(layer: StringName, cell: Vector2i) -> bool:
	var key := "%s,%d,%d" % [layer, cell.x, cell.y]
	return _furnaces.has(key)


func get_all_furnaces() -> Array:
	var result: Array = []
	for key in _furnaces.keys():
		var parts := String(key).split(",")
		if parts.size() == 3:
			result.append({
				"layer": parts[0],
				"cell": Vector2i(int(parts[1]), int(parts[2]))
			})
	return result


func get_fuel_burn_time(item_id: int) -> float:
	var ticks := GDFuelRegistry.get_burn_ticks(item_id)
	if ticks > 0:
		return ticks / 20.0
	return 0.0


func get_recipe_for(item_id: int) -> Dictionary:
	return _recipes.get(item_id, {})


func clear() -> void:
	_furnaces.clear()
