# ConsoleUI — In-game developer console for cheat commands and debug queries.
# Opens with the `/` key, accepts slash-prefixed commands, and displays
# output in a scrollable log.
class_name ConsoleUI
extends Control

signal console_opened
signal console_closed

enum PermissionLevel {
	PLAYER = 0,
	CHEATER = 1,
	OP = 2,
}

const MAX_LOG_LINES := 200
const COMMAND_PREFIX := "/"

var _input_box: LineEdit
var _log_box: RichTextLabel
var _is_open := false
var _command_handlers: Dictionary = {}
var _player: PlayerController = null
var _perf_overlay: PerfOverlay
var _permission_level: int = PermissionLevel.CHEATER
var _opus_profile_active := false
var _opus_profile_remaining := 0.0
var _opus_profile_top_n := 8


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


func _process(delta: float) -> void:
	if not _opus_profile_active:
		return
	_opus_profile_remaining -= delta
	if _opus_profile_remaining <= 0.0:
		_finish_opus_profile("complete")


# --- Public API ---

func set_player(player: PlayerController) -> void:
	_player = player


func is_open() -> bool:
	return _is_open


func set_permission_level(level: int) -> void:
	_permission_level = clampi(level, PermissionLevel.PLAYER, PermissionLevel.OP)


func get_permission_level() -> int:
	return _permission_level


func _permission_name(level: int) -> String:
	match level:
		PermissionLevel.PLAYER:
			return "player"
		PermissionLevel.CHEATER:
			return "cheater"
		PermissionLevel.OP:
			return "op"
	return "unknown"


func open() -> void:
	if _is_open:
		return
	_is_open = true
	visible = true
	_input_box.text = COMMAND_PREFIX
	_input_box.caret_column = _input_box.text.length()
	_input_box.grab_focus()
	console_opened.emit()
	print_line("[color=dim_gray]Console opened — permission: %s[/color]"
		% _permission_name(_permission_level))


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


func register_command(name: String, callback: Callable, description: String,
		level: int = PermissionLevel.PLAYER) -> void:
	_command_handlers[name] = {
		"callback": callback,
		"description": description,
		"level": level,
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
	# PLAYER — basic info commands.
	register_command("help", _cmd_help, "List available commands",
		PermissionLevel.PLAYER)
	register_command("gravity", _cmd_gravity, "Show current gravity direction and zone",
		PermissionLevel.PLAYER)
	register_command("planets", _cmd_planets, "List all travelable planets with distances",
		PermissionLevel.PLAYER)
	register_command("universe", _cmd_universe, "Show player's current universe position",
		PermissionLevel.PLAYER)

	# CHEATER — cheat/quality-of-life commands.
	register_command("fly", _cmd_fly, "Toggle flight in survival mode",
		PermissionLevel.CHEATER)
	register_command("gamemode", _cmd_gamemode, "Set game mode (survival/creative/observer)",
		PermissionLevel.CHEATER)
	register_command("speed", _cmd_speed, "Set flight/observer speed (default 20/30)",
		PermissionLevel.CHEATER)
	register_command("travel", _cmd_travel,
		"Debug teleport for dimension prototype (usage: /travel Mars)",
		PermissionLevel.CHEATER)
	register_command("perf", _cmd_perf, "Toggle TPS and network data overlay",
		PermissionLevel.CHEATER)
	register_command("spikes", _cmd_spikes, "Show recent frame spike diagnostics",
		PermissionLevel.CHEATER)
	register_command("spark", _cmd_spark,
		"Tick profiler controls (usage: /spark profiler <start|stop|top|status>)",
		PermissionLevel.CHEATER)
	register_command("profiler", _cmd_spark,
		"Alias for /spark profiler",
		PermissionLevel.CHEATER)
	register_command("opus", _cmd_opus,
		"Run one-shot tick profile (usage: /opus [seconds] [slow_ms] [top_n])",
		PermissionLevel.CHEATER)

	# OP — debug/admin commands.
	register_command("op", _cmd_op, "Set console permission level (usage: /op <player|cheater|op>)",
		PermissionLevel.OP)


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
		var required_level: int = int(handler.get("level", PermissionLevel.PLAYER))
		if _permission_level < required_level:
			print_line("[color=red]Permission denied: requires %s, current is %s[/color]"
				% [_permission_name(required_level), _permission_name(_permission_level)])
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

	if _player.game_mode == PlayerController.GameMode.OBSERVER:
		print_line("[color=yellow]Flight is always on in observer mode[/color]")
		return
	if _player.game_mode == PlayerController.GameMode.CREATIVE:
		print_line("[color=yellow]Flight is always on in creative mode[/color]")
		return

	_player._flight_enabled = not _player._flight_enabled
	print_line("[color=cyan]Flight: %s[/color]" % ("ON" if _player._flight_enabled else "OFF"))


func _cmd_gamemode(args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var mode_name := args.strip_edges().to_lower()
	match mode_name:
		"survival", "0", "s":
			_player.set_game_mode(PlayerController.GameMode.SURVIVAL)
		"creative", "1", "c":
			_player.set_game_mode(PlayerController.GameMode.CREATIVE)
		"observer", "2", "o":
			_player.set_game_mode(PlayerController.GameMode.OBSERVER)
		_:
			print_line("[color=red]Usage: /gamemode <survival|creative|observer>[/color]")
			return

	print_line("[color=cyan]Game mode set to: %s[/color]" % _player.get_game_mode_name())


func _cmd_gravity(_args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var dir := _player.gravity_direction
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
		var dist := _player.global_position.distance_to(_player.planet_center)
		print_line("Distance to planet center: %.1f" % dist)


func _cmd_help(_args: String) -> void:
	print_line("[color=cyan]Permission: %s[/color]" % _permission_name(_permission_level))
	print_line("[color=cyan]Available commands:[/color]")
	for cmd_name in _command_handlers.keys():
		var handler: Dictionary = _command_handlers[cmd_name]
		var desc: String = handler["description"]
		var lvl: int = int(handler.get("level", 0))
		var lvl_name := _permission_name(lvl)
		var accessible := _permission_level >= lvl
		var prefix := "  " if accessible else "[color=gray]* "
		var suffix := "" if accessible else "[/color]"
		print_line("%s/%s [%s] — %s%s" % [prefix, cmd_name, lvl_name, desc, suffix])


func _cmd_speed(args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	if _player.game_mode == PlayerController.GameMode.SURVIVAL and not _player._flight_enabled:
		print_line("[color=yellow]/speed is only available when flight is active[/color]")
		return

	if args == "":
		var speed_name := "fly_speed" if _player.game_mode != PlayerController.GameMode.OBSERVER else "observer_speed"
		var current := _player.fly_speed if _player.game_mode != PlayerController.GameMode.OBSERVER else _player.observer_speed
		print_line("Current %s: %.1f" % [speed_name, current])
		return

	var value := args.to_float()
	if value <= 0.0:
		print_line("[color=red]Speed must be positive[/color]")
		return

	if _player.game_mode == PlayerController.GameMode.OBSERVER:
		_player.observer_speed = value
	else:
		_player.fly_speed = value
	print_line("[color=cyan]Speed set to %.1f[/color]" % value)


# --- Multi-planet travel commands ---

# /planets — 列出所有可旅行星球及距离。
func _cmd_planets(_args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var planets: Array = _player.get_travelable_planets()
	if planets.is_empty():
		print_line("[color=yellow]No travelable planets available[/color]")
		return

	var upos := _player.get_player_universe_position()
	print_line("[color=cyan]Travelable planets (player @U "
			+ "%.0f,%.0f,%.0f):[/color]" % [upos.x, upos.y, upos.z])

	var active_name := ""
	if _player.universe_manager != null \
			and _player.universe_manager.active_planet != null:
		active_name = _player.universe_manager.active_planet.display_name

	for entry in planets:
		var pname: String = entry["name"]
		var planet: PlanetDescriptor = entry["planet"]
		var dist := _player.get_distance_to_planet(planet)
		var marker := " *" if pname == active_name else ""
		print_line("  %s (dim=%s, radius=%.0f, g=%.2f) dist=%.0f%s" % [
			pname, String(entry["dimension"]), planet.planet_radius,
			planet.gravity_multiplier, dist, marker])


# /travel <name> — 迁移期调试传送，不代表连续宇宙旅行。
func _cmd_travel(args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var target_name := args.strip_edges()
	if target_name == "":
		print_line("[color=red]Usage: /travel <planet name>[/color]")
		print_line("Use /planets to list available destinations")
		return

	print_line("[color=yellow]Debug teleport: switching prototype dimension to '%s'...[/color]"
			% target_name)
	var ok := _player.travel_to_planet_by_name(target_name)
	if ok:
		print_line("[color=cyan]Travel complete — now on %s[/color]" % target_name)
	else:
		print_line("[color=red]Travel failed — no planet matching '%s'[/color]" % target_name)
		print_line("Use /planets to list available destinations")


# /universe — 显示玩家当前宇宙坐标。
func _cmd_universe(_args: String) -> void:
	if _player == null:
		print_line("[color=red]No player reference[/color]")
		return

	var upos := _player.get_player_universe_position()
	print_line("[color=cyan]Universe position: "
			+ "(%.2f, %.2f, %.2f)[/color]" % [upos.x, upos.y, upos.z])
	print_line("Scene position: %s" % _player.global_position)

	if _player.universe_manager != null \
			and _player.universe_manager.active_planet != null:
		var planet := _player.universe_manager.active_planet
		print_line("Active planet: %s (dim=%s)" % [
				planet.display_name, String(planet.dimension_id)])
		print_line("  universe_position: %s" % planet.universe_position)
		print_line("  local_center: %s" % planet.local_center)
		print_line("  radius: %.1f, gravity: %.2f" % [
				planet.planet_radius, planet.gravity_multiplier])


# /op — 设置控制台权限等级。仅 OP 权限可用。
func _cmd_op(args: String) -> void:
	var mode_name := args.strip_edges().to_lower()
	match mode_name:
		"player", "0", "p":
			set_permission_level(PermissionLevel.PLAYER)
		"cheater", "1", "c":
			set_permission_level(PermissionLevel.CHEATER)
		"op", "2", "o":
			set_permission_level(PermissionLevel.OP)
		_:
			print_line("[color=red]Usage: /op <player|cheater|op>[/color]")
			return

	print_line("[color=cyan]Permission set to: %s[/color]" % _permission_name(_permission_level))


# /perf — 切换左上角 TPS 和网络数据传输量覆盖层。
func _cmd_perf(_args: String) -> void:
	if _perf_overlay == null:
		_perf_overlay = PerfOverlay.new()
		_perf_overlay.name = "PerfOverlay"
		var ui_layer := get_parent() as CanvasLayer
		if ui_layer != null:
			ui_layer.add_child(_perf_overlay)
		else:
			add_child(_perf_overlay)

		# 获取 tick_system 和 chunk_bridge 引用。
		var tick_sys: GDTickSystem = null
		var chunk_bridge: ChunkRendererBridge = null
		if _player != null:
			if _player.universe_manager != null:
				tick_sys = _player.universe_manager.tick_system
			chunk_bridge = _player.world
		_perf_overlay.setup(tick_sys, chunk_bridge, _get_runtime_perf_monitor())

	_perf_overlay.toggle()
	var state := "ON" if _perf_overlay.visible else "OFF"
	print_line("[color=cyan]Perf overlay: %s[/color]" % state)


# /spikes [n] — print recent frame spike diagnostics on demand.
func _cmd_spikes(args: String) -> void:
	var monitor := _get_runtime_perf_monitor()
	if monitor == null:
		print_line("[color=red]No RuntimePerfMonitor available[/color]")
		return
	if not monitor.has_method("get_spike_report"):
		print_line("[color=red]RuntimePerfMonitor has no spike report API[/color]")
		return
	var limit := 8
	var trimmed := args.strip_edges()
	if trimmed.is_valid_int():
		limit = clampi(trimmed.to_int(), 1, 32)
	var report: Variant = monitor.call("get_spike_report", limit)
	if typeof(report) == TYPE_PACKED_STRING_ARRAY or typeof(report) == TYPE_ARRAY:
		for line in report:
			print_line(str(line))


# /spark profiler ... — command-style profiler controls inspired by Spark.
func _cmd_spark(args: String) -> void:
	var tick_sys := _get_tick_system_for_profiler()
	if tick_sys == null:
		print_line("[color=red]No GDTickSystem available[/color]")
		return

	var words := args.strip_edges().split(" ", false)
	if words.size() > 0 and String(words[0]).to_lower() == "profiler":
		words.remove_at(0)
	if words.is_empty():
		_print_spark_usage()
		return

	var sub := String(words[0]).to_lower()
	match sub:
		"start", "on":
			var seconds := 0.0
			if words.size() >= 2 and String(words[1]).is_valid_float():
				seconds = maxf(0.0, String(words[1]).to_float())
			if words.size() >= 3 and String(words[2]).is_valid_float():
				tick_sys.set_perf_profiler_slow_scope_ms(
					maxf(0.0, String(words[2]).to_float()))
			tick_sys.clear_perf_profiler()
			tick_sys.set_perf_profiler_enabled(true)
			if seconds > 0.0:
				_opus_profile_active = true
				_opus_profile_remaining = seconds
				_opus_profile_top_n = 8
				print_line("[color=cyan]Tick profiler started for %.1fs[/color]" % seconds)
			else:
				_opus_profile_active = false
				print_line("[color=cyan]Tick profiler started[/color]")
			_print_profiler_status(tick_sys)
		"stop", "off":
			_opus_profile_active = false
			_print_profiler_summary(tick_sys, 8)
			tick_sys.set_perf_profiler_enabled(false)
			print_line("[color=cyan]Tick profiler stopped[/color]")
		"status":
			_print_profiler_status(tick_sys)
		"top", "open":
			var top_n := 8
			if words.size() >= 2 and String(words[1]).is_valid_int():
				top_n = clampi(String(words[1]).to_int(), 1, 32)
			_print_profiler_summary(tick_sys, top_n)
		"clear", "reset":
			tick_sys.clear_perf_profiler()
			print_line("[color=cyan]Tick profiler samples cleared[/color]")
		"interval":
			if words.size() < 2 or not String(words[1]).is_valid_int():
				print_line("[color=red]Usage: /spark profiler interval <ticks>[/color]")
				return
			tick_sys.set_perf_profiler_log_interval_ticks(
				maxi(1, String(words[1]).to_int()))
			_print_profiler_status(tick_sys)
		"threshold", "slow":
			if words.size() < 2 or not String(words[1]).is_valid_float():
				print_line("[color=red]Usage: /spark profiler threshold <ms>[/color]")
				return
			tick_sys.set_perf_profiler_slow_scope_ms(
				maxf(0.0, String(words[1]).to_float()))
			_print_profiler_status(tick_sys)
		"budget":
			if words.size() < 2 or not String(words[1]).is_valid_float():
				print_line("[color=red]Usage: /spark profiler budget <ms>[/color]")
				return
			tick_sys.set_perf_profiler_tick_budget_ms(
				maxf(0.0, String(words[1]).to_float()))
			_print_profiler_status(tick_sys)
		_:
			_print_spark_usage()


# /opus [seconds] [slow_ms] [top_n] — one-shot profiler capture.
func _cmd_opus(args: String) -> void:
	var tick_sys := _get_tick_system_for_profiler()
	if tick_sys == null:
		print_line("[color=red]No GDTickSystem available[/color]")
		return

	var words := args.strip_edges().split(" ", false)
	var seconds := 10.0
	var slow_ms: float = float(tick_sys.get_perf_profiler_slow_scope_ms())
	var top_n := 8
	if words.size() >= 1 and String(words[0]).is_valid_float():
		seconds = maxf(1.0, String(words[0]).to_float())
	if words.size() >= 2 and String(words[1]).is_valid_float():
		slow_ms = maxf(0.0, String(words[1]).to_float())
	if words.size() >= 3 and String(words[2]).is_valid_int():
		top_n = clampi(String(words[2]).to_int(), 1, 32)

	tick_sys.clear_perf_profiler()
	tick_sys.set_perf_profiler_slow_scope_ms(slow_ms)
	tick_sys.set_perf_profiler_enabled(true)
	_opus_profile_active = true
	_opus_profile_remaining = seconds
	_opus_profile_top_n = top_n
	print_line("[color=cyan]Opus profile started: %.1fs slow>=%.2fms top=%d[/color]"
		% [seconds, slow_ms, top_n])


func _finish_opus_profile(reason: String) -> void:
	var tick_sys := _get_tick_system_for_profiler()
	_opus_profile_active = false
	if tick_sys == null:
		print_line("[color=red]Opus profile ended (%s): no GDTickSystem[/color]" % reason)
		return
	print_line("[color=cyan]Opus profile %s[/color]" % reason)
	_print_profiler_summary(tick_sys, _opus_profile_top_n)
	tick_sys.set_perf_profiler_enabled(false)


func _get_tick_system_for_profiler() -> GDTickSystem:
	if _player != null and _player.universe_manager != null:
		return _player.universe_manager.tick_system
	var scene := get_tree().current_scene
	if scene != null:
		return scene.get_node_or_null("GDTickSystem") as GDTickSystem
	return null


func _get_runtime_perf_monitor() -> Node:
	var scene := get_tree().current_scene
	if scene != null:
		return scene.get_node_or_null("RuntimePerfMonitor")
	return null


func _print_spark_usage() -> void:
	print_line("[color=yellow]Usage:[/color]")
	print_line("  /spark profiler start [seconds] [slow_ms]")
	print_line("  /spark profiler stop")
	print_line("  /spark profiler top [n]")
	print_line("  /spark profiler status")
	print_line("  /spark profiler clear")
	print_line("  /spark profiler interval <ticks>")
	print_line("  /spark profiler threshold <ms>")
	print_line("  /opus [seconds] [slow_ms] [top_n]")


func _print_profiler_status(tick_sys: GDTickSystem) -> void:
	print_line("Profiler: %s | budget=%.2fms slow=%.2fms interval=%d ticks" % [
		"ON" if tick_sys.get_perf_profiler_enabled() else "OFF",
		tick_sys.get_perf_profiler_tick_budget_ms(),
		tick_sys.get_perf_profiler_slow_scope_ms(),
		tick_sys.get_perf_profiler_log_interval_ticks(),
	])


func _print_profiler_summary(tick_sys: GDTickSystem, top_n: int) -> void:
	var summary: String = String(tick_sys.get_perf_profiler_summary(top_n))
	if summary != "":
		var display_summary: String = summary.replace("[", "(").replace("]", ")")
		print_line("[color=cyan]%s[/color]" % display_summary)
		print(summary)

	var entries: Array = tick_sys.get_perf_profiler_top(top_n)
	if entries.is_empty():
		print_line("[color=yellow]No profiler samples yet[/color]")
		return
	print_line("[color=cyan]Top tick scopes:[/color]")
	for entry in entries:
		var name := String(entry.get("name", "unknown"))
		print_line("  %s avg=%.3fms max=%.3fms p99=%.3fms share=%.1f%% samples=%d slow=%d" % [
			name,
			float(entry.get("avg_ms", 0.0)),
			float(entry.get("max_ms", 0.0)),
			float(entry.get("p99_ms", 0.0)),
			float(entry.get("budget_share", 0.0)) * 100.0,
			int(entry.get("samples", 0)),
			int(entry.get("slow_samples", 0)),
		])
