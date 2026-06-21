extends Node

const MAIN_MENU_SCENE := preload("res://MainMenu.tscn")

var _closed_count := 0


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var menu := MAIN_MENU_SCENE.instantiate()
	add_child(menu)
	await get_tree().process_frame

	var settings := menu.get_node_or_null(^"SettingsUI") as SettingsUI
	if settings == null:
		_fail("SettingsUI is missing")
		return
	settings.closed.connect(_on_settings_closed)

	# Owner-driven state changes must hide settings without emitting closed.
	settings.open()
	menu.call(&"_show_main_menu")
	if settings.is_open() or _closed_count != 0:
		_fail("owner-driven dismiss emitted closed")
		return

	# A user close emits exactly once, including when the owner handles it.
	settings.open()
	settings.close()
	settings.close()
	if settings.is_open() or _closed_count != 1:
		_fail("user close was not idempotent")
		return

	print("Main menu startup smoke passed: settings close is non-recursive.")
	get_tree().quit(0)


func _on_settings_closed() -> void:
	_closed_count += 1


func _fail(message: String) -> void:
	push_error("Main menu startup smoke failed: %s" % message)
	get_tree().quit(1)
