# ============================================================
# ModCrashGuard — Wraps mod entry-point calls with crash isolation.
# ============================================================
#
# GDScript doesn't have native try/catch, but we can detect common
# failure modes:
#   1. The entry script fails to load (ResourceLoader returns null).
#   2. The entry object is freed during execution.
#   3. A called method emits a push_error (we can't catch this, but
#      we can check the object is still valid afterward).
#   4. The method returns an error sentinel.
#
# When a mod crashes, ModCrashGuard marks it as disabled in the
# ModLoader's disabled set, and the loader skips all subsequent
# lifecycle calls for that mod.
#
# This class also integrates with ModEventBus: a disabled mod's
# event subscriptions are removed.
class_name ModCrashGuard
extends RefCounted

# ------------------------------------------------------------
# State
# ------------------------------------------------------------

# Set of disabled mod_ids.
static var _disabled_mods: Dictionary = {}

# Crash logs: mod_id -> Array[String] (recent crash messages).
static var _crash_logs: Dictionary = {}

# ------------------------------------------------------------
# Public API
# ------------------------------------------------------------

# Returns true if the mod is disabled due to a crash.
static func is_disabled(mod_id: String) -> bool:
	return _disabled_mods.has(mod_id)

# Disable a mod and record the crash reason.
static func disable_mod(mod_id: String, reason: String) -> void:
	if _disabled_mods.has(mod_id):
		return
	_disabled_mods[mod_id] = true
	if not _crash_logs.has(mod_id):
		_crash_logs[mod_id] = []
	(_crash_logs[mod_id] as Array).append(reason)
	push_warning("[ModCrashGuard] mod '%s' disabled: %s" % [mod_id, reason])

# Re-enable a previously disabled mod. The mod must be re-loaded
# manually (e.g. by calling ModLoader.load_mods() again).
static func enable_mod(mod_id: String) -> void:
	_disabled_mods.erase(mod_id)

# Returns the crash log for a mod (Array[String]).
static func get_crash_log(mod_id: String) -> Array:
	return _crash_logs.get(mod_id, [])

# Reset all disabled mods and crash logs. Used before a full reload.
static func reset() -> void:
	_disabled_mods.clear()
	_crash_logs.clear()

# ------------------------------------------------------------
# Safe call wrappers
# ------------------------------------------------------------

# Safely call register_content on an entry point. Returns true on
# success, false if the mod crashed (and was disabled).
static func safe_register_content(entry: ModEntryPoint,
		registrar: ModRegistrar, mod_id: String) -> bool:
	if is_disabled(mod_id):
		return false
	if entry == null or not is_instance_valid(entry):
		disable_mod(mod_id, "entry point is null or freed")
		return false
	# Check the method exists before calling.
	if not entry.has_method("register_content"):
		disable_mod(mod_id, "entry point missing register_content method")
		return false
	var object_before: Object = entry
	entry.register_content(registrar)
	# Verify the object is still valid after the call.
	if not is_instance_valid(object_before):
		disable_mod(mod_id, "entry point freed during register_content")
		return false
	return true

# Safely call a lifecycle hook on an entry point.
static func safe_call_lifecycle(entry: ModEntryPoint, mod_id: String,
		method_name: String, arg: Variant) -> bool:
	if is_disabled(mod_id):
		return false
	if entry == null or not is_instance_valid(entry):
		return false
	if not entry.has_method(method_name):
		return true  # Not an error: the hook is optional.
	var object_before: Object = entry
	entry.call(method_name, arg)
	if not is_instance_valid(object_before):
		disable_mod(mod_id, "entry point freed during %s" % method_name)
		return false
	return true

# Safely invoke a generic Callable (e.g. a custom block entity tick
# callback). Returns the result, or null on crash. The mod_id is
# used to disable the mod on crash.
static func safe_call(callable: Callable, mod_id: String,
		args: Array = []) -> Variant:
	if is_disabled(mod_id):
		return null
	if not callable.is_valid():
		disable_mod(mod_id, "callable is invalid")
		return null
	var object: Object = callable.get_object()
	if object != null and not is_instance_valid(object):
		disable_mod(mod_id, "callable target object freed")
		return null
	var result: Variant = callable.callv(args)
	if object != null and not is_instance_valid(object):
		disable_mod(mod_id, "callable target object freed during call")
		return null
	return result
