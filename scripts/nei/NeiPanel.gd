class_name NeiPanel
extends Control

signal closed
signal opened

const SLOT_SIZE := 36
const GRID_COLS := 8
const PANEL_SIZE := Vector2(920, 600)
const LEFT_W := 320

var _is_open := false
var _current_item_id := 0
var _detail_mode := "info"
var _item_ids: Array[int] = []
var _filtered_ids: Array[int] = []

var _search_box: LineEdit
var _grid: GridContainer
var _icon: TextureRect
var _title: Label
var _subtitle: Label
var _tabs: HBoxContainer
var _list: VBoxContainer


func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build_ui()
	get_viewport().size_changed.connect(_center)


func toggle() -> void:
	if _is_open:
		close()
	else:
		open()


func open() -> void:
	_is_open = true
	visible = true
	_refresh()
	opened.emit()


func close() -> void:
	_is_open = false
	visible = false
	closed.emit()


func open_for_item(item_id: int, mode: String = "recipes") -> void:
	if not _is_open:
		open()
	_detail_mode = mode
	_select_item(item_id)
	_search_box.clear()


func _center() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _build_ui() -> void:
	size = PANEL_SIZE
	_center()

	var bg := ColorRect.new()
	bg.size = size
	bg.color = Color(0.055, 0.058, 0.07, 0.96)
	add_child(bg)

	var header := Label.new()
	header.text = "Content Browser"
	header.position = Vector2(8, 4)
	header.size = Vector2(260, 24)
	header.add_theme_color_override("font_color", Color(0.82, 0.90, 1.0))
	add_child(header)

	var close_btn := Button.new()
	close_btn.text = "X"
	close_btn.position = Vector2(size.x - 30, 2)
	close_btn.size = Vector2(26, 26)
	close_btn.pressed.connect(close)
	add_child(close_btn)

	_search_box = LineEdit.new()
	_search_box.position = Vector2(8, 38)
	_search_box.size = Vector2(LEFT_W - 16, 26)
	_search_box.placeholder_text = "Search item / key"
	_search_box.text_changed.connect(_on_search)
	add_child(_search_box)

	var scroll := ScrollContainer.new()
	scroll.position = Vector2(8, 70)
	scroll.size = Vector2(LEFT_W - 16, size.y - 78)
	add_child(scroll)

	_grid = GridContainer.new()
	_grid.columns = GRID_COLS
	_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_grid)

	_build_detail_panel()


func _build_detail_panel() -> void:
	var dx := LEFT_W + 8
	var dw := size.x - dx - 8
	var panel := Panel.new()
	panel.position = Vector2(dx, 38)
	panel.size = Vector2(dw, size.y - 46)
	add_child(panel)

	_icon = TextureRect.new()
	_icon.position = Vector2(10, 10)
	_icon.size = Vector2(42, 42)
	_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	panel.add_child(_icon)

	_title = Label.new()
	_title.position = Vector2(62, 8)
	_title.size = Vector2(dw - 72, 26)
	_title.add_theme_color_override("font_color", Color(0.94, 0.97, 1.0))
	_title.add_theme_font_size_override("font_size", 18)
	panel.add_child(_title)

	_subtitle = Label.new()
	_subtitle.position = Vector2(62, 34)
	_subtitle.size = Vector2(dw - 72, 20)
	_subtitle.add_theme_color_override("font_color", Color(0.62, 0.70, 0.82))
	panel.add_child(_subtitle)

	_tabs = HBoxContainer.new()
	_tabs.position = Vector2(8, 64)
	_tabs.size = Vector2(dw - 16, 28)
	panel.add_child(_tabs)

	var detail_scroll := ScrollContainer.new()
	detail_scroll.position = Vector2(8, 98)
	detail_scroll.size = Vector2(dw - 16, panel.size.y - 106)
	panel.add_child(detail_scroll)

	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail_scroll.add_child(_list)


func _refresh() -> void:
	NEIIndex.ensure_built()
	_item_ids = NEIIndex.get_all_item_ids()
	_filtered_ids = _item_ids.duplicate()
	_build_grid(_filtered_ids)
	if _current_item_id <= 0 and not _filtered_ids.is_empty():
		_select_item(_filtered_ids[0])
	elif _current_item_id > 0:
		_select_item(_current_item_id)


func _build_grid(item_ids: Array[int]) -> void:
	for child in _grid.get_children():
		child.queue_free()
	for item_id in item_ids:
		var slot := SlotUI.new()
		slot.slot_index = item_id
		slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.item_stack = ItemStack.new(item_id, 1)
		slot.clicked.connect(_on_slot_clicked)
		slot.right_clicked.connect(_on_slot_right_clicked)
		_grid.add_child(slot)


func _on_slot_clicked(item_id: int, _button: int) -> void:
	_detail_mode = "recipes"
	_select_item(item_id)


func _on_slot_right_clicked(item_id: int) -> void:
	_detail_mode = "usage"
	_select_item(item_id)


func _select_item(item_id: int) -> void:
	_current_item_id = item_id
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		_icon.texture = null
		_title.text = "Item #%d" % item_id
		_subtitle.text = "Unregistered item"
	else:
		_icon.texture = def.icon
		_title.text = tr(def.title_key)
		_subtitle.text = _subtitle_for(item_id, def)
	_rebuild_tabs()
	_show_detail()


func _subtitle_for(item_id: int, def) -> String:
	var key := NEIIndex.get_item_key(item_id)
	var text := "ID %d" % item_id
	if not key.is_empty():
		text += "  |  %s" % key
	if def != null:
		text += "  |  Max %d" % int(def.max_stack)
	return text


func _rebuild_tabs() -> void:
	for child in _tabs.get_children():
		child.queue_free()
	_add_tab("info", "Info")
	_add_tab("recipes", "Recipes")
	_add_tab("usage", "Usage")


func _add_tab(mode: String, label_text: String) -> void:
	var btn := Button.new()
	btn.text = label_text
	btn.toggle_mode = true
	btn.button_pressed = (_detail_mode == mode)
	btn.pressed.connect(_on_tab_pressed.bind(mode))
	_tabs.add_child(btn)


func _on_tab_pressed(mode: String) -> void:
	_detail_mode = mode
	_show_detail()


func _show_detail() -> void:
	_clear_detail()
	if _detail_mode == "info":
		_show_info()
	elif _detail_mode == "recipes":
		_show_recipes(NEIIndex.get_recipes_for_output(_current_item_id), "No recipes found.")
	else:
		_show_recipes(NEIIndex.get_recipes_for_input(_current_item_id), "No usages found.")


func _clear_detail() -> void:
	for child in _list.get_children():
		child.queue_free()


func _show_info() -> void:
	var def = ItemDatabase.get_item(_current_item_id)
	if def == null:
		_add_text("This item id is not registered.", Color(0.85, 0.6, 0.6))
		return
	_add_section("Base")
	_add_kv("Item ID", str(_current_item_id))
	var key := NEIIndex.get_item_key(_current_item_id)
	_add_kv("Item Key", key if not key.is_empty() else "-")
	_add_kv("Max Stack", str(def.max_stack))
	_add_section("Recipe Summary")
	_add_kv("Recipes", str(NEIIndex.get_recipes_for_output(_current_item_id).size()))
	_add_kv("Usages", str(NEIIndex.get_recipes_for_input(_current_item_id).size()))

	var tooltip_lines: Array = def.get_tooltip_lines()
	if not tooltip_lines.is_empty():
		_add_section("Tooltip")
		for line in tooltip_lines:
			_add_text(str(line), Color(0.78, 0.84, 0.92))


func _show_recipes(refs: Array[NEIIndex.RecipeRef], empty_text: String) -> void:
	if refs.is_empty():
		_add_text(empty_text, Color(0.62, 0.64, 0.72))
		return
	for ref in refs:
		_list.add_child(_recipe_card(ref))
		var sep := HSeparator.new()
		sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_list.add_child(sep)


func _recipe_card(ref: NEIIndex.RecipeRef) -> Control:
	var card := PanelContainer.new()
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	card.add_child(box)

	var title := Label.new()
	title.text = _source_tag(ref)
	title.add_theme_color_override("font_color", Color(0.55, 0.82, 1.0))
	box.add_child(title)

	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(row)
	_add_stacks(row, ref.item_inputs, ref.fluid_inputs)
	var arrow := Label.new()
	arrow.text = "  ->  "
	arrow.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	arrow.custom_minimum_size.y = SLOT_SIZE
	row.add_child(arrow)
	_add_stacks(row, ref.item_outputs, ref.fluid_outputs)
	return card


func _add_stacks(parent: HBoxContainer, items: Array[Dictionary], fluids: Array[Dictionary]) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(row)
	for entry in items:
		var item_id := int(entry.get("item_id", 0))
		if item_id <= 0:
			continue
		var slot := SlotUI.new()
		slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.slot_index = item_id
		slot.item_stack = ItemStack.new(item_id, int(entry.get("count", 1)))
		slot.clicked.connect(_on_detail_slot_clicked)
		slot.right_clicked.connect(_on_detail_slot_right_clicked)
		row.add_child(slot)
	for fluid in fluids:
		var label := Label.new()
		label.text = "%s\n%d mB" % [str(fluid.get("fluid_name", "?")), int(fluid.get("amount", 0))]
		label.custom_minimum_size = Vector2(90, SLOT_SIZE)
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.add_theme_color_override("font_color", Color(0.55, 0.75, 1.0))
		row.add_child(label)


func _source_tag(ref: NEIIndex.RecipeRef) -> String:
	if ref.recipe_type == "crafting":
		return "Craft: hand" if ref.machine_type.is_empty() else "Craft: %s" % ref.machine_type
	if ref.recipe_type == "machine":
		return "Machine: %s  T%d  %d EU/t  %d ticks" % [ref.machine_type, ref.min_tier, ref.eu_per_tick, ref.duration_ticks]
	return ref.recipe_type


func _on_detail_slot_clicked(item_id: int, _button: int) -> void:
	_detail_mode = "recipes"
	_select_item(item_id)


func _on_detail_slot_right_clicked(item_id: int) -> void:
	_detail_mode = "usage"
	_select_item(item_id)


func _on_search(query: String) -> void:
	_filtered_ids = NEIIndex.search_item_ids(query)
	_build_grid(_filtered_ids)
	if not _filtered_ids.has(_current_item_id) and not _filtered_ids.is_empty():
		_select_item(_filtered_ids[0])


func _add_section(text: String) -> void:
	var sep := HSeparator.new()
	sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_child(sep)
	var label := Label.new()
	label.text = text
	label.add_theme_color_override("font_color", Color(0.70, 0.84, 1.0))
	_list.add_child(label)


func _add_kv(k: String, v: String) -> void:
	var row := HBoxContainer.new()
	var key := Label.new()
	key.text = k
	key.custom_minimum_size.x = 110
	key.add_theme_color_override("font_color", Color(0.60, 0.70, 0.82))
	row.add_child(key)
	var value := Label.new()
	value.text = v
	value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	value.add_theme_color_override("font_color", Color(0.88, 0.92, 0.98))
	row.add_child(value)
	_list.add_child(row)


func _add_text(text: String, color: Color) -> void:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.add_theme_color_override("font_color", color)
	_list.add_child(label)
