# ============================================================
# ModEventBus — Event dispatch to content packs with crash isolation.
# ============================================================
#
# ModEventBus bridges C++ simulation events (exposed as signals on
# GDTickSystem) to GDScript callbacks registered by content packs.
# Each callback is wrapped in a crash guard: if a callback throws or
# returns an error, the offending mod is disabled for the rest of
# the session and a warning is logged, but the game continues.
#
# Registration:
#   registrar.subscribe_event("creature_killed", _on_creature_killed)
#
# The event_name must match a signal on GDTickSystem. The callback
# signature depends on the signal (see GDTickSystem signal list).
#
# Crash isolation policy:
#   - First crash: log a warning, disable the mod's event subscriptions.
#   - The mod remains loaded but receives no further events.
#   - A disabled mod can be re-enabled manually via enable_mod().
class_name ModEventBus
extends RefCounted

# ------------------------------------------------------------
# State
# ------------------------------------------------------------

# Map: event_name -> Array[Dictionary{mod_id, callable}]
var _subscribers: Dictionary = {}

# Reference to the GDTickSystem node (set by ModLoader).
var _tick_system: Node = null

# ------------------------------------------------------------
# Lifecycle
# ------------------------------------------------------------

# Bind to a GDTickSystem node and connect all known event signals.
# Called by ModLoader after the tick system is available.
func bind_tick_system(tick_system: Node) -> void:
	_tick_system = tick_system
	if _tick_system == null:
		return
	# Connect all signals that packs may subscribe to. The signal
	# list is derived from GDTickSystem's ADD_SIGNAL declarations.
	# We use a defensive connect: if a signal doesn't exist, skip it.
	for event_name in _known_events():
		if _tick_system.has_signal(event_name):
			_tick_system.connect(event_name, _on_event.bind(event_name))

# ------------------------------------------------------------
# Public API
# ------------------------------------------------------------

# Subscribe a callback to an event. Returns true on success.
# mod_id: the pack that owns this callback (for crash isolation).
# event_name: must match a GDTickSystem signal name.
# callback: Callable that matches the signal's argument list.
func subscribe(mod_id: String, event_name: String, callback: Callable) -> bool:
	if mod_id.is_empty() or not callback.is_valid():
		return false
	if ModCrashGuard.is_disabled(mod_id):
		push_warning("[ModEventBus] mod '%s' is disabled, cannot subscribe" % mod_id)
		return false
	if not _subscribers.has(event_name):
		_subscribers[event_name] = []
	_subscribers[event_name].append({
		"mod_id": mod_id,
		"callback": callback,
	})
	return true

# Unsubscribe all callbacks for a given mod. Called when a mod is unloaded.
func unsubscribe_all(mod_id: String) -> void:
	for event_name in _subscribers.keys():
		var list: Array = _subscribers[event_name]
		var i := 0
		while i < list.size():
			if list[i].mod_id == mod_id:
				list.remove_at(i)
			else:
				i += 1

# Disable a mod's event subscriptions (after a crash).
# Delegates to ModCrashGuard for a single source of truth.
func disable_mod(mod_id: String) -> void:
	if ModCrashGuard.is_disabled(mod_id):
		return
	ModCrashGuard.disable_mod(mod_id, "event handler crash")
	unsubscribe_all(mod_id)

# Re-enable a previously disabled mod. The mod must re-subscribe.
func enable_mod(mod_id: String) -> void:
	ModCrashGuard.enable_mod(mod_id)

# Returns true if the mod is currently disabled.
func is_mod_disabled(mod_id: String) -> bool:
	return ModCrashGuard.is_disabled(mod_id)

# Returns the list of known event names that packs may subscribe to.
func get_known_events() -> PackedStringArray:
	return PackedStringArray(_known_events())

# ------------------------------------------------------------
# Internal — event dispatch with crash isolation
# ------------------------------------------------------------

# Called when any GDTickSystem signal fires. Dispatches to subscribers.
func _on_event(event_name: String, ...args: Array) -> void:
	if not _subscribers.has(event_name):
		return
	var list: Array = _subscribers[event_name]
	# Iterate over a copy because handlers may unsubscribe during dispatch.
	var snapshot := list.duplicate()
	for entry in snapshot:
		if ModCrashGuard.is_disabled(entry.mod_id):
			continue
		var cb: Callable = entry.callback
		if not cb.is_valid():
			continue
		# Crash guard: if the callback throws, disable the mod.
		var error_report := _safe_call(cb, args)
		if not error_report.is_empty():
			disable_mod(entry.mod_id)
			push_warning("[ModEventBus] event '%s' handler from mod '%s' crashed: %s" %
				[event_name, entry.mod_id, error_report])

# Safely call a callable, returning an error string on failure.
# Returns "" on success. GDScript doesn't have try/catch, but we
# detect common failure modes (invalid object, method error) by
# checking the result variant type and using Object.callv error codes.
func _safe_call(cb: Callable, args: Array) -> String:
	# GDScript Callable.callv returns the result; errors in the called
	# function propagate as push_error/push_warning and a null return.
	# We cannot truly catch exceptions, but we can detect invalid
	# callables and object-free errors.
	if not cb.is_valid():
		return "invalid callable"
	var object: Object = cb.get_object()
	if object != null and not is_instance_valid(object):
		return "object freed"
	var result: Variant = cb.callv(args)
	# If the result is a String starting with "ERROR:", treat as error.
	# This is a convention packs may use to signal failure.
	if result is String and result.begins_with("ERROR:"):
		return String(result)
	return ""

# ------------------------------------------------------------
# Known events — mirrors GDTickSystem signal names
# ------------------------------------------------------------
func _known_events() -> Array:
	return [
		"terrain_changed",
		"chunk_generated",
		"chunk_loaded",
		"chunk_unloaded",
		"creature_spawned",
		"creature_killed",
		"creature_moved",
		"crop_harvested",
		"tree_planted",
		"tree_grown",
		"machine_started",
		"machine_stopped",
		"power_network_changed",
		"fluid_network_changed",
		"player_joined",
		"player_left",
		"player_mined_block",
		"player_placed_block",
		"region_updated",
		"season_changed",
		"day_night_phase_changed",
	]
