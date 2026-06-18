# ConsoleUI — In-game developer console for cheat commands and debug queries.
# Opens with the `/` key, accepts slash-prefixed commands, and displays
# output in a scrollable log.
class_name ConsoleUI
extends Control

signal console_opened
signal console_closed

const MAX_LOG_LINES := 200
const COMMAND_PREFIX := "/"

var _input_box: LineEdit
var _log_box: RichTextLabel
var _is_open := false
var _command_handlers: Dictionary = {}
var _player: PlayerController = null


func _ready() -> void:
	visible = false
	_build_ui()
	_register_default_commands()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_SLASH and not _is_open:
			open()
			get_viewport().set_input_as_handled()
			return
		if event.keycode == KEY_ESCAPE and _is_open:
			close()
			get_viewport().set_input_as_handled()
			return
		if event.keycode == KEY_ENTER and _is_open:
			_submit_command()
			get_viewport().set_input_as_handled()
			return


# --- Public API ---

func set_player(player: PlayerController) -> void:
	_player = player


func is_open() -> bool:
	return _is_open


func open() -> void:
	if _is_open:
		return
	_is_open = true
	visible = true
	_input_box.text = COMMAND_PREFIX
	_input_box.caret_column = _input_box.text.length()
	_input_box.grab_focus()
	console_opened.emit()


func close() -> void:
	if not _is_open:
		return
	_is_open = false
	visible = false
	_input_box.release_focus()
	console_closed.emit()


func print_line(text: String) -> void:
	if _log_box == null:
		return
	_log_box.append_text(text + "\n")

	# Trim old lines to prevent unbounded growth.
	var line_count := _log_box.get_line_count()
	if line_count > MAX_LOG_LINES:
		var excess := line_count - MAX_LOG_LINES
		_log_box.scroll_to_line(excess)


func register_command(name: String, callback: Callable, description: String) -> void:
	_command_handlers[name] = {
		"callback": callback,
		"description": description,
	}


# --- UI construction ---

func _build_ui() -> void:
	# Semi-transparent background panel.
	var panel := PanelContainer.new()
	panel.name = "ConsolePanel"
	panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	panel.custom_minimum_size = Vector2(0, 0)

	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.0, 0.0, 0.0, 0.75)
	style.border_color = Color(0.3, 0.3, 0.3, 1.0)
	style.set_border_width_all(1)
	style.set_content_margin_all(4)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)

	# Vertical layout: log on top, input at bottom.
	var vbox := VBoxContainer.new()
	vbox.name = "VBox"
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	vbox.add_theme_constant_override("separation", 4)
	panel.add_child(vbox)

	# Log area.
	_log_box = RichTextLabel.new()
	_log_box.name = "LogBox"
	_log_box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log_box.bbcode_enabled = true
	_log_box.scroll_following = true
	_log_box.custom_minimum_size = Vector2(0, 180)
	vbox.add_child(_log_box)

	# Input line.
	_input_box = LineEdit.new()
	_input_box.name = "InputBox"
	_input_box.placeholder_text = "Type /help for commands"
	_input_box.custom_minimum_size = Vector2(0, 28)
	vbox.add_child(_input_box)

	_input_box.text_submitted.connect(_on_text_submitted)


# --- Command registration ---

func _register_default_commands() -> void:
	register_command("fly", _cmd_fly, "Toggle creative fly mode")
	register_command("gravity", _cmd_gravity, "Show current gravity direction and zone")
	register_command("help", _cmd_help, "List available commands")
	register_command("speed", _cmd_speed, "Set fly speed (default 20, usage: /speed 40)")


# --- Command execution ---

func _submit_command() -> void:
	var raw := _input_box.text.strip_edges()
	if raw == "":
		close()
		return

	print_line(raw)

	if not raw.begins_with(COMMAND_PREFIX):
		print_line("[color=gray]Commands must start with /[/color]")
		_input_box.text = COMMAND_PREFIX
		_input_box.caret_column = 1
		return

	var parts := raw.substr(COMMAND_PREFIX.length()).split(" ", false, 1)
	var cmd_name: String = parts[0] if parts.size() > 0 else ""
	var cmd_args: String = parts[1] if parts.size() > 1 else ""

	var handler: Dictionary = _command_handlers.get(cmd_name, {})
	if handler.is_empty():
		print_line("[color=red]Unknown command: /%s[/color]" % cmd_name)
	else:
		var callback: Callable = handler["callback"]
		callback.call(cmd_args)

	_input_box.text = COMMAND_PREFIX
	_input_box.caret_column = 1


func _on_text_submitted(_text: String) -> void:
	_submit_command()


# --- Default command implementations ---

func _cmd_fly(_args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	_player.fly_mode = not _player.fly_mode
	var state := "ON" if _player.fly_mode else "OFF"
	print_line("[color=cyan]Fly mode: %s[/color]" % state)


func _cmd_gravity(_args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var dir := _player._gravity_direction
	var zone := "surface"
	if dir == Vector3.ZERO:
		zone = "space (zero-G)"
	elif _player.use_planet_gravity:
		zone = "planet gravity"
	else:
		zone = "default (down)"

	print_line("Gravity zone: %s" % zone)
	print_line("Gravity direction: (%.2f, %.2f, %.2f)" % [dir.x, dir.y, dir.z])
	print_line("Planet gravity radius: %.0f" % _player.planet_gravity_radius)
	if _player.use_planet_gravity:
		var dist := _player.global_position.distance_to(_player._planet_center)
		print_line("Distance to planet center: %.1f" % dist)


func _cmd_help(_args: String) -> void:
	print_line("[color=cyan]Available commands:[/color]")
	for cmd_name in _command_handlers.keys():
		var desc: String = _command_handlers[cmd_name]["description"]
		print_line("  /%s — %s" % [cmd_name, desc])


func _cmd_speed(args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	if args == "":
		print_line("Current fly speed: %.1f" % _player.fly_speed)
		return

	var value := args.to_float()
	if value <= 0.0:
		print_line("[color=red]Speed must be positive[/color]")
		return

	_player.fly_speed = value
	print_line("[color=cyan]Fly speed set to %.1f[/color]" % value)
