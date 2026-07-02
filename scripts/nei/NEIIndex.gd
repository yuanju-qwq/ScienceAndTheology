class_name NEIIndexScript
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
# Ore dictionary: ore_name -> Array[item_id]. Mirrors codechicken.nei OreDict.
var _ore_dict: Dictionary = {}
# Reverse ore dict: item_id -> Array[ore_name].
var _item_ores: Dictionary = {}
# Item category subset: ItemDef.Category -> Array[item_id].
var _by_category: Dictionary = {}
# Item mod/source tag: item_id -> String (e.g. "vanilla", "gregtech").
var _item_sources: Dictionary = {}
# Tooltip text cache: item_id -> String (normalized searchable tooltip).
var _item_tooltips: Dictionary = {}


func _ready() -> void:
	rebuild()


func ensure_built() -> void:
	if not _built:
		rebuild()


func rebuild() -> void:
	var total_started_usec := Time.get_ticks_usec()
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
	_ore_dict.clear()
	_item_ores.clear()
	_by_category.clear()
	_item_sources.clear()
	_item_tooltips.clear()
	var stage_started_usec := Time.get_ticks_usec()
	_index_items()
	_print_perf("NEIIndex.index_items items=%d" % _all_items.size(), stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_index_ore_dict()
	_print_perf("NEIIndex.index_ore_dict ores=%d" % _ore_dict.size(), stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_index_crafting_recipes()
	_print_perf("NEIIndex.index_crafting_recipes recipes=%d" % _all_recipes.size(), stage_started_usec)
	stage_started_usec = Time.get_ticks_usec()
	_index_machine_recipes()
	var machine_recipe_label := "NEIIndex.index_machine_recipes total_recipes=%d machines=%d" % [
		_all_recipes.size(),
		_by_machine.size(),
	]
	_print_perf(machine_recipe_label, stage_started_usec)
	_built = true
	_print_perf("NEIIndex.rebuild total", total_started_usec)


func _print_perf(label: String, started_usec: int) -> void:
	print("[Perf] %s elapsed_ms=%.2f" % [
		label,
		float(Time.get_ticks_usec() - started_usec) / 1000.0,
	])


func get_all_item_ids() -> Array[int]:
	ensure_built()
	return _all_items.duplicate()


func search_item_ids(query: String) -> Array[int]:
	ensure_built()
	var tokens := _tokenize(query)
	if tokens.is_empty():
		return _all_items.duplicate()

	var results: Array[int] = []
	for item_id in _all_items:
		if _item_matches_all_tokens(item_id, tokens):
			results.append(item_id)
	return results


func get_recipes_for_output(item_id: int) -> Array[RecipeRef]:
	ensure_built()
	return (_by_output_item.get(item_id, []) as Array).duplicate()


func get_recipes_for_input(item_id: int) -> Array[RecipeRef]:
	ensure_built()
	return (_by_input_item.get(item_id, []) as Array).duplicate()


func get_recipes_for_output_fluid(fluid_name: String) -> Array[RecipeRef]:
	ensure_built()
	return (_by_output_fluid.get(_normalize(fluid_name), []) as Array).duplicate()


func get_recipes_for_input_fluid(fluid_name: String) -> Array[RecipeRef]:
	ensure_built()
	return (_by_input_fluid.get(_normalize(fluid_name), []) as Array).duplicate()


func get_recipes_for_machine(machine_type: String) -> Array[RecipeRef]:
	ensure_built()
	return (_by_machine.get(machine_type, []) as Array).duplicate()


func get_all_recipes() -> Array[RecipeRef]:
	ensure_built()
	return _all_recipes.duplicate()


func get_machine_types() -> PackedStringArray:
	ensure_built()
	var result := PackedStringArray()
	for key in _by_machine.keys():
		result.append(str(key))
	result.sort()
	return result


func get_related_recipes(item_id: int) -> Array[RecipeRef]:
	ensure_built()
	var result: Array[RecipeRef] = []
	var seen := {}
	for ref in get_recipes_for_output(item_id):
		_add_unique_recipe(result, seen, ref)
	for ref in get_recipes_for_input(item_id):
		_add_unique_recipe(result, seen, ref)
	return result


func get_item_key(item_id: int) -> String:
	ensure_built()
	return _item_keys.get(item_id, "")


func get_item_display_name(item_id: int) -> String:
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		return "Item #%d" % item_id
	return tr(def.title_key)


# Return all ore dictionary names for an item.
func get_ores_for_item(item_id: int) -> PackedStringArray:
	ensure_built()
	var result := PackedStringArray()
	for ore in _item_ores.get(item_id, []) as Array:
		result.append(str(ore))
	return result


# Return all item ids sharing an ore dictionary name.
func get_items_for_ore(ore_name: String) -> Array[int]:
	ensure_built()
	return (_ore_dict.get(ore_name, []) as Array).duplicate()


# Return all ore dictionary names (for subset dropdown).
func get_all_ore_names() -> PackedStringArray:
	ensure_built()
	var result := PackedStringArray()
	for key in _ore_dict.keys():
		result.append(str(key))
	result.sort()
	return result


# Return the source/mod tag for an item.
func get_item_source(item_id: int) -> String:
	ensure_built()
	return str(_item_sources.get(item_id, "vanilla"))


# Return all distinct source/mod tags.
func get_all_sources() -> PackedStringArray:
	ensure_built()
	var seen := {}
	var result := PackedStringArray()
	for item_id in _item_sources.keys():
		var src := str(_item_sources[item_id])
		if not seen.has(src):
			seen[src] = true
			result.append(src)
	result.sort()
	return result


# Return item ids in a category subset (ItemDef.Category).
func get_items_in_category(category: int) -> Array[int]:
	ensure_built()
	return (_by_category.get(category, []) as Array).duplicate()


# Return the cached tooltip text for an item (normalized, searchable).
func get_item_tooltip_text(item_id: int) -> String:
	ensure_built()
	return str(_item_tooltips.get(item_id, ""))


# Return the number of recipes that produce this item.
func get_recipe_count_for_output(item_id: int) -> int:
	ensure_built()
	return (_by_output_item.get(item_id, []) as Array).size()


# Return the number of recipes that consume this item.
func get_recipe_count_for_input(item_id: int) -> int:
	ensure_built()
	return (_by_input_item.get(item_id, []) as Array).size()


# Return a dictionary of item_id -> recipe/usage counts for batch tooltip display.
func get_recipe_counts_batch(item_ids: Array[int]) -> Dictionary:
	ensure_built()
	var result := {}
	for item_id in item_ids:
		result[item_id] = {
			"recipes": get_recipe_count_for_output(item_id),
			"usages": get_recipe_count_for_input(item_id),
		}
	return result


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
			# Category subset — mirrors NEI item subset dropdown.
			var cat: int = int(def.category)
			if not _by_category.has(cat):
				_by_category[cat] = []
			(_by_category[cat] as Array).append(item_id)
			# Tooltip text for #tooltip: search token.
			var tooltip_lines: Array = def.get_tooltip_lines()
			var tooltip_text := _normalize(" ".join(tooltip_lines))
			_item_tooltips[item_id] = tooltip_text
			# Source/mod tag — inferred from item key prefix.
			_item_sources[item_id] = _infer_item_source(item_id)
		var key := ""
		if ItemDatabase.has_method("get_item_key_by_id"):
			key = str(ItemDatabase.get_item_key_by_id(item_id))
		_item_keys[item_id] = _normalize(key)


# Infer the source/mod tag from the item key prefix.
# Items whose key starts with "gt_" are GregTech; others are vanilla.
func _infer_item_source(item_id: int) -> String:
	var key: String = str(_item_keys.get(item_id, ""))
	if key.begins_with("gt_") or key.begins_with("gregtech"):
		return "gregtech"
	if key.begins_with("ae2") or key.begins_with("appeng"):
		return "ae2"
	if key.begins_with("minecraft") or key.is_empty():
		return "vanilla"
	return "mod"


# Build the ore dictionary index from item keys.
# Ore dictionary lets different items share an ore name (e.g. oreCopper).
func _index_ore_dict() -> void:
	for item_id in _all_items:
		var key: String = str(_item_keys.get(item_id, ""))
		var ore_name := _derive_ore_name_from_key(key)
		if ore_name.is_empty():
			continue
		if not _ore_dict.has(ore_name):
			_ore_dict[ore_name] = []
		(_ore_dict[ore_name] as Array).append(item_id)
		if not _item_ores.has(item_id):
			_item_ores[item_id] = []
		(_item_ores[item_id] as Array).append(ore_name)


# Derive an ore dictionary name from an item key as a fallback.
func _derive_ore_name_from_key(key: String) -> String:
	if key.is_empty():
		return ""
	# Common material form patterns: ingotCopper, dustIron, plateGold, etc.
	for prefix in ["ingot", "dust", "tinydust", "nugget", "plate", "rod", "wire", "block", "gem", "crushed"]:
		if key.begins_with(prefix) and key.length() > prefix.length():
			return key
	return ""


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
		_add_to_index(_by_output_fluid, _normalize(str(stack.get("fluid_name", ""))), ref)
	for stack in ref.fluid_inputs:
		_add_to_index(_by_input_fluid, _normalize(str(stack.get("fluid_name", ""))), ref)


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
	if ClassDB.class_exists("GDCraftingManager") and ClassDB.class_has_method("GDCraftingManager", "get_item_id_by_key"):
		return int(GDCraftingManager.get_item_id_by_key(item_key))
	return -1


func _item_matches_all_tokens(item_id: int, tokens: PackedStringArray) -> bool:
	for token in tokens:
		if not _item_matches_token(item_id, token):
			return false
	return true


func _item_matches_token(item_id: int, token: String) -> bool:
	if token.begins_with("id:"):
		return str(item_id) == token.substr(3)
	if token.begins_with("key:"):
		return _item_keys.get(item_id, "").contains(token.substr(4))
	if token.begins_with("@"):
		return _item_has_related_machine(item_id, token.substr(1))
	if token.begins_with("machine:"):
		return _item_has_related_machine(item_id, token.substr(8))
	if token.begins_with("fluid:"):
		return _item_has_related_fluid(item_id, token.substr(6))
	if token.begins_with("tier:"):
		return _item_has_related_tier(item_id, token.substr(5))
	# Ore dictionary search: $ingotCopper matches any item in that ore entry.
	if token.begins_with("$"):
		return _item_has_ore(item_id, token.substr(1))
	if token.begins_with("ore:"):
		return _item_has_ore(item_id, token.substr(4))
	# Source/mod filter: %gregtech, %vanilla, %ae2.
	if token.begins_with("%"):
		return _item_sources.get(item_id, "vanilla") == token.substr(1)
	if token.begins_with("source:"):
		return _item_sources.get(item_id, "vanilla") == token.substr(7)
	if token.begins_with("mod:"):
		return _item_sources.get(item_id, "vanilla") == token.substr(4)
	# Tooltip text search: #tooltip_text.
	if token.begins_with("#"):
		return _item_tooltips.get(item_id, "").contains(token.substr(1))
	if token.begins_with("tooltip:"):
		return _item_tooltips.get(item_id, "").contains(token.substr(8))
	# Category subset: &materials, &tools, etc.
	if token.begins_with("&"):
		return _item_in_category_name(item_id, token.substr(1))
	if token.begins_with("category:"):
		return _item_in_category_name(item_id, token.substr(9))
	# Bookmarked items only.
	if token == "*bookmarked" or token == "*fav":
		return NEISettings != null and NEISettings.is_bookmarked(item_id)

	var name: String = _normalized_item_names.get(item_id, "")
	var key: String = _item_keys.get(item_id, "")
	if name.contains(token) or key.contains(token):
		return true
	return _item_has_related_text(item_id, token)


# Check if an item belongs to an ore dictionary entry.
func _item_has_ore(item_id: int, ore_name: String) -> bool:
	ore_name = _normalize(ore_name)
	if ore_name.is_empty():
		return not _item_ores.get(item_id, []).is_empty()
	for ore in _item_ores.get(item_id, []) as Array:
		if _normalize(str(ore)).contains(ore_name):
			return true
	return false


# Check if an item is in a category by name (e.g. "materials", "tools").
func _item_in_category_name(item_id: int, category_name: String) -> bool:
	category_name = _normalize(category_name)
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		return false
	var cat_name := _category_name(int(def.category))
	return _normalize(cat_name) == category_name or _normalize(cat_name).begins_with(category_name)


# Map an ItemDef.Category enum value to a lowercase name.
func _category_name(category: int) -> String:
	match category:
		ItemDef.Category.MATERIALS: return "materials"
		ItemDef.Category.TOOLS: return "tools"
		ItemDef.Category.COMPONENTS: return "components"
		ItemDef.Category.PLACEABLES: return "placeables"
		ItemDef.Category.RESOURCES: return "resources"
		ItemDef.Category.FOOD: return "food"
		ItemDef.Category.MISC: return "misc"
	return "misc"


func _item_has_related_machine(item_id: int, needle: String) -> bool:
	needle = _normalize(needle)
	if needle.is_empty():
		return true
	for ref in get_related_recipes(item_id):
		if _normalize(ref.machine_type).contains(needle) or _normalize(ref.recipe_type).contains(needle):
			return true
	return false


func _item_has_related_fluid(item_id: int, needle: String) -> bool:
	needle = _normalize(needle)
	if needle.is_empty():
		return true
	for ref in get_related_recipes(item_id):
		if _fluid_list_contains(ref.fluid_inputs, needle) or _fluid_list_contains(ref.fluid_outputs, needle):
			return true
	return false


func _item_has_related_tier(item_id: int, tier_text: String) -> bool:
	var tier := _tier_to_int(tier_text)
	for ref in get_related_recipes(item_id):
		if ref.min_tier == tier:
			return true
	return false


func _item_has_related_text(item_id: int, needle: String) -> bool:
	for ref in get_related_recipes(item_id):
		if _recipe_text(ref).contains(needle):
			return true
	return false


func _recipe_text(ref: RecipeRef) -> String:
	var parts := PackedStringArray()
	parts.append(ref.id)
	parts.append(ref.recipe_type)
	parts.append(ref.machine_type)
	parts.append(ref.display_name)
	for fluid in ref.fluid_inputs:
		parts.append(str(fluid.get("fluid_name", "")))
	for fluid in ref.fluid_outputs:
		parts.append(str(fluid.get("fluid_name", "")))
	for tool in ref.tools:
		parts.append(tool)
	return _normalize(" ".join(parts))


func _fluid_list_contains(list: Array[Dictionary], needle: String) -> bool:
	for fluid in list:
		if _normalize(str(fluid.get("fluid_name", ""))).contains(needle):
			return true
	return false


func _add_unique_recipe(result: Array[RecipeRef], seen: Dictionary, ref: RecipeRef) -> void:
	var key := "%s|%s|%s" % [ref.recipe_type, ref.machine_type, ref.id]
	if seen.has(key):
		return
	seen[key] = true
	result.append(ref)


func _tier_to_int(text: String) -> int:
	match _normalize(text):
		"ulv": return 0
		"lv": return 1
		"mv": return 2
		"hv": return 3
		"ev": return 4
		"iv": return 5
		"luv": return 6
		"zpm": return 7
		"uv": return 8
	return int(text)


func _tokenize(query: String) -> PackedStringArray:
	var tokens := PackedStringArray()
	for raw in query.split(" ", false):
		var token := _normalize(raw)
		if not token.is_empty():
			tokens.append(token)
	return tokens


func _normalize(text: String) -> String:
	return text.strip_edges().to_lower()
