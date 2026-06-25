# ============================================================
# LocalPackSource — Discovers content packs from a local folder.
# ============================================================
# Scans a directory (default `user://mods/`) for sub-folders that
# contain a `manifest.json`. Each such folder is treated as a pack.
#
# This source serves two workflows:
#   1. Author development: authors drop a folder here to test.
#   2. Installed packs: ZIPs are extracted into this folder by
#      ModLoader.install_pack_zip() before loading.
#
# A read-only `res://mods/` path can also be configured for packs
# shipped with the game build (e.g. official mini-expansions).
class_name LocalPackSource
extends ModPackSource

# Directory to scan for pack folders. Defaults to user://mods/.
var scan_dir: String = "user://mods/"

# Optional secondary read-only directory (e.g. res://mods/). Empty
# disables it. Packs here are loaded but never written to.
var readonly_dir: String = ""

func _init() -> void:
	source_name = "local"

func discover_packs() -> Array[ModManifest]:
	var result: Array[ModManifest] = []
	_scan_into(scan_dir, result, false)
	if not readonly_dir.is_empty():
		_scan_into(readonly_dir, result, true)
	return result

# ------------------------------------------------------------
# Internal
# ------------------------------------------------------------

func _scan_into(dir_path: String, out: Array[ModManifest], is_readonly: bool) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		# Directory not existing is normal for a fresh install; skip silently.
		return
	dir.list_dir_begin()
	var name := dir.get_next()
	while not name.is_empty():
		if name == "." or name == ".." or name.begins_with("."):
			name = dir.get_next()
			continue
		var full := dir_path.trim_suffix("/") + "/" + name
		if dir.dir_exists(name):
			var manifest := _load_manifest(full)
			if manifest != null:
				manifest.pack_path = full
				manifest.source = source_name
				out.append(manifest)
		name = dir.get_next()
	dir.list_dir_end()
