# PlayerStatusBars — HUD widget displaying health and hunger bars.
# Positioned below the status label in the top-left corner.
extends Control

const BAR_WIDTH := 200.0
const BAR_HEIGHT := 16.0
const PADDING := 4.0
const LABEL_WIDTH := 36.0
const BG_COLOR := Color(0.1, 0.1, 0.1, 0.65)
const HP_COLOR := Color(0.85, 0.15, 0.15, 0.85)
const HUNGER_COLOR := Color(0.82, 0.55, 0.18, 0.85)
const TEXT_COLOR := Color(1.0, 1.0, 1.0, 0.9)
const STARVING_COLOR := Color(0.7, 0.15, 0.1, 0.9)

var _player: PlayerController = null
var _health_current: float = 100.0
var _health_max: float = 100.0
var _hunger_current: float = 100.0
var _hunger_max: float = 100.0
var _hunger_level: int = 0
var _is_starving: bool = false


func setup(player: PlayerController) -> void:
	_player = player
	set_anchors_preset(PRESET_TOP_LEFT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	# Position below the StatusBackground area.
	offset_left = 16.0
	offset_top = 70.0
	offset_right = 16.0 + BAR_WIDTH + LABEL_WIDTH + PADDING * 3
	offset_bottom = 70.0 + (BAR_HEIGHT + PADDING) * 2 + PADDING


func _ready() -> void:
	queue_redraw()


func _process(_delta: float) -> void:
	if _player == null:
		return
	var survival := _player.game_mode == PlayerController.GameMode.SURVIVAL
	if visible != survival:
		visible = survival
	if not survival:
		return
	_health_current = _player._vitals.get_health_current()
	_health_max = float(_player._vitals.get_health_max())
	_hunger_current = _player._satiation.get_satiation_current()
	_hunger_max = _player._satiation.get_satiation_max()
	_hunger_level = _player._satiation.get_hunger_level()
	_is_starving = _player._satiation.get_is_starving()
	queue_redraw()


func _draw() -> void:
	if _player == null:
		return
	_draw_bar(0, tr("status.hp"), _health_current, _health_max, HP_COLOR)
	_draw_bar(1, tr("status.food"), _hunger_current, _hunger_max,
		STARVING_COLOR if _is_starving else HUNGER_COLOR)


func _draw_bar(row: int, label: String, current: float, maximum: float,
		fill_color: Color) -> void:
	var y := float(row) * (BAR_HEIGHT + PADDING)
	var bar_x := LABEL_WIDTH + PADDING

	# Label
	var font := get_theme_default_font()
	if font:
		draw_string(font, Vector2(0, y + BAR_HEIGHT - 2),
			label, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, TEXT_COLOR)

	# Background
	draw_rect(Rect2(bar_x, y, BAR_WIDTH, BAR_HEIGHT), BG_COLOR)

	# Fill
	if maximum > 0.0:
		var ratio := clampf(current / maximum, 0.0, 1.0)
		if ratio > 0.0:
			draw_rect(Rect2(bar_x, y, BAR_WIDTH * ratio, BAR_HEIGHT), fill_color)

	# Text value
	var text := "%d/%d" % [int(current), int(maximum)]
	if font:
		var text_size := font.get_string_size(text, HORIZONTAL_ALIGNMENT_CENTER, -1, 14)
		var text_x := bar_x + (BAR_WIDTH - text_size.x) * 0.5
		var text_y := y + (BAR_HEIGHT - text_size.y) * 0.5
		draw_string(font, Vector2(text_x, text_y + text_size.y),
			text, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, TEXT_COLOR)
