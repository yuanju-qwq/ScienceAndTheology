# ============================================================
# ModPackSource — Abstract source of content pack folders.
# ============================================================
# A pack source discovers content pack directories and parses their
# manifests. Concrete sources (LocalPackSource, WorkshopPackSource)
# implement `discover_packs()`. This abstraction lets ModLoader
# merge packs from multiple origins: the local mods folder, a
# Steam Workshop directory, or future custom sources.
class_name ModPackSource
extends RefCounted

# Human-readable source name for diagnostics (e.g. "local", "workshop").
var source_name: String = "abstract"

# Discover packs from this source. Returns an array of ModManifest
# objects (one per valid pack folder). Invalid folders are skipped
# with a warning. Override in subclasses.
func discover_packs() -> Array[ModManifest]:
	return Array([], TYPE_OBJECT, "Resource", ModManifest)

# Parse a manifest from a folder. Returns null if the folder has no
# manifest or the manifest is invalid (errors are pushed as warnings).
func _load_manifest(folder_path: String) -> ModManifest:
	var manifest_path := folder_path + "/manifest.json"
	if not FileAccess.file_exists(manifest_path):
		return null
	var manifest := ModManifest.new()
	manifest.source = source_name
	if not manifest.load_from_file(manifest_path):
		push_warning("[ModPackSource:%s] invalid manifest at %s: %s" %
				[source_name, manifest_path, ", ".join(manifest.errors)])
		return null
	return manifest
