# ============================================================
# Example Expansion — reference content pack entry script.
# ============================================================
# Demonstrates the minimal pattern for a content pack: extend
# ModEntryPoint and implement register_content(registrar). Recipes
# reference builtin items by item_key (e.g. "ingot.iron") and are
# namespaced with the mod_id prefix to avoid collisions.
extends ModEntryPoint

func register_content(registrar: ModRegistrar) -> void:
	# Gilded iron block: compress iron + gold into a decorative block.
	# Uses only builtin material items, so no new item registration is
	# required for this example.
	registrar.add_crafting_recipe({
		"name": "example_expansion:craft_gilded_iron_block",
		"category": "materials",
		"required_station": "workbench",
		"inputs": [
			{"type": "item", "item_key": "ingot.iron", "count": 4},
			{"type": "item", "item_key": "ingot.gold", "count": 4},
		],
		"output": {"type": "item", "item_key": "block.iron", "count": 1},
	})
