extends Node

class RecipeRef extends RefCounted:
	var recipe_type: String
	var data: Dictionary
	var display_name: String
	var inputs: Array[Dictionary]
	var outputs: Array[Dictionary]

var _by_output: Dictionary = {}
var _by_input: Dictionary = {}
var _all: Array[RecipeRef] = []

func _ready() -> void:
	rebuild()

func rebuild() -> void:
	_by_output.clear()
	_by_input.clear()
	_all.clear()
	_index_crafting()
	_index_machine()

func get_recipes_for_output(item_id: int) -> Array[RecipeRef]:
	return _by_output.get(item_id, [])

func get_recipes_for_input(item_id: int) -> Array[RecipeRef]:
	return _by_input.get(item_id, [])

func get_all_recipes() -> Array[RecipeRef]:
	return _all

func find_item_ids(query: String) -> Array[int]:
	var q := query.to_lower()
	var results: Array[int] = []
	var seen: Dictionary = {}
	for ref: RecipeRef in _all:
		for output in ref.outputs:
			var oid: int = output.get("item_id", 0)
			if oid > 0 and not seen.has(oid):
				seen[oid] = true
				if q.is_empty():
					results.append(oid)
				else:
					var def := ItemDatabase.get_item(oid)
					if def and tr(def.title_key).to_lower().contains(q):
						results.append(oid)
		for input_item in ref.inputs:
			var iid: int = input_item.get("item_id", 0)
			if iid > 0 and not seen.has(iid):
				seen[iid] = true
				if q.is_empty():
					results.append(iid)
				else:
					var def := ItemDatabase.get_item(iid)
					if def and tr(def.title_key).to_lower().contains(q):
						results.append(iid)
	return results

func _index_crafting() -> void:
	var raw: Array = GDCraftingManager.get_all_recipes()
	for r: Dictionary in raw:
		var ref := RecipeRef.new()
		ref.recipe_type = "crafting"
		ref.data = r
		ref.display_name = r.get("name", "")
		var out_id := int(r.get("output_item_id", 0))
		var out_count := int(r.get("output_count", 0))
		var out_dict := {"item_id": out_id, "count": out_count}
		ref.outputs = [out_dict]
		ref.inputs = []
		for inp: Dictionary in r.get("inputs", []):
			ref.inputs.append({
				"item_id": int(inp.get("item_id", 0)),
				"count": int(inp.get("count", 0)),
			})
		_all.append(ref)
		_add_to_index(ref)

func _index_machine() -> void:
	var machine_types: PackedStringArray = GDRecipeDatabase.get_machine_types()
	for mtype: String in machine_types:
		var raw: Array = GDRecipeDatabase.get_recipes_for_machine(mtype)
		for r: Dictionary in raw:
			var ref := RecipeRef.new()
			ref.recipe_type = "machine"
			ref.data = r
			ref.display_name = r.get("name", "")
			ref.inputs = []
			ref.outputs = []
			for inp: Dictionary in r.get("inputs", []):
				if inp.get("type", "item") != "item":
					continue
				ref.inputs.append({
					"item_id": int(inp.get("item_id", 0)),
					"count": int(inp.get("amount", inp.get("count", 1))),
				})
			for outp: Dictionary in r.get("outputs", []):
				if outp.get("type", "item") != "item":
					continue
				ref.outputs.append({
					"item_id": int(outp.get("item_id", 0)),
					"count": int(outp.get("amount", outp.get("count", 1))),
					"probability": float(outp.get("probability", 1.0)),
				})
			_all.append(ref)
			_add_to_index(ref)

func _add_to_index(ref: RecipeRef) -> void:
	for out in ref.outputs:
		var oid := int(out.get("item_id", 0))
		if oid <= 0:
			continue
		if not _by_output.has(oid):
			_by_output[oid] = []
		_by_output[oid].append(ref)
	for inp in ref.inputs:
		var iid := int(inp.get("item_id", 0))
		if iid <= 0:
			continue
		if not _by_input.has(iid):
			_by_input[iid] = []
		_by_input[iid].append(ref)
