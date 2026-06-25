# ============================================================
# ModManifest — Parsed and validated content pack manifest.
# ============================================================
# A content pack is described by a `manifest.json` at its root.
# This class parses the JSON, validates required fields, and
# exposes typed accessors used by ModLoader for dependency
# resolution and load-order computation.
#
# Manifest schema (manifest.json):
#   {
#     "mod_id": "example_expansion",        # required, unique snake_case id
#     "display_name": "Example Expansion",  # required, human-readable name
#     "version": "1.0.0",                   # required, semver-ish string
#     "author": "Author Name",              # optional
#     "description": "...",                 # optional
#     "entry_script": "mod.gd",             # optional, defaults to "mod.gd"
#     "dependencies": ["other_mod>=1.0.0"], # optional, mod ids that must load first
#     "load_before": ["third_mod"],         # optional, force load before these
#     "load_after": ["base_mod"],           # optional, force load after these
#     "steam_workshop_id": 123456789,       # optional, Steam Workshop item id
#     "min_game_version": "0.14.0"          # optional, minimum game version
#   }
#
# Dependency entries use the form "<mod_id>" or "<mod_id><op><version>"
# where <op> is one of >=, >, ==, <=, <. Plain "<mod_id>" means any version.
class_name ModManifest
extends RefCounted

# ------------------------------------------------------------
# Public fields
# ------------------------------------------------------------

# Unique pack identifier (snake_case). Empty if parsing failed.
var mod_id: String = ""

# Human-readable pack name.
var display_name: String = ""

# Version string (semver-ish, e.g. "1.2.0").
var version: String = ""

# Pack author.
var author: String = ""

# Short description.
var description: String = ""

# Entry script path relative to pack root. Defaults to "mod.gd".
var entry_script: String = "mod.gd"

# Steam Workshop item id (0 if not a workshop pack).
var steam_workshop_id: int = 0

# Minimum game version required (empty string = no constraint).
var min_game_version: String = ""

# Absolute path to the pack root folder on disk.
var pack_path: String = ""

# Source that discovered this pack (e.g. "local", "workshop").
var source: String = ""

# Raw dependency strings as authored, e.g. ["other_mod>=1.0.0"].
var dependency_entries: PackedStringArray = PackedStringArray()

# Raw load_before / load_after mod id lists.
var load_before: PackedStringArray = PackedStringArray()
var load_after: PackedStringArray = PackedStringArray()

# Validation errors collected during parse. Empty if valid.
var errors: PackedStringArray = PackedStringArray()

# ------------------------------------------------------------
# Public API
# ------------------------------------------------------------

# Parse a manifest.json file from disk. Returns true on success.
# Even on failure the partial fields are populated and `errors`
# describes what went wrong.
func load_from_file(path: String) -> bool:
	pack_path = path.get_base_dir()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		errors.append("cannot open manifest file: %s" % path)
		return false
	var text := file.get_as_text()
	file.close()
	return parse_json(text)

# Parse a manifest from a JSON string.
func parse_json(text: String) -> bool:
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		errors.append("manifest is not a JSON object")
		return false
	var data: Dictionary = parsed
	_populate(data)
	_validate()
	return errors.is_empty()

# Return the list of dependency mod ids (without version constraints).
# Used for topological sort edges.
func dependency_ids() -> PackedStringArray:
	var ids: PackedStringArray = PackedStringArray()
	for entry in dependency_entries:
		ids.append(_split_dep_id(entry))
	return ids

# Return true if this manifest satisfies the dependency requirement
# expressed by `dep_entry` against the given `available_version`.
# `dep_entry` is e.g. "other_mod>=1.0.0"; `available_version` is the
# version string of the loaded dependency.
static func satisfies(dep_entry: String, available_version: String) -> bool:
	var parsed := _split_dep(dep_entry)
	var op: String = parsed[0]
	var want_ver: String = parsed[1]
	if op.is_empty():
		return true  # No version constraint.
	return _compare_version(available_version, op, want_ver)

# Return true if the manifest is structurally valid.
func is_valid() -> bool:
	return errors.is_empty()

# ------------------------------------------------------------
# Internal helpers
# ------------------------------------------------------------

func _populate(data: Dictionary) -> void:
	mod_id = String(data.get("mod_id", "")).strip_edges()
	display_name = String(data.get("display_name", "")).strip_edges()
	version = String(data.get("version", "")).strip_edges()
	author = String(data.get("author", "")).strip_edges()
	description = String(data.get("description", "")).strip_edges()
	var entry_val: String = String(data.get("entry_script", "mod.gd")).strip_edges()
	entry_script = entry_val if not entry_val.is_empty() else "mod.gd"
	steam_workshop_id = int(data.get("steam_workshop_id", 0))
	min_game_version = String(data.get("min_game_version", "")).strip_edges()
	dependency_entries = _to_string_array(data.get("dependencies", []))
	load_before = _to_string_array(data.get("load_before", []))
	load_after = _to_string_array(data.get("load_after", []))

func _validate() -> void:
	if mod_id.is_empty():
		errors.append("missing required field: mod_id")
	elif not _is_valid_mod_id(mod_id):
		errors.append("invalid mod_id '%s': must be snake_case [a-z0-9_]+" % mod_id)
	if display_name.is_empty():
		errors.append("missing required field: display_name")
	if version.is_empty():
		errors.append("missing required field: version")
	elif not _is_valid_version(version):
		errors.append("invalid version '%s': expected X.Y.Z form" % version)
	for entry in dependency_entries:
		if not _is_valid_dep_entry(entry):
			errors.append("invalid dependency entry '%s'" % entry)
	for mid in load_before:
		if not _is_valid_mod_id(mid):
			errors.append("invalid load_before id '%s'" % mid)
	for mid in load_after:
		if not _is_valid_mod_id(mid):
			errors.append("invalid load_after id '%s'" % mid)

static func _to_string_array(value: Variant) -> PackedStringArray:
	var result: PackedStringArray = PackedStringArray()
	if typeof(value) == TYPE_ARRAY:
		for item in value:
			var s := String(item).strip_edges()
			if not s.is_empty():
				result.append(s)
	return result

static func _is_valid_mod_id(id: String) -> bool:
	if id.is_empty():
		return false
	for ch in id:
		var valid := (ch >= "a" and ch <= "z") or \
				(ch >= "0" and ch <= "9") or ch == "_"
		if not valid:
			return false
	return true

static func _is_valid_version(v: String) -> bool:
	if v.is_empty():
		return false
	var parts := v.split(".")
	if parts.size() < 2 or parts.size() > 3:
		return false
	for part in parts:
		if part.is_empty():
			return false
		for ch in part:
			if ch < "0" or ch > "9":
				return false
	return true

static func _is_valid_dep_entry(entry: String) -> bool:
	var parsed := _split_dep(entry)
	# parsed = [op, version, id]; id validated via _split_dep_id.
	var id := _split_dep_id(entry)
	if not _is_valid_mod_id(id):
		return false
	if not parsed[0].is_empty() and not _is_valid_version(parsed[1]):
		return false
	return true

# Split a dependency entry into [op, version]. The mod id is stripped.
# Returns ["", ""] for a plain id with no constraint.
static func _split_dep(entry: String) -> PackedStringArray:
	var ops := [">=", "<=", "==", ">", "<"]
	for op in ops:
		var idx := entry.find(op)
		if idx > 0:
			var ver := entry.substr(idx + op.length()).strip_edges()
			return PackedStringArray([op, ver])
	return PackedStringArray(["", ""])

# Extract the mod id from a dependency entry.
static func _split_dep_id(entry: String) -> String:
	var ops := [">=", "<=", "==", ">", "<"]
	for op in ops:
		var idx := entry.find(op)
		if idx > 0:
			return entry.substr(0, idx).strip_edges()
	return entry.strip_edges()

# Compare `available` against `want` using `op`. Versions are compared
# numerically component by component (X.Y.Z), missing components treated as 0.
static func _compare_version(available: String, op: String, want: String) -> bool:
	var cmp := _version_cmp(available, want)
	match op:
		">=": return cmp >= 0
		">": return cmp > 0
		"==": return cmp == 0
		"<=": return cmp <= 0
		"<": return cmp < 0
	return false

static func _version_cmp(a: String, b: String) -> int:
	var pa := a.split(".")
	var pb := b.split(".")
	var size := maxi(pa.size(), pb.size())
	for i in size:
		var na := int(pa[i]) if i < pa.size() and pa[i].is_valid_int() else 0
		var nb := int(pb[i]) if i < pb.size() and pb[i].is_valid_int() else 0
		if na < nb:
			return -1
		elif na > nb:
			return 1
	return 0
