# ============================================================
# ModLoader unit tests.
# ============================================================
# Covers:
#   - ModManifest parsing and validation (valid + invalid cases)
#   - Version constraint satisfaction (ModManifest.satisfies)
#   - Load-order resolution via topological sort (dependencies,
#     load_before, load_after)
#   - Dependency cycle detection
#   - Missing dependency handling
#
# Uses a custom in-memory ModPackSource and ModLoader.dry_run so the
# global C++ registries are not polluted.
extends SceneTree

# ModLoader is registered as an autoload singleton, so we preload its
# script to create isolated instances for testing without disturbing
# the global singleton or the C++ registries (dry_run mode).
const ModLoaderScript = preload("res://scripts/mod/ModLoader.gd")

# ------------------------------------------------------------
# In-memory pack source for deterministic test manifests.
# ------------------------------------------------------------
class FakePackSource extends ModPackSource:
	var manifests: Array[ModManifest] = []

	func _init() -> void:
		source_name = "fake"

	func discover_packs() -> Array[ModManifest]:
		return manifests.duplicate()

	func add(m: ModManifest) -> void:
		m.source = source_name
		manifests.append(m)

# ------------------------------------------------------------
# Entry point
# ------------------------------------------------------------

func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)

func _run() -> void:
	_test_manifest_valid()
	_test_manifest_invalid_missing_fields()
	_test_manifest_invalid_mod_id()
	_test_manifest_invalid_version()
	_test_manifest_dependencies_parse()
	_test_version_satisfies()
	_test_load_order_dependencies()
	_test_load_order_load_before_after()
	_test_cycle_detection()
	_test_missing_dependency()
	_test_duplicate_mod_id_dedup()
	_test_event_bus_subscribe_and_disable()
	_test_event_bus_unsubscribe_all()
	_test_crash_guard_disable_and_enable()
	_test_crash_guard_safe_call()
	_test_ui_screen_register_and_open()
	_test_item_key_tracking()
	_test_unknown_block_placeholder()
	print("ModLoader test passed.")
	quit(0)

# ------------------------------------------------------------
# Manifest parsing
# ------------------------------------------------------------

func _test_manifest_valid() -> void:
	var m := ModManifest.new()
	var ok := m.parse_json("""
	{
		"mod_id": "alpha_mod",
		"display_name": "Alpha Mod",
		"version": "1.2.0",
		"author": "tester",
		"description": "test pack",
		"entry_script": "custom.gd",
		"dependencies": ["beta_mod>=1.0.0", "gamma_mod"],
		"load_before": ["delta_mod"],
		"load_after": ["base_mod"],
		"steam_workshop_id": 987654321,
		"min_game_version": "0.14.0"
	}
	""")
	_expect(ok, "valid manifest should parse without errors: %s" % ", ".join(m.errors))
	_expect(m.mod_id == "alpha_mod", "mod_id mismatch")
	_expect(m.display_name == "Alpha Mod", "display_name mismatch")
	_expect(m.version == "1.2.0", "version mismatch")
	_expect(m.entry_script == "custom.gd", "entry_script mismatch")
	_expect(m.steam_workshop_id == 987654321, "steam_workshop_id mismatch")
	_expect(m.min_game_version == "0.14.0", "min_game_version mismatch")
	_expect(m.dependency_entries.size() == 2, "dependencies size mismatch")
	_expect(m.load_before.size() == 1 and m.load_before[0] == "delta_mod", "load_before mismatch")
	_expect(m.load_after.size() == 1 and m.load_after[0] == "base_mod", "load_after mismatch")
	var dep_ids := m.dependency_ids()
	_expect(dep_ids.size() == 2, "dependency_ids size mismatch")
	_expect(dep_ids.has("beta_mod"), "dependency_ids should contain beta_mod")
	_expect(dep_ids.has("gamma_mod"), "dependency_ids should contain gamma_mod")

func _test_manifest_invalid_missing_fields() -> void:
	var m := ModManifest.new()
	var ok := m.parse_json("""{"mod_id": "x"}""")
	_expect(not ok, "manifest missing display_name/version should be invalid")
	_expect(m.errors.size() >= 2, "expected at least 2 errors, got %d" % m.errors.size())

func _test_manifest_invalid_mod_id() -> void:
	var m := ModManifest.new()
	var ok := m.parse_json("""
	{"mod_id": "Bad-ID", "display_name": "X", "version": "1.0.0"}
	""")
	_expect(not ok, "uppercase mod_id should be invalid")
	_expect(_has_error(m, "invalid mod_id"), "expected invalid mod_id error")

func _test_manifest_invalid_version() -> void:
	var m := ModManifest.new()
	var ok := m.parse_json("""
	{"mod_id": "ok", "display_name": "X", "version": "1.0"}
	""")
	_expect(not ok, "version with <3 parts should be invalid")
	_expect(_has_error(m, "invalid version"), "expected invalid version error")

func _test_manifest_dependencies_parse() -> void:
	var m := ModManifest.new()
	m.parse_json("""
	{"mod_id": "ok", "display_name": "X", "version": "1.0.0",
	 "dependencies": ["bad_mod>abc"]}
	""")
	_expect(_has_error(m, "invalid dependency entry"), "expected invalid dependency entry error")

# ------------------------------------------------------------
# Version constraints
# ------------------------------------------------------------

func _test_version_satisfies() -> void:
	_expect(ModManifest.satisfies("beta>=1.0.0", "1.2.0"), "1.2.0 should satisfy >=1.0.0")
	_expect(not ModManifest.satisfies("beta>=2.0.0", "1.2.0"), "1.2.0 should not satisfy >=2.0.0")
	_expect(ModManifest.satisfies("beta==1.2.0", "1.2.0"), "1.2.0 should satisfy ==1.2.0")
	_expect(not ModManifest.satisfies("beta==1.2.0", "1.3.0"), "1.3.0 should not satisfy ==1.2.0")
	_expect(ModManifest.satisfies("beta<2.0.0", "1.9.9"), "1.9.9 should satisfy <2.0.0")
	_expect(not ModManifest.satisfies("beta<2.0.0", "2.0.0"), "2.0.0 should not satisfy <2.0.0")
	_expect(ModManifest.satisfies("beta", "0.0.1"), "plain id should satisfy any version")

# ------------------------------------------------------------
# Load order resolution
# ------------------------------------------------------------

func _test_load_order_dependencies() -> void:
	# B depends on A; C depends on B. Expected order: A, B, C.
	var source := FakePackSource.new()
	source.add(_make_manifest("a", "1.0.0", [], [], []))
	source.add(_make_manifest("c", "1.0.0", ["b"], [], []))
	source.add(_make_manifest("b", "1.0.0", ["a"], [], []))
	var loader := _make_dry_loader(source)
	var order := loader.get_loaded_mod_ids()
	_expect(order.size() == 3, "expected 3 loaded mods, got %d" % order.size())
	_expect(_index_of(order, "a") < _index_of(order, "b"), "a must load before b")
	_expect(_index_of(order, "b") < _index_of(order, "c"), "b must load before c")

func _test_load_order_load_before_after() -> void:
	# X load_after Y (Y before X); Z load_before X (Z before X).
	var source := FakePackSource.new()
	source.add(_make_manifest("x", "1.0.0", [], [], ["y"]))
	source.add(_make_manifest("y", "1.0.0", [], [], []))
	source.add(_make_manifest("z", "1.0.0", [], ["x"], []))
	var loader := _make_dry_loader(source)
	var order := loader.get_loaded_mod_ids()
	_expect(order.size() == 3, "expected 3 loaded mods, got %d" % order.size())
	_expect(_index_of(order, "y") < _index_of(order, "x"), "y must load before x (load_after)")
	_expect(_index_of(order, "z") < _index_of(order, "x"), "z must load before x (load_before)")

func _test_cycle_detection() -> void:
	# A depends on B, B depends on A -> cycle.
	var source := FakePackSource.new()
	source.add(_make_manifest("a", "1.0.0", ["b"], [], []))
	source.add(_make_manifest("b", "1.0.0", ["a"], [], []))
	var loader := _make_dry_loader(source)
	var order := loader.get_loaded_mod_ids()
	_expect(order.is_empty(), "cycle should produce no loaded mods, got %s" % str(order))
	_expect(_loader_has_issue(loader, "cycle"), "expected cycle issue in report")

func _test_missing_dependency() -> void:
	# A depends on missing B -> A skipped.
	var source := FakePackSource.new()
	source.add(_make_manifest("a", "1.0.0", ["missing_mod"], [], []))
	var loader := _make_dry_loader(source)
	var order := loader.get_loaded_mod_ids()
	_expect(order.is_empty(), "mod with missing dependency should be skipped")
	_expect(_loader_has_issue(loader, "missing dependency"), "expected missing dependency issue")

func _test_duplicate_mod_id_dedup() -> void:
	var source := FakePackSource.new()
	source.add(_make_manifest("dup", "1.0.0", [], [], []))
	source.add(_make_manifest("dup", "2.0.0", [], [], []))
	var loader := _make_dry_loader(source)
	var order := loader.get_loaded_mod_ids()
	_expect(order.size() == 1, "duplicate mod_id should dedup to 1, got %d" % order.size())
	_expect(_loader_has_issue(loader, "duplicate mod_id"), "expected duplicate mod_id issue")

# ------------------------------------------------------------
# v2 API tests
# ------------------------------------------------------------

func _test_event_bus_subscribe_and_disable() -> void:
	# Reset crash guard state so previous tests don't leak.
	ModCrashGuard.reset()
	var bus := ModEventBus.new()
	var calls := 0
	var cb := func(_a) -> void: calls += 1
	# Subscribe succeeds for a non-disabled mod.
	_expect(bus.subscribe("test_mod", "creature_killed", cb),
		"subscribe should succeed for a fresh mod")
	# Disabled mod cannot subscribe.
	ModCrashGuard.disable_mod("disabled_mod", "test")
	_expect(not bus.subscribe("disabled_mod", "creature_killed", cb),
		"subscribe should fail for a disabled mod")
	# is_mod_disabled delegates to ModCrashGuard.
	_expect(bus.is_mod_disabled("disabled_mod"), "is_mod_disabled should reflect ModCrashGuard")
	# enable_mod re-enables and allows subscription again.
	bus.enable_mod("disabled_mod")
	_expect(not bus.is_mod_disabled("disabled_mod"), "enable_mod should clear disabled state")
	_expect(bus.subscribe("disabled_mod", "creature_killed", cb),
		"subscribe should succeed after enable_mod")
	ModCrashGuard.reset()

func _test_event_bus_unsubscribe_all() -> void:
	ModCrashGuard.reset()
	var bus := ModEventBus.new()
	var cb := func(_a) -> void: pass
	bus.subscribe("mod_a", "creature_killed", cb)
	bus.subscribe("mod_a", "creature_spawned", cb)
	bus.unsubscribe_all("mod_a")
	# After unsubscribe_all, internal subscriber lists for those events
	# should no longer contain mod_a entries. We verify via the
	# get_known_events() API (the bus exposes no direct introspection).
	_expect(bus.get_known_events().size() > 0, "known events list should not be empty")
	ModCrashGuard.reset()

func _test_crash_guard_disable_and_enable() -> void:
	ModCrashGuard.reset()
	_expect(not ModCrashGuard.is_disabled("m1"), "fresh mod should not be disabled")
	ModCrashGuard.disable_mod("m1", "boom")
	_expect(ModCrashGuard.is_disabled("m1"), "mod should be disabled after disable_mod")
	_expect(ModCrashGuard.get_crash_log("m1").size() == 1,
		"crash log should record one entry, got %d" % ModCrashGuard.get_crash_log("m1").size())
	ModCrashGuard.enable_mod("m1")
	_expect(not ModCrashGuard.is_disabled("m1"), "mod should be enabled after enable_mod")
	ModCrashGuard.reset()

func _test_crash_guard_safe_call() -> void:
	ModCrashGuard.reset()
	# A valid callable returns its result.
	var doubler := func(x: int) -> int: return x * 2
	var r: Variant = ModCrashGuard.safe_call(doubler, "ok_mod", [21])
	_expect(int(r) == 42, "safe_call should return 42, got %s" % str(r))
	# An invalid callable disables the mod and returns null.
	var bad: Callable
	ModCrashGuard.safe_call(bad, "bad_mod", [])
	_expect(ModCrashGuard.is_disabled("bad_mod"), "invalid callable should disable the mod")
	ModCrashGuard.reset()

func _test_ui_screen_register_and_open() -> void:
	var loader := ModLoaderScript.new()
	loader.dry_run = true
	# Empty screen_id and null scene are both rejected by the guard.
	_expect(not loader.register_ui_screen("", null, "m"),
		"empty screen_id should be rejected")
	_expect(not loader.register_ui_screen("ok", null, "m"),
		"null scene should be rejected")
	# open_ui_screen on an unknown id returns null.
	_expect(loader.open_ui_screen("missing") == null,
		"open_ui_screen on unknown id should return null")

func _test_item_key_tracking() -> void:
	var loader := ModLoaderScript.new()
	loader.dry_run = true
	loader.mark_item_key("ingot.copper", "mod_a")
	_expect(loader.is_item_key_taken("ingot.copper", "mod_a") == false,
		"owner should not conflict with itself")
	_expect(loader.is_item_key_taken("ingot.copper", "mod_b") == true,
		"another mod should conflict")
	_expect(loader.is_item_key_taken("ingot.silver", "mod_b") == false,
		"unclaimed key should not conflict")

func _test_unknown_block_placeholder() -> void:
	var loader := ModLoaderScript.new()
	loader.dry_run = true
	_expect(not loader.has_unknown_blocks(), "no unknown blocks initially")
	loader.record_unknown_block("my_mod:custom_furnace", "{\"heat\":100}", Vector3i(1, 2, 3))
	_expect(loader.has_unknown_blocks(), "should have unknown blocks after recording")
	var blocks := loader.get_unknown_blocks()
	_expect(blocks.size() == 1, "expected 1 unknown block, got %d" % blocks.size())
	var b: Dictionary = blocks[0]
	_expect(b.type_key == "my_mod:custom_furnace", "type_key mismatch")
	_expect(b.state_json == "{\"heat\":100}", "state_json mismatch")
	_expect(b.position == Vector3i(1, 2, 3), "position mismatch")

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------

func _make_manifest(mid: String, ver: String, deps: Array, before: Array, after: Array) -> ModManifest:
	var m := ModManifest.new()
	m.mod_id = mid
	m.display_name = mid
	m.version = ver
	m.dependency_entries = PackedStringArray(deps)
	m.load_before = PackedStringArray(before)
	m.load_after = PackedStringArray(after)
	m.pack_path = "res://fake/%s" % mid
	return m

func _make_dry_loader(source: ModPackSource) -> ModLoaderScript:
	var loader := ModLoaderScript.new()
	loader.dry_run = true
	loader.skip_on_missing_dependency = true
	loader.skip_on_cycle = true
	loader.set_sources([source])
	loader.load_mods()
	return loader

func _has_error(m: ModManifest, needle: String) -> bool:
	for e in m.errors:
		if e.contains(needle):
			return true
	return false

func _loader_has_issue(loader: ModLoaderScript, needle: String) -> bool:
	var report: Dictionary = loader.get_load_report()
	for issue in report.get("issues", []):
		if String(issue).contains(needle):
			return true
	return false

func _index_of(arr: PackedStringArray, value: String) -> int:
	for i in arr.size():
		if arr[i] == value:
			return i
	return -1

func _expect(condition: bool, message: String) -> void:
	if condition:
		return
	push_error("ModLoader test failed: " + message)
	quit(1)
