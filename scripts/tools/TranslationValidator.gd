@tool
class_name TranslationValidator
extends EditorPlugin

# TranslationValidator — scans all title_key usage and validates .csv completeness
#
# Usage:
#   Run from Godot editor: Project > Tools > Validate Translations
#   Or call TranslationValidator.validate() from any script.
#
# What it does:
#   1. Scans all GDScript files for tr(key) calls and collects keys
#   2. Scans all GDScript files for "title_key" field assignments
#   3. Reads locale/*.csv files
#   4. Reports:
#      - Keys in code that are missing from .csv files
#      - Keys in .csv that are unused in code
#      - Duplicate keys in .csv files

const LOCALE_DIR := "res://locale/"
const SCRIPT_DIRS := ["res://scripts/"]
const CSV_HEADER := "keys,"
const COMMENT_PREFIX := "#"

var _code_keys: Dictionary = {}   # key -> [file:line]
var _csv_keys: Dictionary = {}    # locale -> {key -> line}


func _enter_tree() -> void:
	add_tool_menu_item("Validate Translations", _on_validate)


func _exit_tree() -> void:
	remove_tool_menu_item("Validate Translations")


func _on_validate() -> void:
	validate()


static func validate() -> void:
	var v: TranslationValidator = TranslationValidator.new()
	v._run()
	v.free()


func _run() -> void:
	print("=== Translation Validator ===")
	_scan_csv_files()
	_scan_gdscript_files()
	_report_results()


func _scan_csv_files() -> void:
	var dir := DirAccess.open(LOCALE_DIR)
	if dir == null:
		push_error("TranslationValidator: Cannot open locale directory: %s" % LOCALE_DIR)
		return

	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if file_name.ends_with(".csv"):
			var path := LOCALE_DIR.path_join(file_name)
			_parse_csv(path, file_name)
		file_name = dir.get_next()
	dir.list_dir_end()


func _parse_csv(path: String, file_name: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		push_error("TranslationValidator: Cannot open %s" % path)
		return

	var locale := file_name.replace(".csv", "")
	if not _csv_keys.has(locale):
		_csv_keys[locale] = {}

	var line_num := 0
	while not file.eof_reached():
		var line := file.get_line()
		line_num += 1
		line = line.strip_edges()

		if line.is_empty() or line.begins_with(COMMENT_PREFIX):
			continue
		if line.begins_with(CSV_HEADER):
			continue

		var comma_idx := line.find(",")
		if comma_idx == -1:
			push_warning("TranslationValidator: %s:%d — malformed line (no comma)" % [file_name, line_num])
			continue

		var key := line.substr(0, comma_idx).strip_edges()
		var _value := line.substr(comma_idx + 1).strip_edges()

		if key.is_empty():
			push_warning("TranslationValidator: %s:%d — empty key" % [file_name, line_num])
			continue

		var locale_dict: Dictionary = _csv_keys[locale]
		if locale_dict.has(key):
			push_warning("TranslationValidator: %s:%d — duplicate key '%s' (first at line %d)" %
				[file_name, line_num, key, locale_dict[key]])
		else:
			locale_dict[key] = line_num

	file.close()


func _scan_gdscript_files() -> void:
	for dir_path: String in SCRIPT_DIRS:
		_scan_directory(dir_path)


func _scan_directory(dir_path: String) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return

	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		var full_path := dir_path.path_join(file_name)
		if dir.current_is_dir():
			if not file_name.begins_with("."):
				_scan_directory(full_path)
		elif file_name.ends_with(".gd"):
			_scan_file(full_path)
		file_name = dir.get_next()
	dir.list_dir_end()


func _scan_file(path: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return

	var line_num := 0
	while not file.eof_reached():
		var line := file.get_line()
		line_num += 1

		# Scan for tr("key") calls
		var tr_pos := line.find("tr(")
		while tr_pos != -1:
			var open_paren := line.find("(", tr_pos)
			if open_paren == -1:
				break
			var after_paren := open_paren + 1
			var close_paren := _find_matching_paren(line, after_paren)
			if close_paren == -1:
				break

			var inner := line.substr(after_paren, close_paren - after_paren).strip_edges()

			# Extract the key string (handle both "key" and 'key')
			var key := _extract_string_literal(inner)
			if key != null:
				if not _code_keys.has(key):
					_code_keys[key] = []
				var key_refs: Array = _code_keys[key]
				key_refs.append("%s:%d" % [path.trim_prefix("res://"), line_num])

			tr_pos = line.find("tr(", close_paren)

		# Scan for title_key assignments (title_key = "some.key" or "title_key": "some.key")
		var patterns := [
			_title_key_assignment_regex(),
		]
		for pattern: RegEx in patterns:
			var match: RegExMatch = pattern.search(line)
			if match:
				var key: String = match.get_string("key")
				if key:
					if not _code_keys.has(key):
						_code_keys[key] = []
					var key_refs: Array = _code_keys[key]
					key_refs.append("%s:%d" % [path.trim_prefix("res://"), line_num])

	file.close()


func _find_matching_paren(s: String, start: int) -> int:
	var depth := 1
	for i in range(start, s.length()):
		var c := s[i]
		if c == "(":
			depth += 1
		elif c == ")":
			depth -= 1
			if depth == 0:
				return i
	return -1


func _extract_string_literal(s: String) -> String:
	s = s.strip_edges()
	if s.is_empty():
		return String()

	# Check for concatenation: "key1" + "key2" — not supported, skip
	if s.contains("+"):
		return String()

	var quote := s[0]
	if quote != '"' and quote != "'":
		return String()

	var end := s.find(quote, 1)
	if end == -1:
		return String()

	return s.substr(1, end - 1)


func _title_key_assignment_regex() -> RegEx:
	var r := RegEx.new()
	r.compile('title_key\\s*[=:]\\s*"([^"]+)"')
	return r


func _report_results() -> void:
	print("\n=== Translation Validation Report ===")

	var en_keys: Dictionary = _csv_keys.get("en", {})
	var zh_keys: Dictionary = _csv_keys.get("zh", {})

	# Check keys in code that are missing from en.csv
	var missing_en: Array[String] = []
	for key: String in _code_keys:
		if not en_keys.has(key):
			missing_en.append(key)

	if missing_en.size() > 0:
		missing_en.sort()
		print("\n--- Keys used in code but MISSING from en.csv (%d) ---" % missing_en.size())
		for key in missing_en:
			var refs: Array = _code_keys[key]
			print("  %s  (used in: %s)" % [key, "; ".join(PackedStringArray(refs))])
	else:
		print("\nAll code keys are present in en.csv ✓")

	# Check keys missing from zh.csv
	var missing_zh: Array[String] = []
	for key: String in _code_keys:
		if not zh_keys.has(key):
			missing_zh.append(key)

	if missing_zh.size() > 0:
		missing_zh.sort()
		print("\n--- Keys used in code but MISSING from zh.csv (%d) ---" % missing_zh.size())
		for key in missing_zh:
			var refs: Array = _code_keys[key]
			print("  %s  (used in: %s)" % [key, "; ".join(PackedStringArray(refs))])
	else:
		print("\nAll code keys are present in zh.csv ✓")

	# Check unused keys in CSV (only report from en.csv)
	var unused: Array[String] = []
	for key: String in en_keys:
		if not _code_keys.has(key):
			unused.append(key)

	if unused.size() > 0:
		unused.sort()
		print("\n--- Keys in en.csv but NOT used in code (%d) ---" % unused.size())
		for key in unused:
			print("  %s (line %d)" % [key, en_keys[key]])
	else:
		print("\nAll en.csv keys are used in code ✓")

	print("\n=== Stats ===")
	print("  Code keys found:     %d" % _code_keys.size())
	print("  en.csv keys:         %d" % en_keys.size())
	print("  zh.csv keys:         %d" % zh_keys.size())
	print("  Missing from en.csv: %d" % missing_en.size())
	print("  Missing from zh.csv: %d" % missing_zh.size())
	print("  Unused in en.csv:    %d" % unused.size())
	print("=== Validation Complete ===")
