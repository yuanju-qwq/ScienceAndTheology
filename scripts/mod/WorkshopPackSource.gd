# ============================================================
# WorkshopPackSource — Discovers content packs from Steam Workshop.
# ============================================================
# Steam downloads subscribed Workshop items as folders into a
# workshop content directory (typically
# `<steam_install>/steamapps/workshop/content/<app_id>/<item_id>/`).
# Each item folder is expected to contain a `manifest.json` whose
# `steam_workshop_id` matches the folder id.
#
# Steam API integration (querying subscribed items, querying install
# paths via Steamworks) is provided by overriding `_collect_item_paths()`.
# The default implementation scans a configurable directory, which
# works for the standard Steam Workshop download layout and for
# testing without the Steam API.
class_name WorkshopPackSource
extends ModPackSource

# Workshop content directory to scan. Should point at the parent
# folder containing per-item sub-folders (e.g. .../content/<app_id>/).
var workshop_dir: String = ""

# Steam app id (used by subclasses that talk to the Steam API).
var steam_app_id: int = 0

func _init() -> void:
	source_name = "workshop"

func discover_packs() -> Array[ModManifest]:
	var result: Array[ModManifest] = []
	var item_paths := _collect_item_paths()
	for item_path in item_paths:
		var manifest := _load_manifest(item_path)
		if manifest == null:
			continue
		manifest.pack_path = item_path
		manifest.source = source_name
		# Record the workshop item id from the folder name if not set.
		if manifest.steam_workshop_id == 0:
			manifest.steam_workshop_id = _infer_workshop_id(item_path)
		result.append(manifest)
	return result

# ------------------------------------------------------------
# Steam API extension point
# ------------------------------------------------------------

# Return the list of workshop item folder paths to scan. The default
# implementation scans `workshop_dir` for sub-folders. Subclasses that
# integrate the Steamworks API should override this to query
# subscribed items and return their installed paths directly.
func _collect_item_paths() -> PackedStringArray:
	var paths: PackedStringArray = PackedStringArray()
	if workshop_dir.is_empty():
		return paths
	var dir := DirAccess.open(workshop_dir)
	if dir == null:
		return paths
	dir.list_dir_begin()
	var name := dir.get_next()
	while not name.is_empty():
		if name == "." or name == ".." or name.begins_with("."):
			name = dir.get_next()
			continue
		if dir.dir_exists(name):
			paths.append(workshop_dir.trim_suffix("/") + "/" + name)
		name = dir.get_next()
	dir.list_dir_end()
	return paths

# Infer the workshop item id from the folder name. Returns 0 if the
# folder name is not a numeric id.
func _infer_workshop_id(path: String) -> int:
	var folder := path.get_file()
	if folder.is_valid_int():
		return folder.to_int()
	return 0
