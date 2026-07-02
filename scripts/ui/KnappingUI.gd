class_name KnappingUI extends Control

signal closed

const GRID_COLS := 5
const GRID_ROWS := 4
const CELL_SIZE := 40
const PADDING := 2

# Tool head patterns (5x4): true = keep, false = remove
# Stored as arrays of bitmask rows for compactness
var _patterns := {
	"stone_axe_head": [
		0b11110,  # ####.
		0b10001,  # #...#
		0b11110,  # ####.
		0b10000,  # #....
	],
	"stone_shovel_head": [
		0b01110,  # .###.
		0b10001,  # #...#
		0b01110,  # .###.
		0b00000,  # .....
	],
	"stone_hoe_head": [
		0b00100,  # ..#..
		0b01110,  # .###.
		0b10101,  # #.#.#
		0b01110,  # .###.
	],
	"stone_knife_head": [
		0b01100,  # .##..
		0b10010,  # #..#.
		0b01100,  # .##..
		0b00000,  # .....
	],
}

var _cells: Array[bool] = []  # flattened GRID_COLS * GRID_ROWS grid
var _stone_item_id: int = 0
var _is_open := false

@onready var _panel := get_node_or_null(^"Panel") as Panel
@onready var _title := get_node_or_null(^"Panel/Title") as Label
@onready var _grid := get_node_or_null(^"Panel/Grid") as Control
@onready var _pickup_btn := get_node_or_null(^"Panel/PickupBtn") as Button
@onready var _close_btn := get_node_or_null(^"Panel/CloseBtn") as Button
@onready var _status_label := get_node_or_null(^"Panel/StatusLabel") as Label


func _ready() -> void:
	visible = false
	size = Vector2(300, 350)
	_ensure_ui_nodes()
	_center_in_viewport()
	_build_grid()
	if not _close_btn.pressed.is_connected(_on_close):
		_close_btn.pressed.connect(_on_close)
	if not _pickup_btn.pressed.is_connected(_on_pickup):
		_pickup_btn.pressed.connect(_on_pickup)
	_pickup_btn.disabled = true


func _ensure_ui_nodes() -> void:
	if _panel == null:
		_panel = Panel.new()
		_panel.name = "Panel"
		_panel.position = Vector2.ZERO
		_panel.size = size
		add_child(_panel)
	if _title == null:
		_title = Label.new()
		_title.name = "Title"
		_title.text = tr("knapping.title")
		_title.position = Vector2(20, 12)
		_title.size = Vector2(260, 24)
		_panel.add_child(_title)
	if _grid == null:
		_grid = Control.new()
		_grid.name = "Grid"
		_grid.position = Vector2(20, 48)
		_grid.size = Vector2(GRID_COLS * (CELL_SIZE + PADDING), GRID_ROWS * (CELL_SIZE + PADDING))
		_panel.add_child(_grid)
	if _pickup_btn == null:
		_pickup_btn = Button.new()
		_pickup_btn.name = "PickupBtn"
		_pickup_btn.text = tr("knapping.pickup")
		_pickup_btn.position = Vector2(20, 250)
		_pickup_btn.size = Vector2(160, 36)
		_panel.add_child(_pickup_btn)
	if _close_btn == null:
		_close_btn = Button.new()
		_close_btn.name = "CloseBtn"
		_close_btn.text = tr("knapping.close")
		_close_btn.position = Vector2(200, 250)
		_close_btn.size = Vector2(80, 36)
		_panel.add_child(_close_btn)
	if _status_label == null:
		_status_label = Label.new()
		_status_label.name = "StatusLabel"
	_status_label.text = tr("knapping.status_chip")
		_status_label.position = Vector2(20, 300)
		_status_label.size = Vector2(260, 32)
		_panel.add_child(_status_label)


func _center_in_viewport() -> void:
	var vp := get_viewport_rect().size
	position = (vp - size) / 2.0
	if _panel != null:
		_panel.size = size


func _build_grid() -> void:
	if _grid == null:
		_grid = Control.new()
		_grid.name = "Grid"
		_grid.position = Vector2(20, 40)
		_grid.size = Vector2(GRID_COLS * (CELL_SIZE + PADDING), GRID_ROWS * (CELL_SIZE + PADDING))
		if _panel != null:
			_panel.add_child(_grid)
		else:
			add_child(_grid)

	_cells.resize(GRID_COLS * GRID_ROWS)
	for i in range(GRID_COLS * GRID_ROWS):
		_cells[i] = true
		var col := i % GRID_COLS
		var row := i / GRID_COLS
		var btn := Button.new()
		btn.name = "Cell_%d_%d" % [col, row]
		btn.position = Vector2(col * (CELL_SIZE + PADDING), row * (CELL_SIZE + PADDING))
		btn.size = Vector2(CELL_SIZE, CELL_SIZE)
		btn.toggle_mode = true
		btn.button_pressed = true
		btn.pressed.connect(_on_cell_toggled.bind(i))
		_grid.add_child(btn)


func open(stone_item_id: int) -> void:
	_stone_item_id = stone_item_id
	_is_open = true
	visible = true
	# Reset grid: all cells enabled
	for i in range(GRID_COLS * GRID_ROWS):
		_cells[i] = true
		var btn := _grid.get_child(i) as Button
		if btn:
			btn.button_pressed = true
			btn.disabled = false
	var stone_key := ItemDatabase.get_item_key_by_id(stone_item_id)
	var disp := tr("item." + stone_key) if not stone_key.is_empty() else tr("knapping.stone")
	_title.text = tr("knapping.title_format") % disp
	_pickup_btn.disabled = true
	_status_label.text = tr("knapping.status_chip")


func close() -> void:
	_is_open = false
	visible = false
	_stone_item_id = 0
	closed.emit()


func is_open() -> bool:
	return _is_open


func _on_close() -> void:
	close()


func _on_cell_toggled(index: int) -> void:
	_cells[index] = not _cells[index]
	# Check if remaining cells match any pattern
	var matched := _find_matching_pattern()
	if matched != "":
		_pickup_btn.disabled = false
		_status_label.text = tr("knapping.status_ready")
		_pickup_btn.text = tr("knapping.pickup_format") % tr("item." + matched)
	else:
		_pickup_btn.disabled = true
		_status_label.text = tr("knapping.status_chipping")


func _find_matching_pattern() -> String:
	for pattern_key: String in _patterns:
		if _match_pattern(_patterns[pattern_key]):
			return pattern_key
	return ""


func _match_pattern(pattern_rows: Array) -> bool:
	for row in range(GRID_ROWS):
		var row_mask := 0
		for col in range(GRID_COLS):
			if _cells[row * GRID_COLS + col]:
				row_mask |= (1 << (GRID_COLS - 1 - col))
		if row_mask != int(pattern_rows[row]):
			return false
	return true


func _on_pickup() -> void:
	var matched := _find_matching_pattern()
	if matched == "" or _player == null:
		return
	# Use the command type string directly since GameCommandServer may not be in scope.
	var cmd_server = _player.get_command_server()
	if cmd_server == null:
		return
	var result: Dictionary = cmd_server.submit_command({
		"type": "knapping_pickup",
		"tool_head_key": matched,
		"stone_item_id": _stone_item_id,
	})
	if not bool(result.get("ok", false)):
		_status_label.text = tr("knapping.failed_format") % str(result.get("reason", "unknown"))
		return
	close()


var _player


func set_player(p) -> void:
	_player = p
