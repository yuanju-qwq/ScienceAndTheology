# ============================================================
# ModLoader — Autoload that discovers, orders, and loads content packs.
# ============================================================
# ModLoader is the runtime entry point for the mod/content pack system.
# It runs after the builtin content autoloads (ItemDatabase,
# ContentDatabase) so that packs can reference builtin items, and
# before index-building autoloads (NEIIndex) so that mod content is
# included in indices.
#
# Pipeline:
#   1. Seed the global recipe-name registry from builtin content.
#   2. Discover packs from all configured PackSources.
#   3. Deduplicate by mod_id (first source wins; later dups warned).
#   4. Resolve load order via topological sort over dependencies,
#      load_after, and load_before edges. Detect cycles and missing
#      dependencies.
#   5. For each pack in order: instantiate its entry script, build a
#      ModRegistrar, and call register_content(registrar).
#   6. After all packs: call on_all_packs_loaded(loader) on each.
#   7. Aggregate and print a load report.
#
# Pack sources default to LocalPackSource(user://mods/) and a
# WorkshopPackSource (configured via set_workshop_dir). Steam API
# integration is provided by subclassing WorkshopPackSource and
# overriding _collect_item_paths().
extends Node

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

# When true, packs with missing dependencies are skipped (with a
# warning). When false, they are loaded anyway (best-effort).
var skip_on_missing_dependency: bool = true

# When true, a dependency cycle causes all packs in the cycle to be
# skipped. When false, the cycle is broken arbitrarily (not recommended).
var skip_on_cycle: bool = true

# When true, the loader resolves load order and validates manifests
# but skips actual content registration (no calls to GDCraftingManager
# / GDRecipeDatabase). Used by tests to verify ordering logic without
# polluting global C++ registries.
var dry_run: bool = false

# ------------------------------------------------------------
# Runtime state
# ------------------------------------------------------------

# Configured pack sources, in priority order (first wins on dup mod_id).
var _sources: Array[ModPackSource] = []

# Loaded manifests keyed by mod_id.
var _manifests_by_id: Dictionary = {}

# Loaded entry points keyed by mod_id (for on_all_packs_loaded).
var _entry_points: Dictionary = {}

# Per-pack registrar summaries (Dictionary per mod_id).
var _pack_reports: Dictionary = {}

# Global recipe-name ownership: name -> mod_id (or "builtin").
var _recipe_name_owners: Dictionary = {}

# True once load_mods() has run.
var _loaded: bool = false

# Aggregate load issues (source-level, not per-pack).
var _load_issues: PackedStringArray = PackedStringArray()

# ModEventBus for dispatching simulation events to packs.
var _event_bus: ModEventBus = ModEventBus.new()

# UI screen registry: screen_id -> {scene, mod_id}
var _ui_screens: Dictionary = {}

# Shared GDTerrainContentRegistry instance for mod terrain content.
# Lazy-created on first use. Note: builtin world gen builds its own
# registry instance in BuiltinTerrainContent; integrating mod content
# into the builtin config flow is a follow-up task.
var _terrain_registry: GDTerrainContentRegistry = null

# Item key ownership: item_key -> mod_id (for conflict detection).
var _item_key_owners: Dictionary = {}

# ------------------------------------------------------------
# Lifecycle
# ------------------------------------------------------------

func _ready() -> void:
	# Default sources: local mods folder + workshop (configured later).
	if _sources.is_empty():
		var local := LocalPackSource.new()
		local.scan_dir = "user://mods/"
		local.readonly_dir = "res://mods/"
		_sources.append(local)
		var workshop := WorkshopPackSource.new()
		_sources.append(workshop)
	# Install any pending ZIPs from user://mods/incoming/ before
	# discovery so they are picked up on the first load.
	scan_incoming_zips()
	# The autoload instance loads real packs; test instances set
	# dry_run = true before calling load_mods().
	load_mods()

# ------------------------------------------------------------
# Public configuration API
# ------------------------------------------------------------

# Replace all pack sources. Useful for tests or custom setups.
func set_sources(sources: Array[ModPackSource]) -> void:
	_sources = sources

# Add a custom pack source.
func add_source(source: ModPackSource) -> void:
	_sources.append(source)

# Configure the workshop directory for the default WorkshopPackSource.
# Call this before load_mods() (e.g. from a Steam integration bootstrap).
func set_workshop_dir(path: String, app_id: int = 0) -> void:
	for source in _sources:
		if source is WorkshopPackSource:
			(source as WorkshopPackSource).workshop_dir = path
			(source as WorkshopPackSource).steam_app_id = app_id
			return

# ------------------------------------------------------------
# Public query API (used by ModRegistrar)
# ------------------------------------------------------------

# Return true if `name` is already claimed by a pack other than `owner_id`.
# Builtin names are owned by "builtin".
func is_recipe_name_taken(name: String, owner_id: String) -> bool:
	var current: String = _recipe_name_owners.get(name, "")
	return not current.is_empty() and current != owner_id

# Claim a recipe name for `owner_id`. No-op if already owned by the
# same owner.
func mark_recipe_name(name: String, owner_id: String) -> void:
	if not _recipe_name_owners.has(name):
		_recipe_name_owners[name] = owner_id

# Return the list of loaded mod_ids in load order.
func get_loaded_mod_ids() -> PackedStringArray:
	var ids: PackedStringArray = PackedStringArray()
	for manifest in _ordered_manifests:
		ids.append(manifest.mod_id)
	return ids

# Return the manifest for a loaded mod, or null.
func get_manifest(mod_id: String) -> ModManifest:
	return _manifests_by_id.get(mod_id)

# Return the aggregate load report (per-pack summaries + source issues).
func get_load_report() -> Dictionary:
	var packs: Array = []
	for mod_id in _ordered_manifests_ids:
		packs.append(_pack_reports.get(mod_id, {}))
	return {
		"loaded_mod_ids": Array(get_loaded_mod_ids()),
		"packs": packs,
		"issues": Array(_load_issues),
	}

# ------------------------------------------------------------
# Main pipeline
# ------------------------------------------------------------

# Run the full discovery + load pipeline. Idempotent.
func load_mods() -> void:
	if _loaded:
		return
	_loaded = true
	if not dry_run:
		_seed_builtin_recipe_names()
	var manifests := _discover_and_dedup()
	_ordered_manifests = _resolve_load_order(manifests)
	_ordered_manifests_ids.clear()
	for m in _ordered_manifests:
		_ordered_manifests_ids.append(m.mod_id)
	if not dry_run:
		_register_packs()
		_notify_all_loaded()
	_print_report()

# Ordered manifests (filled by load_mods). Kept as members so tests
# can inspect order without re-running.
var _ordered_manifests: Array[ModManifest] = []
var _ordered_manifests_ids: PackedStringArray = PackedStringArray()

# ------------------------------------------------------------
# Stage 1: seed builtin recipe names
# ------------------------------------------------------------

func _seed_builtin_recipe_names() -> void:
	# Crafting recipes.
	for recipe in GDCraftingManager.get_all_recipes():
		var name: String = String(recipe.get("name", ""))
		if not name.is_empty():
			mark_recipe_name(name, "builtin")
	# Processing recipes (namespaced by machine_type@name).
	for machine_type in GDRecipeDatabase.get_machine_types():
		for recipe in GDRecipeDatabase.get_recipes_for_machine(machine_type):
			var name: String = String(recipe.get("name", ""))
			if not name.is_empty():
				mark_recipe_name("%s@%s" % [machine_type, name], "builtin")

# ------------------------------------------------------------
# Stage 2: discover + dedup
# ------------------------------------------------------------

func _discover_and_dedup() -> Array[ModManifest]:
	var result: Array[ModManifest] = []
	var seen: Dictionary = {}
	for source in _sources:
		var packs := source.discover_packs()
		for manifest in packs:
			if seen.has(manifest.mod_id):
				_load_issues.append("duplicate mod_id '%s' from source '%s' ignored (first seen in '%s')" %
						[manifest.mod_id, source.source_name, seen[manifest.mod_id]])
				continue
			seen[manifest.mod_id] = source.source_name
			_manifests_by_id[manifest.mod_id] = manifest
			result.append(manifest)
	return result

# ------------------------------------------------------------
# Stage 3: topological sort (Kahn's algorithm)
# ------------------------------------------------------------

func _resolve_load_order(manifests: Array[ModManifest]) -> Array[ModManifest]:
	# Build adjacency: edge u -> v means u must load before v.
	# Edges come from: v depends on u, v load_after u, u load_before v.
	var ids: Dictionary = {}  # mod_id -> manifest
	for m in manifests:
		ids[m.mod_id] = m

	var edges: Dictionary = {}  # mod_id -> Array[mod_id] (successors)
	var indegree: Dictionary = {}  # mod_id -> int
	for m in manifests:
		edges[m.mod_id] = []
		indegree[m.mod_id] = 0

	var add_edge := func(from_id: String, to_id: String) -> void:
		if from_id == to_id:
			return
		if not ids.has(from_id) or not ids.has(to_id):
			return
		# Avoid duplicate edges inflating indegree.
		for existing in edges[from_id]:
			if existing == to_id:
				return
		edges[from_id].append(to_id)
		indegree[to_id] = int(indegree[to_id]) + 1

	for m in manifests:
		# Dependencies + load_after: those must come before m.
		for dep_entry in m.dependency_entries:
			var dep_id := ModManifest._split_dep_id(dep_entry)
			add_edge.call(dep_id, m.mod_id)
		for after_id in m.load_after:
			add_edge.call(after_id, m.mod_id)
		# load_before: m must come before these.
		for before_id in m.load_before:
			add_edge.call(m.mod_id, before_id)

	# Check missing dependencies and version constraints.
	var skipped_missing: Dictionary = {}
	for m in manifests:
		for dep_entry in m.dependency_entries:
			var dep_id := ModManifest._split_dep_id(dep_entry)
			if not ids.has(dep_id):
				if skip_on_missing_dependency:
					skipped_missing[m.mod_id] = true
					_load_issues.append("mod '%s' skipped: missing dependency '%s'" %
							[m.mod_id, dep_entry])
			else:
				var dep_manifest: ModManifest = ids[dep_id]
				if not ModManifest.satisfies(dep_entry, dep_manifest.version):
					skipped_missing[m.mod_id] = true
					_load_issues.append("mod '%s' skipped: dependency '%s' version %s does not satisfy %s" %
							[m.mod_id, dep_id, dep_manifest.version, dep_entry])

	# Kahn's algorithm with stable ordering (process in discovery order).
	var queue: Array[String] = []
	for m in manifests:
		if skipped_missing.has(m.mod_id):
			continue
		if int(indegree[m.mod_id]) == 0:
			queue.append(m.mod_id)

	var ordered: Array[ModManifest] = []
	var processed: Dictionary = {}
	while not queue.is_empty():
		# Pop the first (stable order).
		var current: String = queue.pop_front()
		if processed.has(current) or skipped_missing.has(current):
			continue
		processed[current] = true
		ordered.append(ids[current])
		for successor in edges[current]:
			if skipped_missing.has(successor):
				continue
			indegree[successor] = int(indegree[successor]) - 1
			if int(indegree[successor]) == 0 and not processed.has(successor):
				queue.append(successor)

	# Detect cycles: any non-skipped mod not processed is in a cycle.
	if ordered.size() < (manifests.size() - skipped_missing.size()):
		for m in manifests:
			if not processed.has(m.mod_id) and not skipped_missing.has(m.mod_id):
				_load_issues.append("mod '%s' skipped: dependency cycle detected" % m.mod_id)
				if skip_on_cycle:
					continue
				ordered.append(m)

	return ordered

# ------------------------------------------------------------
# Stage 4: register each pack
# ------------------------------------------------------------

func _register_packs() -> void:
	for manifest in _ordered_manifests:
		var entry := _instantiate_entry(manifest)
		if entry == null:
			_pack_reports[manifest.mod_id] = {
				"mod_id": manifest.mod_id,
				"error": "failed to load entry script",
			}
			continue
		entry.manifest = manifest
		_entry_points[manifest.mod_id] = entry
		var registrar := ModRegistrar.new()
		registrar.manifest = manifest
		registrar._loader = self
		registrar._event_bus = _event_bus
		registrar._command_server = _find_command_server()
		# Crash guard: if register_content crashes, the mod is disabled
		# but other packs still load.
		if not ModCrashGuard.safe_register_content(entry, registrar, manifest.mod_id):
			_pack_reports[manifest.mod_id] = {
				"mod_id": manifest.mod_id,
				"error": "disabled during register_content (crash)",
			}
			continue
		_pack_reports[manifest.mod_id] = registrar.summary()

func _instantiate_entry(manifest: ModManifest) -> ModEntryPoint:
	var script_path := manifest.pack_path + "/" + manifest.entry_script
	if not ResourceLoader.exists(script_path):
		_load_issues.append("mod '%s' entry script not found: %s" %
				[manifest.mod_id, script_path])
		return null
	var script := load(script_path)
	if script == null:
		_load_issues.append("mod '%s' failed to load entry script: %s" %
				[manifest.mod_id, script_path])
		return null
	var instance = script.new()
	if not (instance is ModEntryPoint):
		_load_issues.append("mod '%s' entry script does not extend ModEntryPoint: %s" %
				[manifest.mod_id, script_path])
		return null
	return instance as ModEntryPoint

# ------------------------------------------------------------
# Stage 5: post-load notification
# ------------------------------------------------------------

func _notify_all_loaded() -> void:
	for mod_id in _ordered_manifests_ids:
		var entry: ModEntryPoint = _entry_points.get(mod_id)
		if entry == null:
			continue
		# Crash guard for the optional on_all_packs_loaded hook.
		ModCrashGuard.safe_call_lifecycle(entry, mod_id, "on_all_packs_loaded", self)

# ------------------------------------------------------------
# Stage 6: report
# ------------------------------------------------------------

func _print_report() -> void:
	var loaded_count := _ordered_manifests.size()
	var issue_count := _load_issues.size()
	var crafting_total := 0
	var processing_total := 0
	var items_total := 0
	var fluids_total := 0
	var machine_types_total := 0
	var block_entity_types_total := 0
	var disabled_count := 0
	for mod_id in _ordered_manifests_ids:
		var report: Dictionary = _pack_reports.get(mod_id, {})
		crafting_total += int(report.get("crafting_registered", 0))
		processing_total += int(report.get("processing_registered", 0))
		items_total += int(report.get("items_registered", 0))
		fluids_total += int(report.get("fluids_registered", 0))
		machine_types_total += int(report.get("machine_types_registered", 0))
		block_entity_types_total += int(report.get("block_entity_types_registered", 0))
		if ModCrashGuard.is_disabled(mod_id):
			disabled_count += 1
	print("ModLoader: loaded %d pack(s) (%d disabled), crafting +%d, processing +%d, items +%d, fluids +%d, machine_types +%d, block_entity_types +%d, issues %d" %
			[loaded_count, disabled_count, crafting_total, processing_total,
			items_total, fluids_total, machine_types_total,
			block_entity_types_total, issue_count])
	for issue in _load_issues:
		push_warning("[ModLoader] " + issue)

# ============================================================
# v2 extensions: event bus, UI screens, item key tracking,
# incoming ZIP auto-scan, unknown block placeholder
# ============================================================

# Bind the event bus to a GDTickSystem node. Called after the tick
# system becomes available (e.g. in _ready or on world load).
func bind_event_bus(tick_system: Node) -> void:
	_event_bus.bind_tick_system(tick_system)

# Find the GDGameCommandServer autoload. Returns null if not available.
func _find_command_server() -> Node:
	var tree := get_tree()
	if tree == null:
		return null
	return tree.get_first_node_in_group("game_command_server")

# Register a UI screen. Called by ModRegistrar.register_ui_screen.
# Rejects empty ids, null scenes, and duplicates.
func register_ui_screen(screen_id: String, packed_scene: PackedScene,
		mod_id: String) -> bool:
	if screen_id.is_empty() or packed_scene == null:
		return false
	if _ui_screens.has(screen_id):
		return false
	_ui_screens[screen_id] = {"scene": packed_scene, "mod_id": mod_id}
	return true

# Open a registered UI screen. Returns the instantiated Control or null.
func open_ui_screen(screen_id: String) -> Control:
	var entry: Dictionary = _ui_screens.get(screen_id, {})
	if entry.is_empty():
		return null
	var scene: PackedScene = entry.scene
	if scene == null:
		return null
	return scene.instantiate() as Control

# Mark an item key as owned by a mod. Used for conflict detection.
func mark_item_key(item_key: String, mod_id: String) -> void:
	if not _item_key_owners.has(item_key):
		_item_key_owners[item_key] = mod_id

# Return true if the item key is already claimed by another mod.
func is_item_key_taken(item_key: String, owner_id: String) -> bool:
	var current: String = _item_key_owners.get(item_key, "")
	return not current.is_empty() and current != owner_id

# Return the shared GDTerrainContentRegistry instance, creating it
# lazily via ClassDB. Returns null if the class is not registered.
func get_terrain_registry() -> GDTerrainContentRegistry:
	if _terrain_registry != null and is_instance_valid(_terrain_registry):
		return _terrain_registry
	if not ClassDB.class_exists("GDTerrainContentRegistry"):
		return null
	_terrain_registry = ClassDB.instantiate("GDTerrainContentRegistry")
	return _terrain_registry

# ------------------------------------------------------------
# World lifecycle dispatch (crash-guarded)
# ------------------------------------------------------------

# Notify all packs that a world was loaded. Each pack's
# on_world_loaded(registrar) is called (if present), wrapped in the
# crash guard. Returns the number of packs successfully notified.
func notify_world_loaded() -> int:
	var notified := 0
	for mod_id in _ordered_manifests_ids:
		var entry: ModEntryPoint = _entry_points.get(mod_id)
		if entry == null:
			continue
		var registrar := ModRegistrar.new()
		registrar.manifest = get_manifest(mod_id)
		registrar._loader = self
		registrar._event_bus = _event_bus
		if ModCrashGuard.safe_call_lifecycle(entry, mod_id, "on_world_loaded", registrar):
			notified += 1
	return notified

# Notify all packs that a world is about to be saved. Each pack's
# on_world_saving(registrar) is called (if present).
func notify_world_saving() -> int:
	var notified := 0
	for mod_id in _ordered_manifests_ids:
		var entry: ModEntryPoint = _entry_points.get(mod_id)
		if entry == null:
			continue
		var registrar := ModRegistrar.new()
		registrar.manifest = get_manifest(mod_id)
		registrar._loader = self
		if ModCrashGuard.safe_call_lifecycle(entry, mod_id, "on_world_saving", registrar):
			notified += 1
	return notified

# Unload a single mod: call on_mod_unloading, remove its event
# subscriptions, and drop its entry point. Returns true if the mod
# was loaded and is now unloaded.
func unload_mod(mod_id: String) -> bool:
	var entry: ModEntryPoint = _entry_points.get(mod_id)
	if entry == null:
		return false
	var registrar := ModRegistrar.new()
	registrar.manifest = get_manifest(mod_id)
	registrar._loader = self
	ModCrashGuard.safe_call_lifecycle(entry, mod_id, "on_mod_unloading", registrar)
	_event_bus.unsubscribe_all(mod_id)
	_entry_points.erase(mod_id)
	return true

# Unload all mods (e.g. before returning to main menu).
func unload_all_mods() -> void:
	for mod_id in _ordered_manifests_ids.duplicate():
		unload_mod(mod_id)
	_entry_points.clear()
	_ui_screens.clear()
	_unknown_block_placeholders.clear()

# ------------------------------------------------------------
# Incoming ZIP auto-scan
# ------------------------------------------------------------

# Scan user://mods/incoming/ for .zip files and install each one.
# Successfully installed packs are removed from incoming/; failed
# ones are left for manual inspection. Returns the list of installed
# mod_ids.
func scan_incoming_zips() -> PackedStringArray:
	var installed: PackedStringArray = PackedStringArray()
	var incoming_dir := "user://mods/incoming/"
	var dir := DirAccess.open(incoming_dir)
	if dir == null:
		return installed
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while not file_name.is_empty():
		if file_name.ends_with(".zip") and not dir.current_is_dir():
			var zip_path := incoming_dir + file_name
			var mod_id := install_pack_zip(zip_path)
			if not mod_id.is_empty():
				installed.append(mod_id)
				# Remove the installed zip.
				dir.remove(file_name)
			else:
				_load_issues.append("scan_incoming_zips: failed to install %s" % file_name)
		file_name = dir.get_next()
	dir.list_dir_end()
	return installed

# ------------------------------------------------------------
# Unknown block placeholder (save compatibility)
# ------------------------------------------------------------

# Map: missing_type_key -> placeholder_data. When a custom block
# entity's type_key is not registered (mod unloaded), the entity is
# preserved as an "unknown block" with opaque state. This prevents
# save corruption when mods are added or removed.
var _unknown_block_placeholders: Dictionary = {}

# Record an unknown block entity encountered during load. Called by
# the world loader when a CUSTOM entity's type_key has no handler.
func record_unknown_block(type_key: String, state_json: String,
		pos: Vector3i) -> void:
	var key := "%s@%d,%d,%d" % [type_key, pos.x, pos.y, pos.z]
	_unknown_block_placeholders[key] = {
		"type_key": type_key,
		"state_json": state_json,
		"position": pos,
	}

# Returns true if there are any unknown block placeholders.
func has_unknown_blocks() -> bool:
	return not _unknown_block_placeholders.is_empty()

# Returns all unknown block placeholders as an Array of Dictionaries.
func get_unknown_blocks() -> Array:
	return _unknown_block_placeholders.values()

# ============================================================
# ZIP install utility
# ============================================================

# Extract a content pack ZIP into user://mods/<mod_id>/ so it can be
# discovered by LocalPackSource on the next load. The mod_id is read
# from the manifest inside the ZIP; if the manifest is missing or
# invalid, the extraction is aborted and the partial folder removed.
# Returns the installed mod_id on success, or "" on failure.
func install_pack_zip(zip_path: String) -> String:
	var reader := ZIPReader.new()
	var err := reader.open(zip_path)
	if err != OK:
		_load_issues.append("install_pack_zip: cannot open %s (err %d)" % [zip_path, err])
		return ""
	var files := reader.get_files()
	# Locate manifest.json (at root or one level deep).
	var manifest_idx := files.find("manifest.json")
	var root_prefix := ""
	if manifest_idx == -1:
		for i in files.size():
			if files[i].ends_with("/manifest.json") and files[i].count("/") == 1:
				manifest_idx = i
				root_prefix = files[i].get_base_dir() + "/"
				break
	if manifest_idx == -1:
		_load_issues.append("install_pack_zip: no manifest.json in %s" % zip_path)
		reader.close()
		return ""
	# Parse manifest to get mod_id (determines install folder).
	var manifest_bytes := reader.read_file(files[manifest_idx])
	var manifest_text := manifest_bytes.get_string_from_utf8()
	var probe := ModManifest.new()
	if not probe.parse_json(manifest_text):
		_load_issues.append("install_pack_zip: invalid manifest in %s: %s" %
				[zip_path, ", ".join(probe.errors)])
		reader.close()
		return ""
	var dest_dir := "user://mods/%s/" % probe.mod_id
	_ensure_dir(dest_dir)
	# Extract all files under root_prefix into dest_dir.
	for path in files:
		if not path.begins_with(root_prefix):
			continue
		var rel := path.substr(root_prefix.length())
		if rel.is_empty():
			continue
		if path.ends_with("/"):
			_ensure_dir(dest_dir + rel)
			continue
		_ensure_dir(dest_dir + rel.get_base_dir() + "/")
		var data := reader.read_file(path)
		var f := FileAccess.open(dest_dir + rel, FileAccess.WRITE)
		if f == null:
			_load_issues.append("install_pack_zip: cannot write %s" % (dest_dir + rel))
			continue
		f.store_buffer(data)
		f.close()
	reader.close()
	print("ModLoader: installed pack '%s' from %s" % [probe.mod_id, zip_path])
	return probe.mod_id

# ------------------------------------------------------------
# Internal helpers
# ------------------------------------------------------------

func _ensure_dir(path: String) -> void:
	DirAccess.make_dir_recursive_absolute(path)
