# ============================================================
# ModRegistrar — Pack-scoped content registration facade.
# ============================================================
# Passed to each pack's `register_content(registrar)` hook. It wraps
# the existing C++ binding APIs (GDCraftingManager, GDRecipeDatabase)
# and tracks which names this pack has registered, so collisions
# across packs are reported with clear attribution.
#
# Conflict policy (default): recipe names are globally unique. If a
# name was already registered by an earlier pack (or builtin), the
# later registration is skipped with a warning, and the conflict is
# recorded in this registrar's report. The C++ registries also
# enforce uniqueness, but catching it here gives pack-attributed
# diagnostics.
#
# Pack authors should prefix recipe names with their mod_id
# (e.g. "my_mod:craft_widget") to avoid collisions.
class_name ModRegistrar
extends RefCounted

# ------------------------------------------------------------
# State
# ------------------------------------------------------------

# The manifest of the pack this registrar serves.
var manifest: ModManifest = null

# Reference to the ModLoader (for shared name tracking). Set by loader.
var _loader: Node = null

# Per-pack counters.
var crafting_registered: int = 0
var crafting_skipped: int = 0
var processing_registered: int = 0
var processing_skipped: int = 0

# Per-pack issue messages (conflicts, invalid items, etc.).
var issues: PackedStringArray = PackedStringArray()

# Per-pack counters for v2 content types.
var items_registered: int = 0
var fluids_registered: int = 0
var fuels_registered: int = 0
var loot_tables_registered: int = 0
var machine_types_registered: int = 0
var block_entity_types_registered: int = 0
var commands_registered: int = 0
var ui_screens_registered: int = 0
var event_subscriptions: int = 0

# Reference to the ModEventBus (set by loader).
var _event_bus: ModEventBus = null

# Reference to the GDGameCommandServer (set by loader).
var _command_server: Node = null

# ------------------------------------------------------------
# Public API — crafting recipes
# ------------------------------------------------------------

# Register a single crafting recipe. See GDCraftingManager.register_recipe
# for the dictionary schema. Returns true on success.
func add_crafting_recipe(recipe: Dictionary) -> bool:
	var name: String = String(recipe.get("name", ""))
	if name.is_empty():
		_record_issue("crafting recipe missing 'name'")
		return false
	if _loader != null and _loader.is_recipe_name_taken(name, manifest.mod_id):
		_record_issue("crafting recipe '%s' conflicts with an earlier pack; skipped" % name)
		crafting_skipped += 1
		return false
	if GDCraftingManager.register_recipe(recipe):
		crafting_registered += 1
		if _loader != null:
			_loader.mark_recipe_name(name, manifest.mod_id)
		return true
	# C++ side rejected it (invalid items, dup, etc.).
	_record_issue("crafting recipe '%s' rejected by GDCraftingManager" % name)
	crafting_skipped += 1
	return false

# Register multiple crafting recipes. Returns the count registered.
func add_crafting_recipes(recipes: Array) -> int:
	var count: int = 0
	for recipe in recipes:
		if add_crafting_recipe(recipe):
			count += 1
	return count

# ------------------------------------------------------------
# Public API — processing recipes
# ------------------------------------------------------------

# Register a single processing (machine) recipe. See
# GDRecipeDatabase.register_recipe for the dictionary schema.
# Returns true on success.
func add_processing_recipe(recipe: Dictionary) -> bool:
	var name: String = String(recipe.get("name", ""))
	if name.is_empty():
		_record_issue("processing recipe missing 'name'")
		return false
	# Processing recipes are namespaced by machine_type + name.
	var machine_type: String = String(recipe.get("machine_type", ""))
	var full_key := "%s@%s" % [machine_type, name]
	if _loader != null and _loader.is_recipe_name_taken(full_key, manifest.mod_id):
		_record_issue("processing recipe '%s' conflicts with an earlier pack; skipped" % full_key)
		processing_skipped += 1
		return false
	if GDRecipeDatabase.register_recipe(recipe):
		processing_registered += 1
		if _loader != null:
			_loader.mark_recipe_name(full_key, manifest.mod_id)
		return true
	_record_issue("processing recipe '%s' rejected by GDRecipeDatabase" % full_key)
	processing_skipped += 1
	return false

# Register multiple processing recipes. Returns the count registered.
func add_processing_recipes(recipes: Array) -> int:
	var count: int = 0
	for recipe in recipes:
		if add_processing_recipe(recipe):
			count += 1
	return count

# ------------------------------------------------------------
# Public API — items (mod-registered)
# ------------------------------------------------------------

# Register a new non-material item. Returns the assigned item_id, or 0 on failure.
# Dictionary fields:
#   item_key (String, required): globally unique stable key.
#   title_key (String, optional): localization key.
func add_item(def: Dictionary) -> int:
	var item_key: String = String(def.get("item_key", ""))
	if item_key.is_empty():
		_record_issue("add_item: missing 'item_key'")
		return 0
	var item_id: int = int(GDItemRegistry.register_item(def))
	if item_id > 0:
		items_registered += 1
		if _loader != null:
			_loader.mark_item_key(item_key, manifest.mod_id)
	else:
		_record_issue("add_item: '%s' rejected by GDItemRegistry (duplicate?)" % item_key)
	return item_id

# ------------------------------------------------------------
# Public API — fluids
# ------------------------------------------------------------

# Register a new fluid. Returns the assigned fluid_id, or -1 on failure.
# Dictionary fields:
#   name (String, required): stable fluid key.
#   title_key (String, optional): localization key.
#   chemical_formula (String, optional).
#   temperature (int, optional, default 300).
#   is_gas (bool, optional, default false).
func add_fluid(def: Dictionary) -> int:
	var fluid_name: String = String(def.get("name", ""))
	if fluid_name.is_empty():
		_record_issue("add_fluid: missing 'name'")
		return -1
	var fluid_id: int = int(GDFluidRegistry.register_fluid(def))
	if fluid_id >= 0:
		fluids_registered += 1
	else:
		_record_issue("add_fluid: '%s' rejected by GDFluidRegistry" % fluid_name)
	return fluid_id

# ------------------------------------------------------------
# Public API — fuels
# ------------------------------------------------------------

# Register an item as fuel. Returns true on success.
# Dictionary fields:
#   name (String, required): stable fuel key.
#   item_id (int, required): the item id.
#   burn_ticks (int, required): burn duration in ticks (20 TPS).
#   title_key (String, optional).
#   category (int, optional, default 0=SOLID).
func add_fuel(def: Dictionary) -> bool:
	if not GDFuelRegistry.register_fuel(def):
		_record_issue("add_fuel: rejected by GDFuelRegistry")
		return false
	fuels_registered += 1
	return true

# ------------------------------------------------------------
# Public API — loot tables
# ------------------------------------------------------------

# Register a loot table. Returns true on success.
#   table_key (String, required): globally unique key.
#   entries (Array[Dictionary], required): weighted drop entries.
#     Each entry: {item_key, weight, min_count, max_count}
func add_loot_table(table_key: String, entries: Array) -> bool:
	if table_key.is_empty():
		_record_issue("add_loot_table: missing 'table_key'")
		return false
	if not GDLootTableRegistry.register_table(table_key, entries):
		_record_issue("add_loot_table: '%s' rejected" % table_key)
		return false
	loot_tables_registered += 1
	return true

# ------------------------------------------------------------
# Public API — world generation (delegates to a shared
# GDTerrainContentRegistry instance owned by ModLoader)
# ------------------------------------------------------------

# Obtain the shared terrain registry from the loader (lazy-created).
# Returns null if the C++ class is not registered or loader is unset.
func _terrain_registry() -> GDTerrainContentRegistry:
	if _loader == null:
		return null
	return _loader.get_terrain_registry()

# Register a terrain material. Returns true on success.
func add_material(def: Dictionary) -> bool:
	var registry := _terrain_registry()
	if registry == null or not registry.register_material(def):
		_record_issue("add_material: rejected by GDTerrainContentRegistry")
		return false
	return true

# Register a tree species. Returns true on success.
func add_tree_species(def: Dictionary) -> bool:
	var registry := _terrain_registry()
	if registry == null or not registry.register_tree_species(def):
		_record_issue("add_tree_species: rejected")
		return false
	return true

# Register a crop species. Returns true on success.
func add_crop_species(def: Dictionary) -> bool:
	var registry := _terrain_registry()
	if registry == null or not registry.register_crop_species(def):
		_record_issue("add_crop_species: rejected")
		return false
	return true

# Register an ore vein group. Returns true on success.
func add_ore_vein(def: Dictionary) -> bool:
	var registry := _terrain_registry()
	if registry == null or not registry.register_ore_vein_group(def):
		_record_issue("add_ore_vein: rejected")
		return false
	return true

# Register a biome rule. Returns true on success.
func add_biome_rule(def: Dictionary) -> bool:
	var registry := _terrain_registry()
	if registry == null or not registry.register_biome_rule(def):
		_record_issue("add_biome_rule: rejected")
		return false
	return true

# ------------------------------------------------------------
# Public API — machine types
# ------------------------------------------------------------

# Register a new machine type definition. Returns true on success.
# The actual processing logic is provided by a GDScript callback.
# Dictionary fields: see GDMachineDefinitionRegistry.register_definition.
func add_machine_type(def: Dictionary) -> bool:
	if not GDMachineDefinitionRegistry.register_definition(def):
		_record_issue("add_machine_type: rejected by GDMachineDefinitionRegistry")
		return false
	machine_types_registered += 1
	return true

# ------------------------------------------------------------
# Public API — custom block entities
# ------------------------------------------------------------

# Register a custom block entity type. Returns true on success.
#   type_key (String, required): globally unique key.
#   tick_callback (Callable, required): called every tick.
#   serialize_callback (Callable, required): state -> JSON string.
#   deserialize_callback (Callable, required): JSON string -> state.
func add_block_entity_type(type_key: String, tick_callback: Callable,
		serialize_callback: Callable, deserialize_callback: Callable) -> bool:
	if not GDCustomBlockEntityRegistry.register_type(type_key,
			tick_callback, serialize_callback, deserialize_callback):
		_record_issue("add_block_entity_type: '%s' rejected (duplicate?)" % type_key)
		return false
	block_entity_types_registered += 1
	return true

# ------------------------------------------------------------
# Public API — event subscription
# ------------------------------------------------------------

# Subscribe to a simulation event. Returns true on success.
#   event_name (String): a GDTickSystem signal name.
#   callback (Callable): handler matching the signal's arguments.
func subscribe_event(event_name: String, callback: Callable) -> bool:
	if _event_bus == null:
		_record_issue("subscribe_event: no event bus available")
		return false
	if not _event_bus.subscribe(manifest.mod_id, event_name, callback):
		_record_issue("subscribe_event: '%s' rejected" % event_name)
		return false
	event_subscriptions += 1
	return true

# ------------------------------------------------------------
# Public API — custom commands
# ------------------------------------------------------------

# Register a custom game command. Returns true on success.
#   command_name (String): unique command name.
#   callback (Callable): receives command Dictionary, returns result Dictionary.
func register_command(command_name: String, callback: Callable) -> bool:
	if _command_server == null:
		_record_issue("register_command: no command server available")
		return false
	if not _command_server.register_command(command_name, callback):
		_record_issue("register_command: '%s' rejected (duplicate?)" % command_name)
		return false
	commands_registered += 1
	return true

# ------------------------------------------------------------
# Public API — UI screens
# ------------------------------------------------------------

# Register a UI screen. Returns true on success.
#   screen_id (String): unique screen identifier.
#   packed_scene (PackedScene): the scene to instantiate.
# UI screens are stored in ModLoader's global registry and opened
# via ModLoader.open_ui_screen(screen_id).
func register_ui_screen(screen_id: String, packed_scene: PackedScene) -> bool:
	if screen_id.is_empty() or packed_scene == null:
		_record_issue("register_ui_screen: invalid arguments")
		return false
	if _loader != null and _loader.register_ui_screen(screen_id, packed_scene, manifest.mod_id):
		ui_screens_registered += 1
		return true
	_record_issue("register_ui_screen: '%s' rejected (duplicate?)" % screen_id)
	return false

# ------------------------------------------------------------
# Public API — item helpers
# ------------------------------------------------------------

# Resolve an item_key (e.g. "ingot.iron") to its item_id. Returns -1 if
# the item is not registered. Pack authors should prefer item_key over
# numeric item_id for stability across versions.
func resolve_item_id(item_key: String) -> int:
	return int(GDCraftingManager.get_item_id_by_key(item_key))

# Return true if the given item_key refers to a registered item.
func has_item(item_key: String) -> bool:
	return resolve_item_id(item_key) > 0

# ------------------------------------------------------------
# Public API — reporting
# ------------------------------------------------------------

# Return true if this pack had any registration issues.
func has_issues() -> bool:
	return not issues.is_empty()

# Return a summary dictionary for diagnostics.
func summary() -> Dictionary:
	return {
		"mod_id": manifest.mod_id if manifest != null else "",
		"crafting_registered": crafting_registered,
		"crafting_skipped": crafting_skipped,
		"processing_registered": processing_registered,
		"processing_skipped": processing_skipped,
		"items_registered": items_registered,
		"fluids_registered": fluids_registered,
		"fuels_registered": fuels_registered,
		"loot_tables_registered": loot_tables_registered,
		"machine_types_registered": machine_types_registered,
		"block_entity_types_registered": block_entity_types_registered,
		"commands_registered": commands_registered,
		"ui_screens_registered": ui_screens_registered,
		"event_subscriptions": event_subscriptions,
		"issues": Array(issues),
	}

# ------------------------------------------------------------
# Internal
# ------------------------------------------------------------

func _record_issue(message: String) -> void:
	issues.append(message)
	if manifest != null:
		push_warning("[Mod '%s'] %s" % [manifest.mod_id, message])
	else:
		push_warning("[Mod] %s" % message)
