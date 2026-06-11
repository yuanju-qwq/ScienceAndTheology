class_name WikiUI extends Control

var _is_open := false
var _current_category := "materials"
var _current_entry_id := ""

# UI nodes
var _bg: ColorRect
var _title: Label
var _close_btn: Button
var _search_box: LineEdit

var _cat_container: VBoxContainer
var _cat_buttons: Dictionary = {}  # category -> Button
var _entry_scroll: ScrollContainer
var _entry_list: VBoxContainer

var _detail_panel: VBoxContainer
var _detail_icon: TextureRect
var _detail_title: Label
var _detail_subtitle: Label
var _detail_desc: Label
var _detail_props: GridContainer
var _detail_related: VBoxContainer

var _entry_buttons: Dictionary = {}  # entry_id -> Button

const PANEL_W := 820
const PANEL_H := 520
const LEFT_W := 220
const RIGHT_X := LEFT_W + 12

const COLORS: Dictionary = {
	materials = Color(0.6, 0.8, 1.0),
	fluids = Color(0.4, 0.7, 1.0),
	items = Color(1.0, 0.7, 0.4),
	magic = Color(0.8, 0.5, 1.0),
	guides = Color(0.5, 1.0, 0.5),
}


func _ready() -> void:
	visible = false
	_build_ui()
	_refresh()


# ── Public API ────────────────────────────────────────────────────────────
func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if _is_open:
		_refresh()


# ── UI construction ───────────────────────────────────────────────────────
func _build_ui() -> void:
	size = Vector2(PANEL_W, PANEL_H)
	position = Vector2(
		get_viewport_rect().size.x / 2 - PANEL_W / 2,
		get_viewport_rect().size.y / 2 - PANEL_H / 2
	)

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.07, 0.07, 0.09, 0.95)
	add_child(_bg)

	_title = Label.new()
	_title.text = "Encyclopedia"
	_title.position = Vector2(8, 4)
	_title.size = Vector2(200, 22)
	_title.add_theme_color_override("font_color", Color(0.8, 0.9, 1.0))
	add_child(_title)

	_close_btn = Button.new()
	_close_btn.text = "X"
	_close_btn.position = Vector2(PANEL_W - 28, 2)
	_close_btn.size = Vector2(24, 24)
	_close_btn.pressed.connect(_close)
	add_child(_close_btn)

	# Separator
	var sep := HSeparator.new()
	sep.position = Vector2(0, 26)
	sep.size = Vector2(PANEL_W, 4)
	add_child(sep)

	_build_left_panel()
	_build_detail_panel()


func _build_left_panel() -> void:
	# Search box
	_search_box = LineEdit.new()
	_search_box.position = Vector2(8, 32)
	_search_box.size = Vector2(LEFT_W - 8, 22)
	_search_box.placeholder_text = "Search..."
	_search_box.text_changed.connect(_on_search)
	add_child(_search_box)

	# Category buttons
	_cat_container = VBoxContainer.new()
	_cat_container.position = Vector2(8, 58)
	_cat_container.size = Vector2(LEFT_W - 8, 30 * WikiDatabase.CATEGORY_ORDER.size())
	add_child(_cat_container)

	for cat in WikiDatabase.CATEGORY_ORDER:
		var label: String = WikiDatabase.CATEGORY_LABELS.get(cat, cat)
		var btn := Button.new()
		btn.text = label
		btn.size = Vector2(LEFT_W - 8, 26)
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		btn.toggle_mode = true
		btn.button_pressed = (cat == _current_category)
		btn.pressed.connect(_on_category_pressed.bind(cat))
		_cat_container.add_child(btn)
		_cat_buttons[cat] = btn

	# Entry scroll list
	_entry_scroll = ScrollContainer.new()
	_entry_scroll.position = Vector2(8, 58 + 30 * WikiDatabase.CATEGORY_ORDER.size() + 6)
	_entry_scroll.size = Vector2(LEFT_W - 8, PANEL_H - _entry_scroll.position.y - 8)
	add_child(_entry_scroll)

	_entry_list = VBoxContainer.new()
	_entry_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_entry_scroll.add_child(_entry_list)


func _build_detail_panel() -> void:
	_detail_panel = VBoxContainer.new()
	_detail_panel.position = Vector2(RIGHT_X, 32)
	_detail_panel.size = Vector2(PANEL_W - RIGHT_X - 8, PANEL_H - 38)
	add_child(_detail_panel)

	# Header with icon and title
	var header := HBoxContainer.new()
	header.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(header)

	_detail_icon = TextureRect.new()
	_detail_icon.size = Vector2(40, 40)
	_detail_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_detail_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	_detail_icon.custom_minimum_size = Vector2(40, 40)
	header.add_child(_detail_icon)

	var title_box := VBoxContainer.new()
	title_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title_box)

	_detail_title = Label.new()
	_detail_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_title.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	_detail_title.add_theme_font_size_override("font_size", 18)
	title_box.add_child(_detail_title)

	_detail_subtitle = Label.new()
	_detail_subtitle.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_subtitle.add_theme_color_override("font_color", Color(0.6, 0.7, 0.8))
	title_box.add_child(_detail_subtitle)

	# Separator
	var sep2 := HSeparator.new()
	sep2.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(sep2)

	# Description
	_detail_desc = Label.new()
	_detail_desc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_desc.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_detail_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_desc.add_theme_color_override("font_color", Color(0.8, 0.85, 0.9))
	_detail_panel.add_child(_detail_desc)

	# Properties title
	var props_title := HSeparator.new()
	props_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(props_title)
	var props_label := Label.new()
	props_label.text = "Properties"
	props_label.add_theme_color_override("font_color", Color(0.7, 0.8, 1.0))
	_detail_panel.add_child(props_label)

	# Properties grid
	_detail_props = GridContainer.new()
	_detail_props.columns = 2
	_detail_props.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_props)

	# Related entries
	var related_label := HSeparator.new()
	related_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(related_label)
	var rel_title := Label.new()
	rel_title.text = "Related"
	rel_title.add_theme_color_override("font_color", Color(0.7, 0.8, 1.0))
	_detail_panel.add_child(rel_title)

	_detail_related = VBoxContainer.new()
	_detail_related.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_related)


# ── Refresh ───────────────────────────────────────────────────────────────
func _refresh() -> void:
	_search_box.clear()
	_build_entry_list(_current_category)


# ── Entry list ────────────────────────────────────────────────────────────
func _build_entry_list(category: String) -> void:
	for btn in _entry_buttons.values():
		if is_instance_valid(btn):
			btn.queue_free()
	_entry_buttons.clear()

	var entries := WikiDatabase.get_entries_in_category(category)
	for e in entries:
		var btn := Button.new()
		btn.text = e.title
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
		btn.button_pressed = (e.id == _current_entry_id)
		btn.toggle_mode = true
		btn.pressed.connect(_on_entry_selected.bind(e.id))
		_entry_list.add_child(btn)
		_entry_buttons[e.id] = btn

	# Auto-select first entry if none selected.
	if not _current_entry_id.is_empty():
		if _entry_buttons.has(_current_entry_id):
			_show_entry(_current_entry_id)
			return
	_current_entry_id = ""
	if entries.size() > 0:
		_show_entry(entries[0].id)


func _show_entry(eid: String) -> void:
	var e := WikiDatabase.get_entry(eid)
	if not e:
		return

	_current_entry_id = eid

	# Update detail panel.
	_detail_icon.texture = e.icon
	_detail_title.text = e.title
	_detail_subtitle.text = e.subtitle
	_detail_desc.text = e.description

	# Properties grid.
	for child in _detail_props.get_children():
		child.queue_free()
	for prop in e.properties:
		var label := Label.new()
		label.text = prop.get("label", "")
		label.add_theme_color_override("font_color", Color(0.6, 0.7, 0.8))
		_detail_props.add_child(label)
		var value := Label.new()
		value.text = prop.get("value", "")
		value.add_theme_color_override("font_color", Color(0.85, 0.9, 1.0))
		_detail_props.add_child(value)

	# Related entries.
	for child in _detail_related.get_children():
		child.queue_free()
	if e.related_ids.size() > 0:
		var rel_box := HBoxContainer.new()
		rel_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		for rid in e.related_ids:
			var re := WikiDatabase.get_entry(rid)
			if re:
				var rel_btn := Button.new()
				rel_btn.text = re.title
				rel_btn.pressed.connect(_on_related_pressed.bind(rid))
				rel_box.add_child(rel_btn)
		_detail_related.add_child(rel_box)
	_detail_related.visible = e.related_ids.size() > 0

	# Highlight selected entry in list.
	for entry_id in _entry_buttons:
		var btn := _entry_buttons[entry_id] as Button
		if is_instance_valid(btn):
			btn.button_pressed = (entry_id == _current_entry_id)


# ── Callbacks ─────────────────────────────────────────────────────────────
func _close() -> void:
	_is_open = false
	visible = false


func _on_category_pressed(cat: String) -> void:
	_current_category = cat
	for c in _cat_buttons:
		var btn := _cat_buttons[c] as Button
		if is_instance_valid(btn):
			btn.button_pressed = (c == cat)
	_build_entry_list(cat)


func _on_entry_selected(eid: String) -> void:
	_show_entry(eid)


func _on_related_pressed(eid: String) -> void:
	_show_entry(eid)
	# Scroll entry list to the selected entry.
	for eid2 in _entry_buttons:
		var btn := _entry_buttons[eid2] as Button
		if is_instance_valid(btn):
			btn.button_pressed = (eid2 == eid)
			if eid2 == eid:
				# Scroll into view.
				_entry_scroll.ensure_control_visible(btn)


func _on_search(query: String) -> void:
	# Switch to a "search results" view.
	for btn in _entry_buttons.values():
		if is_instance_valid(btn):
			btn.queue_free()
	_entry_buttons.clear()

	var results := WikiDatabase.search(query)
	for e in results:
		var btn := Button.new()
		btn.text = e.title
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
		btn.pressed.connect(_on_entry_selected.bind(e.id))
		_entry_list.add_child(btn)
		_entry_buttons[e.id] = btn

	if results.size() > 0:
		_show_entry(results[0].id)
