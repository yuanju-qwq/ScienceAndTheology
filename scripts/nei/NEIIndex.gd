class_name NEIIndex
extends Node

## NEIIndex is the query/index layer for the in-game item and recipe browser.
## It reads from the runtime registries instead of becoming the authority itself.
## ItemDatabase / GDCraftingManager / GDRecipeDatabase remain the gameplay data sources.

class RecipeRef extends RefCounted:
	var id: String = ""
	var recipe_type: String = ""
	var machine_type: String = ""
	var display_name: String = ""
	var data: Dictionary = {}
	var item_inputs: Array[Dictionary] = []
	var item_outputs: Array[Dictionary] = []
	var fluid_inputs: Array[Dictionary] = []
	var fluid_outputs: Array[Dictionary] = []
	var tools: PackedStringArray = PackedStringArray()
	var conditions: Dictionary = {}
	var min_tier: int = 0
	var eu_per_tick: int = 0
	var duration_ticks: int = 0

var _built := false
var _all_items: Array[int] = []
var _all_recipes: Array[RecipeRef] = []
var _by_output_item: Dictionary = {}  # item_id -> Array[RecipeRef]
var _by_input_item: Dictionary = {}   # item_id -> Array[RecipeRef]
var _by_output_fluid: Dictionary = {} # fluid_name -> Array[RecipeRef]
var _by_input_fluid: Dictionary = {}  # fluid_name -> Array[RecipeRef]
var _by_machine: Dictionary = {}      # machine_type -> Array[RecipeRef]
var _normalized_item_names: Dictionary = {} # item_id -> String
var _item_keys: Dictionary = {}       # item_id -> String


func _ready() -> void:
	rebuild()


func ensure_built() -> void:
	if not _built:
		rebuild()


func rebuild() -> void:
	_built = false
	_all_items.clear()
	_all_recipes.clear()
	_by_output_item.clear()
	_by_input_item.clear()
	_by_output_fluid.clear()
	_by_input_fluid.clear()
	_by_machine.clear()
	_normalized_item_names.clear()
	_item_keys.clear()
	_index_items()
	_index_crafting_recipes()
	_index_machine_recipes()
	_built = true


func get_all_item_ids() -> Array[int]:
	ensure_built()
	return _all_items.duplicate()


func search_item_ids(query: String) -> Array[int]:
	ensure_built()
	var q := _normalize(query)
	if q.is_empty():
		return _all_items.duplicate()

	var results: Array[int] = []
	for item_id in _all_items:
		var name: String = _normalized_item_names.get(item_id, "")
		var key: String = _item_keys.get(item_id, "")
		if name.contains(q) or key.contains(q):
			results.append(item_id)
	return results


func get_recipes_for_output(item_id: int) -> Array[RecipeRef]:
	ensure_built()
	return (_by_output_item.get(item_id, []) as Array).duplicate()


func get_recipes_for_input(item_id: int) -> Array[RecipeRef]:
	ensure_built()
	return (_by_input_item.get(item_id, []) as Array).duplicate()


func get_recipes_for_machine(machine_type: String) -> Array[RecipeRef]:
	ensure_built()
	return (_by_machine.get(machine_type, []) as Array).duplicate()


func get_all_recipes() -> Array[RecipeRef]:
	ensure_built()
	return _all_recipes.duplicate()


func get_item_key(item_id: int) -> String:
	ensure_built()
	return _item_keys.get(item_id, "")


func get_item_display_name(item_id: int) -> String:
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		return "Item #%d" % item_id
	return tr(def.title_key)


func _index_items() -> void:
	var ids: Array[int] = ItemDatabase.get_all_item_ids()
	ids.sort()
	for item_id in ids:
		if item_id <= 0:
			continue
		_all_items.append(item_id)
		var def = ItemDatabase.get_item(item_id)
		if def != null:
			_normalized_item_names[item_id] = _normalize(tr(def.title_key))
		var key := ""
		if ItemDatabase.has_method("get_item_key_by_id"):
			key = str(ItemDatabase.get_item_key_by_id(item_id))
		_item_keys[item_id] = _normalize(key)


func _index_crafting_recipes() -> void:
	if not ClassDB.class_exists("GDCraftingManager"):
		return
	var raw: Array = GDCraftingManager.get_all_recipes()
	for r in raw:
		if typeof(r) != TYPE_DICTIONARY:
			continue
		var data := r as Dictionary
		var ref := RecipeRef.new()
		ref.recipe_type = "crafting"
		ref.id = str(data.get("id", data.get("name", "crafting_%d" % _all_recipes.size())))
		ref.display_name = str(data.get("name", ref.id))
		ref.data = data
		ref.machine_type = str(data.get("required_station", ""))
		if data.has("required_tool") and str(data.get("required_tool", "")) != "":
			ref.tools.append(str(data.get("required_tool", "")))

		var output_stack := _read_crafting_output(data)
		if not output_stack.is_empty():
			ref.item_outputs.append(output_stack)

		for raw_input in data.get("inputs", []):
			if typeof(raw_input) != TYPE_DICTIONARY:
				continue
			var stack := _read_item_stack(raw_input as Dictionary)
			if not stack.is_empty():
				ref.item_inputs.append(stack)

		_register_recipe(ref)


func _index_machine_recipes() -> void:
	if not ClassDB.class_exists("GDRecipeDatabase"):
		return
	var machine_types: PackedStringArray = GDRecipeDatabase.get_machine_types()
	for machine_type in machine_types:
		var raw: Array = GDRecipeDatabase.get_recipes_for_machine(machine_type)
		for r in raw:
			if typeof(r) != TYPE_DICTIONARY:
				continue
			var data := r as Dictionary
			var ref := RecipeRef.new()
			ref.recipe_type = "machine"
			ref.machine_type = str(data.get("machine_type", machine_type))
			ref.id = str(data.get("id", data.get("name", "%s_%d" % [ref.machine_type, _all_recipes.size()])))
			ref.display_name = str(data.get("name", ref.id))
			ref.data = data
			ref.min_tier = int(data.get("min_tier", 0))
			ref.eu_per_tick = int(data.get("eu_per_tick", 0))
			ref.duration_ticks = int(data.get("duration_ticks", 0))
			ref.conditions = data.get("conditions", {})

			for raw_input in data.get("inputs", []):
				if typeof(raw_input) != TYPE_DICTIONARY:
					continue
				var input := raw_input as Dictionary
				var input_type := str(input.get("type", "item"))
				if input_type == "item":
					var stack := _read_item_stack(input)
					if not stack.is_empty():
						ref.item_inputs.append(stack)
				elif input_type == "fluid":
					ref.fluid_inputs.append(_read_fluid_stack(input))

			for raw_output in data.get("outputs", []):
				if typeof(raw_output) != TYPE_DICTIONARY:
					continue
				var output := raw_output as Dictionary
				var output_type := str(output.get("type", "item"))
				if output_type == "item":
					var stack := _read_item_stack(output)
					if not stack.is_empty():
						if output.has("probability"):
							stack["probability"] = float(output.get("probability", 1.0))
						ref.item_outputs.append(stack)
				elif output_type == "fluid":
					ref.fluid_outputs.append(_read_fluid_stack(output))

			_register_recipe(ref)


func _register_recipe(ref: RecipeRef) -> void:
	if ref.item_inputs.is_empty() and ref.item_outputs.is_empty() and ref.fluid_inputs.is_empty() and ref.fluid_outputs.is_empty():
		return
	_all_recipes.append(ref)
	if not ref.machine_type.is_empty():
		_add_to_index(_by_machine, ref.machine_type, ref)
	for stack in ref.item_outputs:
		_add_to_index(_by_output_item, int(stack.get("item_id", 0)), ref)
	for stack in ref.item_inputs:
		_add_to_index(_by_input_item, int(stack.get("item_id", 0)), ref)
	for stack in ref.fluid_outputs:
		_add_to_index(_by_output_fluid, str(stack.get("fluid_name", "")), ref)
	for stack in ref.fluid_inputs:
		_add_to_index(_by_input_fluid, str(stack.get("fluid_name", "")), ref)


func _add_to_index(index: Dictionary, key, ref: RecipeRef) -> void:
	if key == null:
		return
	if key is int and key <= 0:
		return
	if key is String and (key as String).is_empty():
		return
	if not index.has(key):
		index[key] = []
	index[key].append(ref)


func _read_crafting_output(data: Dictionary) -> Dictionary:
	if data.has("output") and typeof(data["output"]) == TYPE_DICTIONARY:
		return _read_item_stack(data["output"] as Dictionary)
	var item_id := int(data.get("output_item_id", 0))
	if item_id <= 0:
		return {}
	return {
		"item_id": item_id,
		"count": int(data.get("output_count", data.get("amount", 1))),
	}


func _read_item_stack(data: Dictionary) -> Dictionary:
	var item_id := int(data.get("item_id", 0))
	if item_id <= 0 and data.has("item_key"):
		item_id = _resolve_item_key(str(data.get("item_key", "")))
	if item_id <= 0:
		return {}
	return {
		"item_id": item_id,
		"count": int(data.get("amount", data.get("count", 1))),
	}


func _read_fluid_stack(data: Dictionary) -> Dictionary:
	return {
		"fluid_name": str(data.get("fluid_name", data.get("fluid", ""))),
		"amount": int(data.get("amount", 0)),
	}


func _resolve_item_key(item_key: String) -> int:
	if item_key.is_empty():
		return -1
	if ItemDatabase.has_method("get_item_id_by_key"):
		var id_from_item_db := int(ItemDatabase.get_item_id_by_key(item_key))
		if id_from_item_db > 0:
			return id_from_item_db
	if ClassDB.class_exists("GDCraftingManager") and GDCraftingManager.has_method("get_item_id_by_key"):
		return int(GDCraftingManager.get_item_id_by_key(item_key))
	return -1


func _normalize(text: String) -> String:
	return text.strip_edges().to_lower()
