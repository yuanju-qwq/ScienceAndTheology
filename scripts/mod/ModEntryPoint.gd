# ============================================================
# ModEntryPoint — Base class for content pack entry scripts.
# ============================================================
# Each content pack provides an entry script (default `mod.gd` at
# the pack root) that extends this class. ModLoader instantiates
# the entry script and calls `register_content(registrar)` in
# load order, after the builtin content has been registered.
#
# The registrar is a ModRegistrar scoped to this pack; it wraps
# the existing C++ binding APIs (GDCraftingManager,
# GDRecipeDatabase, ItemDatabase) and tracks conflicts.
#
# Minimal example (mod.gd):
#   extends ModEntryPoint
#
#   func register_content(registrar: ModRegistrar) -> void:
#       registrar.add_crafting_recipe({
#           "name": "my_mod:craft_widget",
#           "category": "misc",
#           "required_station": "workbench",
#           "inputs": [{"type": "item", "item_key": "ingot.iron", "count": 1}],
#           "output": {"type": "item", "item_key": "my_widget", "count": 1},
#       })
#
# Pack authors should prefix recipe names and item keys with their
# mod_id (e.g. "my_mod:craft_widget") to avoid collisions with
# builtin content and other packs.
class_name ModEntryPoint
extends RefCounted

# The manifest for this pack, set by ModLoader before registration.
var manifest: ModManifest = null

# Register this pack's content via the registrar. Override in
# subclasses. Called exactly once per pack, in load order.
func register_content(_registrar: ModRegistrar) -> void:
	# Default no-op; packs override.
	pass

# Optional lifecycle hook called after all packs have registered.
# Useful for cross-pack integration. Override if needed.
func on_all_packs_loaded(_loader: Node) -> void:
	pass

# Optional lifecycle hook called when a world is loaded. Override
# to perform world-specific setup (e.g. registering world-gen
# overrides, initializing per-world state).
func on_world_loaded(_registrar: ModRegistrar) -> void:
	pass

# Optional lifecycle hook called before a world is saved. Override
# to flush any pending state to the registrar or world data.
func on_world_saving(_registrar: ModRegistrar) -> void:
	pass

# Optional lifecycle hook called when this mod is about to be
# unloaded (e.g. during a hot-reload or shutdown). Override to
# clean up resources, disconnect signals, and unregister handlers.
func on_mod_unloading(_registrar: ModRegistrar) -> void:
	pass
